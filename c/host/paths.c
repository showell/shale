// paths.c: the Linux host's hook on paths: resolving a path, the
// absolute path's hint, the check before a change and its journal, and the
// secrets rule's checks of a path, a link's target and a program's
// arguments. It holds the mounts (`runs:x` is the mount `runs`'s folder
// joined with `x`, held to the root's rules), the walk of each part with
// `lstat` (the store follows no symbolic link), the read-only mounts, the
// secrets rule (no read, listing, `stat` or `real` of a secrets mount's
// files by any spelling, no program handed its folder) and the journal
// `--undo-on-stop` keeps (undo.c puts it back).
//
// Include store.c, path_run.c and mounts.c first.

// A path as it was before the run's first change of it.
typedef enum {
    // Nothing was there; undo deletes what the run made.
    PRIOR_ABSENT,
    // A folder; undo only reports it if it is gone.
    PRIOR_DIR,
    // A file: its bytes and its mode.
    PRIOR_FILE,
} PriorKind;

typedef struct {
    Str abs;
    PriorKind was;
    Str bytes;
    uint32_t mode;
} Prior;

VEC_DEFINE(Priors, Prior)

// The most bytes `--undo-on-stop` journals: a change past it stops the run,
// so nothing is changed that could not be put back.
enum { PATHS_JOURNAL_MAX = 64 << 20 };

// A secrets mount's folder, worked out once for a run: the mount's name, its
// folder as the mounts file names it (the CLI's), and as this run sees it
// (paths_run_dir_of's answer).
typedef struct {
    Str name;
    Str cli_dir;
    Str run_dir;
} PathsSecretDir;

SLICE_TYPE(PathsSecretDirs, const PathsSecretDir);

// The Linux host's paths, as Zig's `Host` holds them.
typedef struct {
    // The host's own arena, Zig's `h.alloc`: the journal's copies, which
    // outlive the run's values.
    Arena *arena;
    // The root as the CLI was given it, absolute. A mount inside it is taken
    // against the run's own root.
    Str root_abs;
    // The process's working folder, what a relative root is against.
    Str cwd_abs;
    MountsItems mounts;
    // Where the mounts came from, for a stop that names them.
    Str mounts_from;
    // `--undo-on-stop`: journal each path before its first change.
    bool undo;
    Priors journal;
    size_t journal_bytes;
    // The secrets mounts' folders, in the mounts' order, worked out by
    // paths_secret_dirs_make for the run whose root is `secret_dirs_root`,
    // over the mounts `secret_dirs_mounts`; paths_secret_dirs reads them.
    PathsSecretDirs secret_dirs;
    Str secret_dirs_root;
    MountsItems secret_dirs_mounts;
    bool secret_dirs_made;
} Paths;

static void paths_make(Paths *h, Arena *arena, Str root_abs, Str cwd_abs, MountsItems mounts, Str mounts_from) {
    memset(h, 0, sizeof *h);
    h->arena = arena;
    h->root_abs = root_abs;
    h->cwd_abs = cwd_abs;
    h->mounts = mounts;
    h->mounts_from = mounts_from;
    h->journal = Priors_make(arena);
}

static Arena *paths_values(PathRun *r) {
    return r->ctx->values;
}

// Zig's `std.fs.path.resolve` of `base` (absolute) then `p`: an absolute
// `p` starts again from `/`; `.` and empty parts are dropped and `..` takes
// one back, never above `/`. No call to the system, so a link is not seen.
static bool paths_resolve_posix(Arena *a, Str base, Str p, Str *out) {
    Buf b = buf_make(a);
    for (int k = 0; k < 2; k++) {
        Str s = k == 0 ? base : p;
        if (s.len > 0 && str_byte(s, 0) == '/') buf_clear(&b);
        size_t from = 0;
        while (from <= s.len) {
            size_t to = path_part_end(s, from);
            Str part = str_sub(s, from, to);
            if (str_eq(part, S("..") )) {
                while (b.len > 0 && at(b, b.len - 1) != '/') b.len--;
                if (b.len > 0) b.len--;
            } else if (part.len > 0 && !str_eq(part, S("."))) {
                if (buf_byte(&b, '/') != OK || buf_str(&b, part) != OK) return false;
            }
            if (to == s.len) break;
            from = to + 1;
        }
    }
    if (b.len == 0 && buf_byte(&b, '/') != OK) return false;
    buf_fit(&b);
    *out = buf_view(&b);
    return true;
}

// `p` absolute: as it is when it is, else against the working folder,
// resolved, so `.` is the folder itself.
static STATUS_MUST_USE Status paths_absolute(Paths *h, PathRun *r, Pos pos, Str p, Str *out) {
    if (p.len > 0 && str_byte(p, 0) == '/') {
        *out = p;
        return OK;
    }
    if (!paths_resolve_posix(paths_values(r), h->cwd_abs, p, out)) return run_oom(r->ctx, pos);
    return OK;
}

// The run's own root, absolute.
static STATUS_MUST_USE Status paths_root_of(Paths *h, PathRun *r, Pos pos, Str *out) {
    return paths_absolute(h, r, pos, mounts_trim_slash(r->root), out);
}

