// mounts.c: the mounts file, read into the mounts a run knows. A mount is a
// named folder outside the root, reached as `name:path`; the file says one a
// line, `name = /abs/path` or `name = ~/path`, then any of ` ro`, ` tools`
// and ` secrets`. A mistake names its line.
//
// Include file_system.c first (its error names and its file read).

// ` ro` after the path refuses every change through the mount; reads and
// programs are unaffected.
typedef enum {
    MOUNT_READ_WRITE,
    MOUNT_READ_ONLY,
} MountAccess;

typedef struct {
    // What precedes the `:`: letters, digits, `_` and `-`.
    Str name;
    // The folder, absolute, its trailing `/` trimmed; links in it resolved
    // once by `mounts_load`.
    Str dir;
    MountAccess access;
    // ` tools`: `run` takes a program under it by path.
    bool tools;
    // ` secrets`: a folder of tokens no script reads, lists or names; it is
    // read-only too.
    bool secrets;
} Mount;

VEC_DEFINE(Mounts, Mount)

// `p` without its trailing `/`s, but `/` itself kept: the filesystem's root
// trimmed to "" would be taken as the working folder.
static Str mounts_trim_slash(Str p) {
    size_t n = p.len;
    while (n > 0 && str_byte(p, n - 1) == '/') n--;
    if (n == 0 && p.len > 0) return S("/");
    return str_sub(p, 0, n);
}

// Whether `abs` is the folder `dir` or a path below it (`dir` absolute, `/`
// included).
// DRAGON: the file system's boundaries. Every mount's, the root's and the
// secrets folder's containment is this test, and a prefix alone would put
// `/tmp/xy` inside `/tmp/x`, so a read-only or secrets mount's guard would miss
// a sibling that only begins with its name, or claim one.
// REMEDY: the byte after the prefix must be `/` or the end; test_paths
// plants the prefix.
static bool mounts_in_folder(Str abs, Str dir) {
    if (str_eq(dir, S("/"))) return abs.len > 0 && str_byte(abs, 0) == '/';
    return str_starts_with(abs, dir) && (abs.len == dir.len || str_byte(abs, dir.len) == '/');
}

// Whether `text` holds a secrets mount's folder, as the mounts file names
// it, anywhere in it. A stop for a mount whose
// folder is missing prints the folder only when it does not.
static bool mounts_spell_secret_folder(MountsItems ms, Str text) {
    for (size_t k = 0; k < ms.len; k++) {
        Str dir = at(ms, k).dir;
        if (!at(ms, k).secrets || dir.len > text.len) continue;
        for (size_t i = 0; i + dir.len <= text.len; i++) {
            if (str_eq(str_sub(text, i, i + dir.len), dir)) return true;
        }
    }
    return false;
}

static bool mounts_valid_name(Str s) {
    for (size_t i = 0; i < s.len; i++) {
        uint8_t c = str_byte(s, i);
        bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!(alnum || c == '_' || c == '-')) return false;
    }
    return true;
}

static bool mounts_space(uint8_t c, bool cr) {
    return c == ' ' || c == '\t' || (cr && c == '\r');
}

// `s` trimmed of spaces and tabs (and `\r` when `cr`) at both ends.
static Str mounts_trim(Str s, bool cr) {
    size_t from = 0;
    size_t to = s.len;
    while (from < to && mounts_space(str_byte(s, from), cr)) from++;
    while (to > from && mounts_space(str_byte(s, to - 1), cr)) to--;
    return str_sub(s, from, to);
}

// The next word of `s` from `*from`, split on spaces and tabs; false
// when none is left.
static bool mounts_next_word(Str s, size_t *from, Str *word) {
    size_t i = *from;
    while (i < s.len && mounts_space(str_byte(s, i), false)) i++;
    if (i == s.len) return false;
    size_t start = i;
    while (i < s.len && !mounts_space(str_byte(s, i), false)) i++;
    *word = str_sub(s, start, i);
    *from = i;
    return true;
}

// What is said of the mount `m` whose folder could not be opened, `why` the
// error (FileNotFound or NotDir for a folder that is not there), into `*out`
// in `a`; false when the arena refuses. The one home of these words, for
// paths.c's stop on a mount a script uses and the CLI's library reader's.
// DRAGON: the secrets rule. The folder of a secrets mount, or a path that
// spells one, would print where the tokens are.
// REMEDY: the folder is named only when the mount is no secrets mount and
// its path spells none (mounts_spell_secret_folder).
static bool mounts_folder_failed(Arena *a, MountsItems ms, const Mount *m, FileSystemErr why, Str *out) {
    bool missing = why == FILE_SYSTEM_FILE_NOT_FOUND || why == FILE_SYSTEM_NOT_DIR;
    bool spells = mounts_spell_secret_folder(ms, m->dir);
    Buf b = buf_make(a);
    if (buf_lit(&b, "the mount `") != OK || buf_str(&b, m->name) != OK) return false;
    Status s = OK;
    if (m->secrets) {
        s = buf_str(&b, missing ? S("` is a secrets mount whose folder is missing") : S("` is a secrets mount whose folder cannot be opened: "));
    } else if (spells) {
        s = buf_str(&b, missing ? S("`'s folder is missing") : S("`'s folder cannot be opened: "));
    } else {
        s = buf_lit(&b, "` is ");
        if (s == OK) s = buf_str(&b, m->dir);
        if (s == OK) s = buf_str(&b, missing ? S(", which is not a folder there") : S(", which cannot be opened: "));
    }
    if (s == OK && !missing) s = buf_str(&b, file_system_err_name(why));
    if (s == OK && spells && !m->secrets) s = buf_lit(&b, "; its path holds a secrets mount's folder, so it is not named");
    if (s != OK) return false;
    *out = buf_view(&b);
    return true;
}