// `a` then `b` then `c`, in the run's arena.
static STATUS_MUST_USE Status paths_cat(PathRun *r, Pos pos, Str a, Str b, Str c, Str *out) {
    Buf w = buf_make(paths_values(r));
    if (buf_str(&w, a) != OK || buf_str(&w, b) != OK || buf_str(&w, c) != OK) return run_oom(r->ctx, pos);
    buf_fit(&w);
    *out = buf_view(&w);
    return OK;
}

// A mount's folder `dir` as a run whose root is `run_root` sees it, made in
// `a`: one inside the CLI's root is taken against the run's own root,
// absolute (paths_root_of's). false when the arena refuses.
static bool paths_run_dir_in(const Paths *h, Arena *a, Str run_root, Str dir, Str *out) {
    if (!mounts_in_folder(dir, h->root_abs)) {
        *out = dir;
        return true;
    }
    Str given = mounts_trim_slash(run_root);
    Str root = given;
    if (!(given.len > 0 && str_byte(given, 0) == '/') && !paths_resolve_posix(a, h->cwd_abs, given, &root)) return false;
    Buf w = buf_make(a);
    if (buf_str(&w, root) != OK || buf_str(&w, str_sub(dir, h->root_abs.len, dir.len)) != OK) return false;
    buf_fit(&w);
    *out = buf_view(&w);
    return true;
}

// A mount's folder as this run sees it, in the run's arena.
static STATUS_MUST_USE Status paths_run_dir_of(Paths *h, PathRun *r, Pos pos, const Mount *m, Str *out) {
    if (!paths_run_dir_in(h, paths_values(r), r->root, m->dir, out)) return run_oom(r->ctx, pos);
    return OK;
}

// Works out the secrets mounts' folders for the run `r`, once, in the host's
// arena (a yield moves the run's values, and these outlive every call), so
// the checks below read them and a walk's every entry allocates nothing for
// them. Called where a run's PathRun is made, and again when a test changes
// the mounts or the root. false when the arena refuses.
static bool paths_secret_dirs_make(Paths *h, const PathRun *r) {
    size_t n = 0;
    for (size_t k = 0; k < h->mounts.len; k++) n += at(h->mounts, k).secrets ? 1 : 0;
    PathsSecretDir *dirs = ARENA_ARRAY(h->arena, PathsSecretDir, n);
    if (dirs == NULL) return false;
    SLICE_TYPE(Made, PathsSecretDir);
    Made made = {dirs, n};
    size_t i = 0;
    for (size_t k = 0; k < h->mounts.len; k++) {
        const Mount *m = &at(h->mounts, k);
        if (!m->secrets) continue;
        PathsSecretDir d = {m->name, m->dir, S("")};
        if (!paths_run_dir_in(h, h->arena, r->root, m->dir, &d.run_dir)) return false;
        at(made, i) = d;
        i++;
    }
    h->secret_dirs = (PathsSecretDirs){dirs, n};
    h->secret_dirs_root = r->root;
    h->secret_dirs_mounts = h->mounts;
    h->secret_dirs_made = true;
    return true;
}

// The secrets mounts' folders for the run `r`.
// DRAGON: the secrets rule. A list worked out for another root or other
// mounts would miss a secrets folder, and every check below would let it by.
// REMEDY: the list is held to the root and the mounts it was made for, and
// one made for others, or none made, is a bug in the C that ends the process;
// test_paths holds the death.
static PathsSecretDirs paths_secret_dirs(const Paths *h, const PathRun *r) {
    bool same_mounts = h->secret_dirs_mounts.ptr == h->mounts.ptr && h->secret_dirs_mounts.len == h->mounts.len;
    if (!h->secret_dirs_made || !same_mounts || !str_eq(h->secret_dirs_root, r->root)) c_bug("paths: the secrets folders were not worked out for this run's root and mounts");
    return h->secret_dirs;
}

// Whether byte `c` can continue a file's name in a message: a letter, a
// digit, `_` or `-`.
static bool paths_name_byte(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// Whether `text` holds the folder `dir` itself, anywhere in it, rather than
// a sibling whose name only begins with the folder's (`keys2`, `keys.bak`).
// DRAGON: the secrets rule. A plain substring test takes `R/keys2/f` for
// `R/keys`, and the check's caller then ends the process, skipping the
// jobs' end and the undo; too narrow a test would let the folder through.
// REMEDY: an occurrence is a sibling only when a name byte follows it, or a
// `.` and then a name byte, so the folder at a message's end (`R/keys.`) or
// before `/`, a quote or a space still counts; every occurrence is tried,
// and test_paths holds both sides.
static bool paths_text_names_dir(Str text, Str dir) {
    if (dir.len == 0 || dir.len > text.len) return false;
    for (size_t i = 0; i + dir.len <= text.len; i++) {
        if (!str_eq(str_sub(text, i, i + dir.len), dir)) continue;
        size_t after = i + dir.len;
        if (after == text.len) return true;
        uint8_t c = str_byte(text, after);
        if (paths_name_byte(c)) continue;
        if (c == '.' && after + 1 < text.len && paths_name_byte(str_byte(text, after + 1))) continue;
        return true;
    }
    return false;
}

// The `PathHook.names_secret` check: whether `text` holds a secrets mount's
// folder, as the mounts file names it or as this run sees it.
static STATUS_MUST_USE Status paths_names_secret(void *ctx, PathRun *r, Pos pos, Str text, bool *names);

// The secrets rule's check of a report the host writes past every stop's
// (the undo's lines): one that names a secrets mount's folder is a bug in
// the C, and the process ends before it is said. STOP when the arena
// refuses the check.
static inline STATUS_MUST_USE Status paths_report_names_no_secret(Paths *h, PathRun *r, Str text) {
    bool names = false;
    Pos none = {0, 0};
    TRY(paths_names_secret(h, r, none, text, &names));
    if (names) c_bug("paths: a report line names a secrets mount's folder");
    return OK;
}

static STATUS_MUST_USE Status paths_names_secret(void *ctx, PathRun *r, Pos pos, Str text, bool *names) {
    Paths *h = ctx;
    (void)pos;
    *names = false;
    PathsSecretDirs sd = paths_secret_dirs(h, r);
    for (size_t k = 0; k < sd.len; k++) {
        if (paths_text_names_dir(text, at(sd, k).cli_dir) || paths_text_names_dir(text, at(sd, k).run_dir)) {
            *names = true;
            return OK;
        }
    }
    return OK;
}

// A path (`shown`, at `abs`) that reaches a secrets mount's folder by
// another way, the root or another mount, is refused as the mount's own
// path is. A folder above it may be listed: names are not secrets.
// DRAGON: the secrets rule. A root path (`keys/tok`) or another mount's
// (`all:keys/tok`) is not the secrets mount's by its name, and a link would
// slip past a test of the OS path as written.
// REMEDY: this test of where the path lands, after paths_refuse_links has
// refused every link on the way; test_paths plants the root's, another
// mount's and a link's way in.
static STATUS_MUST_USE Status paths_refuse_secrets_under(Paths *h, PathRun *r, Pos pos, Str shown, Str abs) {
    PathsSecretDirs sd = paths_secret_dirs(h, r);
    for (size_t k = 0; k < sd.len; k++) {
        if (mounts_in_folder(abs, at(sd, k).run_dir)) {
            return path_fail(r, pos, CLASS_REFUSED, S("\"{s}\" is in the mount `{s}`, which holds secrets"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(at(sd, k).name)));
        }
    }
    return OK;
}

// The `PathHook.in_secrets` check: whether `full`, an OS path a walk
// reached below a folder it was allowed (the root, a mount's), is a secrets
// mount's folder or inside it, by paths_refuse_secrets_under's own test, so
// the walk leaves it out.
// DRAGON: the secrets rule. A walk lists and stats each entry by its OS
// path, never through the store's confinement, so a secrets mount's folder
// under the root would be listed, its files named and sized.
// REMEDY: builtins_walk.c asks this of every entry before it stats, lists or
// keeps it, and of every folder `after` resumes in; test_host_grep_tree
// plants a secrets mount under the root.
static STATUS_MUST_USE Status paths_in_secrets(void *ctx, PathRun *r, Pos pos, Str full, bool *in) {
    Paths *h = ctx;
    *in = false;
    PathsSecretDirs sd = paths_secret_dirs(h, r);
    if (sd.len == 0) return OK;
    Str abs = S("");
    TRY(paths_absolute(h, r, pos, mounts_trim_slash(full), &abs));
    for (size_t k = 0; k < sd.len; k++) {
        if (mounts_in_folder(abs, at(sd, k).run_dir)) {
            *in = true;
            return OK;
        }
    }
    return OK;
}

// The mount `path` names before its `:` at `colon`, into `*out`; a stop
// naming the mounts there are when it is none.
static STATUS_MUST_USE Status paths_mount_of(Paths *h, PathRun *r, Pos pos, Str path, size_t colon, const Mount **out) {
    Str name = str_sub(path, 0, colon);
    for (size_t k = 0; k < h->mounts.len; k++) {
        if (str_eq(at(h->mounts, k).name, name)) {
            *out = &at(h->mounts, k);
            return OK;
        }
    }
    if (h->mounts.len == 0) {
        return path_fail(r, pos, CLASS_ARGUMENT, S("\"{s}\": `{s}:` is not a mount: there are none ({s})"), MSG_ARGS(MSG_FROM_SCRIPT(path), MSG_FROM_SCRIPT(name), MSG_FROM_HOST(h->mounts_from)));
    }
    Buf names = buf_make(paths_values(r));
    for (size_t k = 0; k < h->mounts.len; k++) {
        if (k > 0 && buf_lit(&names, ", ") != OK) return run_oom(r->ctx, pos);
        if (buf_byte(&names, '`') != OK || buf_str(&names, at(h->mounts, k).name) != OK || buf_byte(&names, '`') != OK) return run_oom(r->ctx, pos);
    }
    return path_fail(r, pos, CLASS_ARGUMENT, S("\"{s}\": `{s}:` is not a mount: the mounts are {s} ({s})"),
                     MSG_ARGS(MSG_FROM_SCRIPT(path), MSG_FROM_SCRIPT(name), MSG_FROM_HOST(buf_view(&names)), MSG_FROM_HOST(h->mounts_from)));
}

// Stops the run if any part of `rest` below `base` is a symbolic link,
// each part `lstat`ed and charged a step; a missing part ends the walk.
// The stop names the path as the script wrote it, `shown_prefix` first.
// DRAGON: the store's boundaries. A link anywhere below the root or a
// mount's folder would let a path that passes every check land outside them
// (`dir/up -> ../../etc`). A link made between this walk and the store's call
// is the one race it cannot see.
// REMEDY: each part is lstat'ed, never followed, before the store is
// called; test_paths plants a link to a file, to a folder, in the middle, in
// a mount and out of the root.
static STATUS_MUST_USE Status paths_refuse_links(PathRun *r, Pos pos, Str base, Str rest, Str shown_prefix) {
    if (rest.len == 0) return OK;
    Buf walked = buf_make(paths_values(r));
    if (buf_str(&walked, base) != OK) return run_oom(r->ctx, pos);
    size_t from = 0;
    size_t shown_end = 0;
    while (from <= rest.len) {
        size_t to = path_part_end(rest, from);
        Str part = str_sub(rest, from, to);
        TRY(path_charge(r, pos, 1));
        if (buf_byte(&walked, '/') != OK || buf_str(&walked, part) != OK) return run_oom(r->ctx, pos);
        shown_end += part.len + (shown_end > 0 ? 1 : 0);
        struct stat st;
        StoreErr e = store_os_stat(buf_view(&walked), false, &st);
        if (e != STORE_OK) return OK;
        if (S_ISLNK(st.st_mode)) {
            return path_fail(r, pos, CLASS_REFUSED, S("{s}{s} is a symbolic link: the store follows none"), MSG_ARGS(MSG_FROM_SCRIPT(shown_prefix), MSG_FROM_SCRIPT(str_sub(rest, 0, shown_end))));
        }
        if (to == rest.len) break;
        from = to + 1;
    }
    return OK;
}

// `path` (the mount `m`'s, its `:` at `colon`) as an OS path, the rest held
// to the root's rules and walked for links.
static STATUS_MUST_USE Status paths_mount_path(Paths *h, PathRun *r, Pos pos, Str path, size_t colon, const Mount *m, Str *out) {
    Str rest = str_sub(path, colon + 1, path.len);
    if (str_eq(rest, S("."))) rest = S("");
    if (rest.len > 0 && str_byte(rest, 0) == '/') {
        return path_fail(r, pos, CLASS_ARGUMENT, S("\"{s}\" is absolute inside its mount: a mount's paths are relative to it"), MSG_ARGS(MSG_FROM_SCRIPT(path)));
    }
    size_t from = 0;
    while (rest.len > 0 && from <= rest.len) {
        size_t to = path_part_end(rest, from);
        Str part = str_sub(rest, from, to);
        if (str_eq(part, S(".."))) {
            return path_fail(r, pos, CLASS_ARGUMENT, S("\"{s}\" climbs out of its mount with `..`: a folder beside it is reached through a mount of its own (`shale --help` lists this box's mounts)"), MSG_ARGS(MSG_FROM_SCRIPT(path)));
        }
        if (!store_fat_name(part)) {
            return path_fail(r, pos, CLASS_ARGUMENT, S("\"{s}\" has a part (\"{s}\") that is not a name: no `.`, `..`, empty parts or characters FAT refuses"), MSG_ARGS(MSG_FROM_SCRIPT(path), MSG_FROM_SCRIPT(part)));
        }
        if (to == rest.len) break;
        from = to + 1;
    }
    // A missing mount folder stops the run only when the mount is used. A
    // stop whose folder would spell a secrets mount's names the mount alone:
    // that folder is what the secrets rule hides.
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, m->dir, &c);
    // O_PATH, as Zig's `openDir` without `iterate`: a folder that may be
    // searched but not listed still opens.
    int fd = e == STORE_OK ? open(c, O_PATH | O_DIRECTORY | O_CLOEXEC) : -1;
    if (fd >= 0) {
        close(fd);
    } else {
        StoreErr why = e != STORE_OK ? e : store_err_of(errno);
        bool spells = mounts_spell_secret_folder(h->mounts, m->dir);
        if (why == STORE_FILE_NOT_FOUND || why == STORE_NOT_DIR) {
            if (m->secrets) return path_fail(r, pos, CLASS_STORE, S("the mount `{s}` is a secrets mount whose folder is missing"), MSG_ARGS(MSG_FROM_HOST(m->name)));
            if (spells) return path_fail(r, pos, CLASS_STORE, S("the mount `{s}`'s folder is missing; its path holds a secrets mount's folder, so it is not named"), MSG_ARGS(MSG_FROM_HOST(m->name)));
            return path_fail(r, pos, CLASS_STORE, S("the mount `{s}` is {s}, which is not a folder there"), MSG_ARGS(MSG_FROM_HOST(m->name), MSG_FROM_HOST(m->dir)));
        }
        if (m->secrets) return path_fail(r, pos, CLASS_STORE, S("the mount `{s}` is a secrets mount whose folder cannot be opened: {s}"), MSG_ARGS(MSG_FROM_HOST(m->name), MSG_FROM_HOST(store_err_name(why))));
        if (spells) return path_fail(r, pos, CLASS_STORE, S("the mount `{s}`'s folder cannot be opened: {s}; its path holds a secrets mount's folder, so it is not named"), MSG_ARGS(MSG_FROM_HOST(m->name), MSG_FROM_HOST(store_err_name(why))));
        return path_fail(r, pos, CLASS_STORE, S("the mount `{s}` is {s}, which cannot be opened: {s}"), MSG_ARGS(MSG_FROM_HOST(m->name), MSG_FROM_HOST(m->dir), MSG_FROM_HOST(store_err_name(why))));
    }
    Str dir = S("");
    TRY(paths_run_dir_of(h, r, pos, m, &dir));
    TRY(paths_refuse_links(r, pos, dir, rest, str_sub(path, 0, colon + 1)));
    if (rest.len == 0) {
        *out = dir;
        return OK;
    }
    return paths_cat(r, pos, dir, str_eq(dir, S("/")) ? S("") : S("/"), rest, out);
}