typedef enum {
    MOUNTS_OK,
    MOUNTS_MALFORMED,
    MOUNTS_OUT_OF_MEMORY,
} MountsResult;

// `*why`, the `line {d}: ...` text of `fmt`, made in `a`; MOUNTS_MALFORMED,
// or MOUNTS_OUT_OF_MEMORY when the arena refuses it.
static MountsResult mounts_malformed(Arena *a, Str *why, size_t n, Str fmt, MsgArgs args) {
    MsgNum num;
    Buf b = buf_make(a);
    if (buf_lit(&b, "line ") != OK || buf_str(&b, msg_num(&num, n)) != OK || buf_lit(&b, ": ") != OK || msg_format(&b, fmt, args) != OK) return MOUNTS_OUT_OF_MEMORY;
    *why = buf_view(&b);
    return MOUNTS_MALFORMED;
}

// Parses a mounts file's `text` into `*out`, `~/` taken from `home`: one
// `name = value` a line, `#` comments and blank lines skipped. On
// MOUNTS_MALFORMED, `*why` names the line and what is wrong with it, and the
// mount once its name is known to be one.
// DRAGON: the secrets rule. A message that quoted the line, its path or a
// word after the path could show a secrets mount's folder, and none of it
// passes `run_fail`'s check, since no run has started.
// REMEDY: every message here names a line number, a keyword or a checked
// name alone.
static MountsResult mounts_parse(Arena *a, Str text, Str home, Mounts *out, Str *why) {
    *out = Mounts_make(a);
    // The two messages that name no mount take no arguments.
    MsgArgs none = {NULL, 0};
    size_t n = 0;
    size_t from = 0;
    while (from <= text.len) {
        size_t to = path_part_end_at(text, from, '\n');
        Str raw = str_sub(text, from, to);
        from = to + 1;
        n++;
        Str line = mounts_trim(raw, true);
        if (line.len == 0 || str_byte(line, 0) == '#') continue;
        const char *eq = str_find_byte(line, '=');
        if (eq == NULL) return mounts_malformed(a, why, n, S("the line is not `name = value`"), none);
        size_t eq_at = (size_t)(eq - line.ptr);
        Str name = mounts_trim(str_sub(line, 0, eq_at), false);
        // The name is checked first, so every message after this one may
        // name the mount: what is not a name may be a path.
        if (name.len == 0 || !mounts_valid_name(name)) return mounts_malformed(a, why, n, S("the name before `=` is not a mount's name: letters, digits, `_` and `-`"), none);
        Str words = str_sub(line, eq_at + 1, line.len);
        size_t w_at = 0;
        Str value = S("");
        if (!mounts_next_word(words, &w_at, &value)) value = S("");
        bool said_ro = false;
        bool tools = false;
        bool secrets = false;
        Str w = S("");
        while (mounts_next_word(words, &w_at, &w)) {
            bool seen = false;
            if (str_eq(w, S("ro"))) {
                seen = said_ro;
            } else if (str_eq(w, S("tools"))) {
                seen = tools;
            } else if (str_eq(w, S("secrets"))) {
                seen = secrets;
            } else {
                return mounts_malformed(a, why, n, S("only `ro`, `tools` and `secrets` may follow `{s}`'s path (and a path has no space)"), MSG_ARGS(MSG_FROM_HOST(name)));
            }
            if (seen) return mounts_malformed(a, why, n, S("`{s}` twice after `{s}`'s path"), MSG_ARGS(MSG_FROM_HOST(w), MSG_FROM_HOST(name)));
            if (str_eq(w, S("ro"))) {
                said_ro = true;
            } else if (str_eq(w, S("tools"))) {
                tools = true;
            } else {
                secrets = true;
            }
        }
        if (secrets && tools) return mounts_malformed(a, why, n, S("`{s}` is `secrets` and `tools`: a toolchain's programs are not tokens"), MSG_ARGS(MSG_FROM_HOST(name)));
        for (size_t k = 0; k < out->len; k++) {
            if (str_eq(at(*out, k).name, name)) return mounts_malformed(a, why, n, S("the mount `{s}` is named twice"), MSG_ARGS(MSG_FROM_HOST(name)));
        }
        // A NUL is refused here, where its line is known: past the parse
        // the folder is a path the kernel takes (paths.c, shale.c).
        OsStr checked;
        if (!os_str_of(value, &checked)) return mounts_malformed(a, why, n, S("`{s}`'s path holds a NUL byte, which no path can carry"), MSG_ARGS(MSG_FROM_HOST(name)));
        Str dir = S("");
        if (str_starts_with(value, S("~/"))) {
            Buf b = buf_make(a);
            if (buf_str(&b, home) != OK || buf_byte(&b, '/') != OK || buf_str(&b, str_sub(value, 2, value.len)) != OK) return MOUNTS_OUT_OF_MEMORY;
            dir = buf_view(&b);
        } else if (value.len > 0 && str_byte(value, 0) == '/') {
            if (!arena_dup(a, value, &dir)) return MOUNTS_OUT_OF_MEMORY;
        } else {
            return mounts_malformed(a, why, n, S("`{s}`'s value is relative: an absolute path, or one from `~/`"), MSG_ARGS(MSG_FROM_HOST(name)));
        }
        Mount m = {S(""), mounts_trim_slash(dir), said_ro || secrets ? MOUNT_READ_ONLY : MOUNT_READ_WRITE, tools, secrets};
        if (!arena_dup(a, name, &m.name) || Mounts_push(out, m) != OK) return MOUNTS_OUT_OF_MEMORY;
    }
    return MOUNTS_OK;
}