// Where `path`'s first part's `:` is, when it has one.
static bool paths_colon(Str path, size_t *colon) {
    size_t first_end = path_part_end(path, 0);
    const char *c = str_find_byte(str_sub(path, 0, first_end), ':');
    if (c == NULL) return false;
    *colon = (size_t)(c - path.ptr);
    return true;
}

// The `PathHook.resolve` of a mount's path to its folder joined with the
// rest (`*resolved` true); a root path is left to the root's rules, after
// its links and its way into a secrets mount's folder are refused. Zig's
// `resolve`.
static STATUS_MUST_USE Status paths_resolve(void *ctx, PathRun *r, Pos pos, Str path, bool *resolved, Str *full) {
    Paths *h = ctx;
    *resolved = false;
    size_t colon = 0;
    if (!paths_colon(path, &colon)) {
        // The core checks a root path's names; links are refused only once
        // every part is a name, so a bad name gets the core's message.
        size_t from = 0;
        while (from <= path.len) {
            size_t to = path_part_end(path, from);
            if (!store_fat_name(str_sub(path, from, to))) return OK;
            if (to == path.len) break;
            from = to + 1;
        }
        TRY(paths_refuse_links(r, pos, mounts_trim_slash(r->root), path, S("")));
        if (paths_secret_dirs(h, r).len == 0) return OK;
        Str root = S("");
        TRY(paths_root_of(h, r, pos, &root));
        Str abs = S("");
        TRY(paths_cat(r, pos, root, S("/"), path, &abs));
        return paths_refuse_secrets_under(h, r, pos, path, abs);
    }
    const Mount *m = NULL;
    TRY(paths_mount_of(h, r, pos, path, colon, &m));
    // DRAGON: the secrets rule. A secrets mount's own name reaching the store
    // would read, list or change its tokens.
    // REMEDY: refused here, before its folder is opened or walked, for every
    // call; only paths_secret_path and paths_secret_target take one.
    // test_paths plants each builtin's path through it.
    if (m->secrets) {
        return path_fail(r, pos, CLASS_REFUSED, S("\"{s}\": the mount `{s}` holds secrets: only `run`'s `secrets` (or `http_get`'s `bearer`) may name its files"), MSG_ARGS(MSG_FROM_SCRIPT(path), MSG_FROM_HOST(m->name)));
    }
    Str os = S("");
    TRY(paths_mount_path(h, r, pos, path, colon, m, &os));
    Str abs = S("");
    TRY(paths_absolute(h, r, pos, os, &abs));
    TRY(paths_refuse_secrets_under(h, r, pos, path, abs));
    *resolved = true;
    *full = os;
    return OK;
}

// The `PathHook.absolute_hint`: an absolute path as a script writes it,
// through the deepest of the root and the mounts whose folder holds it, a
// tie to the root. A path in a secrets mount's folder gets none, so the
// stop names neither that mount nor where its folder is; neither does one
// whose rest is no script's path. The folders are the CLI's (`root_abs`, a
// mount's `dir`), as the script's author sees them.
static STATUS_MUST_USE Status paths_absolute_hint(void *ctx, PathRun *r, Pos pos, Str abs_given, bool *has, AbsoluteHint *out) {
    Paths *h = ctx;
    *has = false;
    Str abs = mounts_trim_slash(abs_given);
    PathsSecretDirs sd = paths_secret_dirs(h, r);
    for (size_t k = 0; k < sd.len; k++) {
        if (mounts_in_folder(abs, mounts_trim_slash(at(sd, k).cli_dir))) return OK;
    }
    Str best_dir = mounts_trim_slash(h->root_abs);
    const Mount *best = NULL;
    bool held = mounts_in_folder(abs, best_dir);
    for (size_t k = 0; k < h->mounts.len; k++) {
        const Mount *m = &at(h->mounts, k);
        if (m->secrets) continue;
        Str dir = mounts_trim_slash(m->dir);
        if (!mounts_in_folder(abs, dir)) continue;
        if (held && dir.len <= best_dir.len) continue;
        best_dir = dir;
        best = m;
        held = true;
    }
    if (!held) return OK;
    Str rest = S("");
    if (str_eq(best_dir, S("/"))) {
        rest = str_sub(abs, 1, abs.len);
    } else if (abs.len != best_dir.len) {
        rest = str_sub(abs, best_dir.len + 1, abs.len);
    }
    size_t from = 0;
    while (rest.len > 0 && from <= rest.len) {
        size_t to = path_part_end(rest, from);
        if (!store_fat_name(str_sub(rest, from, to))) return OK;
        if (to == rest.len) break;
        from = to + 1;
    }
    Str tail = rest.len == 0 ? S(".") : rest;
    *has = true;
    out->mount = best != NULL ? best->name : S("");
    out->spelled = tail;
    if (best == NULL) return OK;
    return paths_cat(r, pos, best->name, S(":"), tail, &out->spelled);
}