// The mounts, and the file they came from (a stop that names them says it).
typedef struct {
    Mounts mounts;
    Str from;
} MountsLoaded;

// Loads the mounts from `pinned` (`SHALE_MOUNTS`, when `has_pinned`) or else
// `~/.config/shale/mounts`. A missing default file is no mounts; a missing
// pinned one, or any mistake in a file, is MOUNTS_MALFORMED with `*why`
// naming the file. A link in a mount's own path is the user's choice, so it
// is resolved once here; below it, the file system follows none.
static inline MountsResult mounts_load(Arena *a, bool has_pinned, Str pinned, Str home, MountsLoaded *out, Str *why) {
    out->mounts = Mounts_make(a);
    out->from = pinned;
    if (!has_pinned) {
        Buf b = buf_make(a);
        if (buf_str(&b, home) != OK || buf_lit(&b, "/.config/shale/mounts") != OK) return MOUNTS_OUT_OF_MEMORY;
        out->from = buf_view(&b);
    }
    Str text = S("");
    FileSystemErr e = file_system_read_file(a, os_str_own(out->from), 64 * 1024, &text);
    if (e == FILE_SYSTEM_OUT_OF_MEMORY) return MOUNTS_OUT_OF_MEMORY;
    if (e != FILE_SYSTEM_OK) {
        if (!has_pinned && e == FILE_SYSTEM_FILE_NOT_FOUND) return MOUNTS_OK;
        Buf b = buf_make(a);
        if ((has_pinned && buf_lit(&b, "SHALE_MOUNTS=") != OK) || buf_str(&b, out->from) != OK || buf_lit(&b, ": ") != OK || buf_str(&b, file_system_err_name(e)) != OK) return MOUNTS_OUT_OF_MEMORY;
        *why = buf_view(&b);
        return MOUNTS_MALFORMED;
    }
    Str line_why = S("");
    MountsResult r = mounts_parse(a, text, home, &out->mounts, &line_why);
    if (r == MOUNTS_MALFORMED) {
        Buf b = buf_make(a);
        if (buf_str(&b, out->from) != OK || buf_lit(&b, ": ") != OK || buf_str(&b, line_why) != OK) return MOUNTS_OUT_OF_MEMORY;
        *why = buf_view(&b);
    }
    if (r != MOUNTS_OK) return r;
    for (size_t k = 0; k < out->mounts.len; k++) {
        Mount *m = &at(out->mounts, k);
        FileSystemCPath cp;
        const char *c = NULL;
        FileSystemErr ce = file_system_cstr(&cp, os_str_own(m->dir), &c);
        char resolved[PATH_MAX];
        int err = ENAMETOOLONG;
        if (ce == FILE_SYSTEM_OK) err = realpath(c, resolved) != NULL ? 0 : errno;
        // A folder that is not there keeps its spelling: a run that uses
        // the mount stops on it then (paths.c's paths_mount_path).
        if (err == ENOENT || err == ENOTDIR) continue; // skip: missing is the mount's own stop
        if (err != 0) {
            // The folder is not named: it may be a secrets mount's.
            Buf b = buf_make(a);
            if (buf_str(&b, out->from) != OK || buf_lit(&b, ": the mount `") != OK || buf_str(&b, m->name) != OK || buf_lit(&b, "`'s folder cannot be resolved: ") != OK || buf_str(&b, file_system_err_name(file_system_err_of(err))) != OK) return MOUNTS_OUT_OF_MEMORY;
            *why = buf_view(&b);
            return MOUNTS_MALFORMED;
        }
        if (!arena_dup(a, (Str){resolved, strlen(resolved)}, &m->dir)) return MOUNTS_OUT_OF_MEMORY;
    }
    return MOUNTS_OK;
}