// Whether `abs` is under a writable mount whose folder is inside `ro_dir`:
// the nearer mount decides. Zig's `underWritableWithin`, which reads a
// failure to spell a mount's folder as no.
static bool paths_under_writable_within(Paths *h, PathRun *r, Pos pos, Str abs, Str ro_dir) {
    for (size_t k = 0; k < h->mounts.len; k++) {
        const Mount *m = &at(h->mounts, k);
        if (m->access == MOUNT_READ_ONLY) continue;
        Str dir = S("");
        Stop saved = r->ctx->stop;
        bool was = r->ctx->stopped;
        if (paths_run_dir_of(h, r, pos, m, &dir) != OK) {
            r->ctx->stop = saved;
            r->ctx->stopped = was;
            return false;
        }
        if (dir.len <= ro_dir.len || !mounts_in_folder(dir, ro_dir)) continue;
        if (mounts_in_folder(abs, dir)) return true;
    }
    return false;
}

// Journals what is at `abs_in` before its first change: a file's bytes and
// mode, a folder, or nothing there; a later change of it journals nothing.
// Refused, before anything changes, past the journal's room, for a file it
// cannot read, and for what is neither a file nor a folder. Zig's
// `noteBefore`.
// DRAGON: the undo journal. A change made before its path is journaled
// could not be put back.
// REMEDY: may_change calls this before every write, and it stops the run
// rather than let through a change it could not undo; the room is checked
// against the file's size before it is read. test_paths plants the room, an
// unreadable file, a fifo and an unsearchable folder.
static STATUS_MUST_USE Status paths_note_before(Paths *h, PathRun *r, Pos pos, Str abs_in) {
    Str abs = mounts_trim_slash(abs_in);
    for (size_t k = 0; k < h->journal.len; k++) {
        if (str_eq(at(h->journal, k).abs, abs)) return OK;
    }
    struct stat st;
    StoreErr e = store_os_stat(abs, false, &st);
    Prior p = {S(""), PRIOR_ABSENT, S(""), 0};
    if (e == STORE_OK && S_ISDIR(st.st_mode)) {
        p.was = PRIOR_DIR;
    } else if (e == STORE_OK && S_ISREG(st.st_mode)) {
        size_t room = PATHS_JOURNAL_MAX - h->journal_bytes;
        if ((uint64_t)st.st_size > room) {
            MsgNum most;
            return path_fail(r, pos, CLASS_REFUSED, S("--undo-on-stop keeps at most {d} bytes of what a run changes, and {s} would pass it"), MSG_ARGS(MSG_FROM_HOST(msg_num(&most, PATHS_JOURNAL_MAX)), MSG_FROM_HOST(abs)));
        }
        Str bytes = S("");
        StoreErr re = store_os_read_file(h->arena, abs, room + 1, &bytes);
        if (re != STORE_OK) {
            return path_fail(r, pos, CLASS_STORE, S("--undo-on-stop: {s} cannot be read ({t}), so it could not be put back"), MSG_ARGS(MSG_FROM_HOST(abs), MSG_FROM_HOST(store_err_name(re))));
        }
        h->journal_bytes += bytes.len;
        p.was = PRIOR_FILE;
        p.bytes = bytes;
        p.mode = st.st_mode & 07777;
    } else if (e == STORE_OK) {
        return path_fail(r, pos, CLASS_REFUSED, S("--undo-on-stop: {s} is neither a file nor a folder, and could not be put back"), MSG_ARGS(MSG_FROM_HOST(abs)));
    } else if (e != STORE_FILE_NOT_FOUND) {
        return path_fail(r, pos, CLASS_STORE, S("--undo-on-stop: {s} cannot be looked at ({t}), so it could not be put back"), MSG_ARGS(MSG_FROM_HOST(abs), MSG_FROM_HOST(store_err_name(e))));
    }
    if (!arena_dup(h->arena, abs, &p.abs) || Priors_push(&h->journal, p) != OK) return run_oom(r->ctx, pos);
    return OK;
}

// The `PathHook.may_change`: refuses a change through a read-only mount,
// by its name or by a root path into its folder (a writable mount inside it
// decides for its own folder), then journals the path under
// `--undo-on-stop`.
// DRAGON: the store's boundaries. A builtin that skipped this would write
// through a read-only mount.
// REMEDY: every change builtin asks it before it writes (builtins_change's
// `admit`), and it judges where the path lands as well as its name, so
// `frozen/a.zig` from the root is refused as `ro:a.zig` is. test_paths plants
// each.
static STATUS_MUST_USE Status paths_may_change(void *ctx, PathRun *r, Pos pos, Str rel, Str full) {
    Paths *h = ctx;
    Str abs = S("");
    TRY(paths_absolute(h, r, pos, mounts_trim_slash(full), &abs));
    for (size_t k = 0; k < h->mounts.len; k++) {
        const Mount *m = &at(h->mounts, k);
        if (m->access != MOUNT_READ_ONLY) continue;
        bool named = str_starts_with(rel, m->name) && rel.len > m->name.len && str_byte(rel, m->name.len) == ':';
        Str dir = S("");
        TRY(paths_run_dir_of(h, r, pos, m, &dir));
        bool under = mounts_in_folder(abs, dir);
        if (named || (under && !paths_under_writable_within(h, r, pos, abs, dir))) {
            return path_fail(r, pos, CLASS_REFUSED, S("{s}: mount `{s}` is read-only"), MSG_ARGS(MSG_FROM_SCRIPT(rel), MSG_FROM_HOST(m->name)));
        }
    }
    if (h->undo) TRY(paths_note_before(h, r, pos, abs));
    return OK;
}

static void paths_hook(Paths *h, PathHook *out) {
    PathHook v = {h, paths_resolve, paths_may_change, paths_absolute_hint, paths_names_secret, paths_in_secrets};
    *out = v;
}

// A Shale path's OS path, through the mounts or the root, made absolute:
// `real`'s answer, and what a program's `cwd` is. Zig's `osPath`, its
// string already checked.
static STATUS_MUST_USE Status paths_os_path(Paths *h, PathRun *r, Pos pos, Str who, Str given, bool allow_root, Str *out) {
    StorePath p;
    TRY(path_arg(r, pos, who, given, allow_root, &p));
    return paths_absolute(h, r, pos, mounts_trim_slash(p.full), out);
}

// A secret's file, for `run`'s `secrets`: its OS path, through any mount, a
// secrets one included (which every other path refuses). Zig's
// `secretPath`, its string already checked.
static inline STATUS_MUST_USE Status paths_secret_path(Paths *h, PathRun *r, Pos pos, Str who, Str path, Str *out) {
    size_t colon = 0;
    if (!paths_colon(path, &colon)) return paths_os_path(h, r, pos, who, path, false, out);
    const Mount *m = NULL;
    TRY(paths_mount_of(h, r, pos, path, colon, &m));
    if (!m->secrets) return paths_os_path(h, r, pos, who, path, false, out);
    if (colon + 1 == path.len) return path_fail(r, pos, CLASS_ARGUMENT, S("\"{s}\": a secret is a file in the mount, not the mount"), MSG_ARGS(MSG_FROM_SCRIPT(path)));
    Str os = S("");
    TRY(paths_mount_path(h, r, pos, path, colon, m, &os));
    return paths_absolute(h, r, pos, os, out);
}

// Where `run`'s `stdout_secret` writes: a file directly in a secrets mount
// (`keys:NAME`), its OS path. A path anywhere else, the mount itself, a
// folder in it, `..` or a link is refused. Zig's `secretTarget`, its
// string already checked.
static STATUS_MUST_USE Status paths_secret_target(Paths *h, PathRun *r, Pos pos, Str path, Str *out) {
    size_t colon = 0;
    if (!paths_colon(path, &colon)) {
        return path_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` \"{s}\" is not in a mount: a secret is stored only in a `secrets` mount, `\"keys:NAME\"`"), MSG_ARGS(MSG_FROM_SCRIPT(path)));
    }
    const Mount *m = NULL;
    TRY(paths_mount_of(h, r, pos, path, colon, &m));
    if (!m->secrets) {
        return path_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` \"{s}\": the mount `{s}` does not hold secrets: a secret is stored only in a `secrets` mount"), MSG_ARGS(MSG_FROM_SCRIPT(path), MSG_FROM_HOST(m->name)));
    }
    Str rest = str_sub(path, colon + 1, path.len);
    if (rest.len == 0 || str_eq(rest, S("."))) return path_fail(r, pos, CLASS_ARGUMENT, S("\"{s}\": a secret is a file in the mount, not the mount"), MSG_ARGS(MSG_FROM_SCRIPT(path)));
    if (str_find_byte(rest, '/') != NULL) {
        return path_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` \"{s}\" is in a folder of the mount: a secret is a file directly in it, `\"{s}:NAME\"`"), MSG_ARGS(MSG_FROM_SCRIPT(path), MSG_FROM_HOST(m->name)));
    }
    Str os = S("");
    TRY(paths_mount_path(h, r, pos, path, colon, m, &os));
    return paths_absolute(h, r, pos, os, out);
}

typedef enum {
    PATHS_NAMES_NONE,
    PATHS_NAMES_INSIDE,
    PATHS_NAMES_HOLDS,
} PathsNames;

// Whether `arg`, as a program may take it (whole, or after an option's
// `=`), reaches `dir`: inside it by its text, resolved against the
// program's `cwd`, or through a link; or holding it, a folder above it.
// Each spelling tried is charged a step.
static STATUS_MUST_USE Status paths_names_folder(PathRun *r, Pos pos, Str arg, Str cwd, Str dir, PathsNames *out) {
    *out = PATHS_NAMES_NONE;
    if (dir.len == 0 || mem_find(arg.ptr, arg.len, dir.ptr, dir.len) != SIZE_MAX) {
        *out = PATHS_NAMES_INSIDE;
        return OK;
    }
    const char *eq = str_find_byte(arg, '=');
    Str after_eq = eq != NULL ? str_sub(arg, (size_t)(eq - arg.ptr) + 1, arg.len) : S("");
    for (int k = 0; k < 2; k++) {
        Str p = k == 0 ? arg : after_eq;
        if (p.len == 0) continue;
        TRY(path_charge(r, pos, 1));
        Str abs = S("");
        if (!paths_resolve_posix(paths_values(r), cwd, p, &abs)) return run_oom(r->ctx, pos);
        if (mounts_in_folder(abs, dir)) {
            *out = PATHS_NAMES_INSIDE;
            return OK;
        }
        if (mounts_in_folder(dir, abs)) {
            *out = PATHS_NAMES_HOLDS;
            return OK;
        }
        StoreCPath cp;
        const char *c = NULL;
        StoreErr e = store_cstr(&cp, abs, &c);
        char real[PATH_MAX];
        if (e == STORE_OK && realpath(c, real) != NULL) {
            Str rp = {real, strlen(real)};
            if (mounts_in_folder(rp, dir)) {
                *out = PATHS_NAMES_INSIDE;
                return OK;
            }
            if (mounts_in_folder(dir, rp)) {
                *out = PATHS_NAMES_HOLDS;
                return OK;
            }
        }
    }
    return OK;
}

SLICE_TYPE(PathsStrs, const Str);

// Refuses a program's argument, or an `env` value, that names a secrets
// mount's folder: a token is handed over as `secrets`, never as a path a
// program reads. `who` is `run` or `job_start`; `env_keys` and `env_values`
// are the `env` map's string entries, in its order. Zig's
// `refuseSecretArgs`.
// DRAGON: the secrets rule. This is an argv guard, not a sandbox: a program
// that builds a path itself, takes one after a prefix (`-Tkeys/tok`) or
// follows a link it made reaches a secret all the same (SHALE-AGENTS.md says
// so).
// REMEDY: it refuses what a script spells: the folder by its text, by `.` and
// `..` against the program's folder, after an option's `=`, through a link,
// and a folder above it; test_paths plants each.
static inline STATUS_MUST_USE Status paths_refuse_secret_args(Paths *h, PathRun *r, Pos pos, Str who, PathsStrs argv, Str cwd, PathsStrs env_keys, PathsStrs env_values) {
    PathsSecretDirs sd = paths_secret_dirs(h, r);
    for (size_t k = 0; k < sd.len; k++) {
        const PathsSecretDir *m = &at(sd, k);
        Str dir = m->run_dir;
        for (size_t i = 0; i < argv.len; i++) {
            PathsNames n = PATHS_NAMES_NONE;
            TRY(paths_names_folder(r, pos, at(argv, i), cwd, dir, &n));
            MsgNum num;
            Str nth = msg_num(&num, i + 1);
            if (n == PATHS_NAMES_INSIDE) {
                return path_fail(r, pos, CLASS_REFUSED, S("`{s}`'s argument {d} names the mount `{s}`'s folder, which holds secrets: hand one over as `secrets`"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_HOST(nth), MSG_FROM_HOST(m->name)));
            }
            if (n == PATHS_NAMES_HOLDS) {
                return path_fail(r, pos, CLASS_REFUSED, S("`{s}`'s argument {d} holds the mount `{s}`'s folder, which holds secrets: name a folder beside it"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_HOST(nth), MSG_FROM_HOST(m->name)));
            }
        }
        for (size_t i = 0; i < env_keys.len; i++) {
            PathsNames n = PATHS_NAMES_NONE;
            TRY(paths_names_folder(r, pos, at(env_values, i), cwd, dir, &n));
            if (n != PATHS_NAMES_NONE) {
                return path_fail(r, pos, CLASS_REFUSED, S("`{s}`'s `env` {s} names the mount `{s}`'s folder, which holds secrets: hand one over as `secrets`"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(at(env_keys, i)), MSG_FROM_HOST(m->name)));
            }
        }
    }
    return OK;
}
