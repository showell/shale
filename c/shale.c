// shale.c: the C `shale` command. Today it is Zig Shale's `--check` alone
// (src/script_runner.zig): it reads the command line, the mounts, the script
// and the preludes as the Zig runner does, parses the script with them, and
// prints the same report with the same exit code. Every other command line is
// refused, naming what is not yet ported. This file holds the system calls;
// what it decides is in script/cli.h and script/parse.h.

// `realpath` is X/Open's.
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "script/cli.h"
#include "preludes.h"

enum {
    // Exit codes, as the Zig runner's: finished, stopped, could not begin.
    EXIT_FINISHED = 0,
    EXIT_STOPPED = 1,
    EXIT_REFUSED = 2,
    // Zig's `loadMounts` reads at most this much of the mounts file.
    MOUNTS_MOST = 64 * 1024,
    // Room for the parse beyond the machine budget: the C's tree is laid out
    // for itself, and its working lists stay in the arena, so the budget's own
    // bytes are no measure of it. DRAGON: the machine budget. The parse's
    // arena is bounded, but not at Zig's `budget machine` stop: a source that
    // stops there in Zig parses here. The source budget (64 KiB) bounds the
    // tree, so the arena can't grow without end.
    PARSE_ROOM = 256 * 1024 * 1024,
};

typedef struct {
    Arena *arena;
    Buf err;
} Out;

// Zig's name for a failed open or read, as its messages print `@errorName`.
static Str zig_error_name(int e) {
    switch (e) {
    case ENOENT: return S("FileNotFound");
    case EISDIR: return S("IsDir");
    case ENOTDIR: return S("NotDir");
    case EACCES: return S("AccessDenied");
    case EPERM: return S("PermissionDenied");
    case ELOOP: return S("SymLinkLoop");
    case ENAMETOOLONG: return S("NameTooLong");
    default: return S("Unexpected");
    }
}

// `s` as a NUL-ended copy, for a system call.
static const char *c_string(Arena *a, Str s) { // cstr: the system's own
    if (s.len == SIZE_MAX) return NULL;
    char *p = arena_alloc(a, s.len + 1, 1);
    if (p == NULL) return NULL;
    Bytes room = {p, s.len + 1};
    if (s.len > 0) memcpy(room.ptr, s.ptr, s.len);
    at(room, s.len) = 0;
    return p;
}

static Str from_c(const char *s) { // cstr: the system's own
    Str r = {s, strlen(s)};
    return r;
}

// Reads all of `fd` into the arena, at most `limit` bytes: Zig's
// `.limited(limit)`, whose refusal of more is StreamTooLong.
static STATUS_MUST_USE Status read_all(Arena *a, int fd, size_t limit, Str *out, Str *err) {
    Buf b = buf_make(a);
    char chunk[16384];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            *err = zig_error_name(errno);
            return STOP;
        }
        if (n == 0) break;
        if (b.len + (size_t)n > limit) {
            *err = S("StreamTooLong");
            return STOP;
        }
        Str got = {chunk, (size_t)n};
        if (buf_str(&b, got) != OK) {
            *err = S("OutOfMemory");
            return STOP;
        }
    }
    *out = buf_view(&b);
    return OK;
}

static STATUS_MUST_USE Status read_path(Arena *a, Str path, size_t limit, Str *out, Str *err) {
    const char *p = c_string(a, path);
    if (p == NULL) {
        *err = S("OutOfMemory");
        return STOP;
    }
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        *err = zig_error_name(errno);
        return STOP;
    }
    Status s = read_all(a, fd, limit, out, err);
    close(fd);
    return s;
}

// The real path of `p`, or `p` itself when it has none (it is missing).
static Str real_or_same(Arena *a, Str p) {
    const char *c = c_string(a, p);
    if (c == NULL) return p;
    char buf[PATH_MAX];
    if (realpath(c, buf) == NULL) return p;
    Str r = {0};
    if (!arena_dup(a, from_c(buf), &r)) return p;
    return r;
}

static STATUS_MUST_USE Status join(Arena *a, Str x, Str y, Str *out) {
    Buf b = buf_make(a);
    TRY(buf_str(&b, x));
    if (!(x.len > 0 && str_byte(x, x.len - 1) == '/')) TRY(buf_byte(&b, '/'));
    TRY(buf_str(&b, y));
    *out = buf_view(&b);
    return OK;
}

static void say(Out *o, Str s) {
    if (buf_str(&o->err, s) != OK) o->err.len = 0;
}

static int flush_and_exit(Out *o, int code) {
    if (o->err.len > 0) {
        size_t n = fwrite(o->err.ptr, 1, o->err.len, stderr);
        (void)n;
    }
    fflush(stderr);
    return code;
}

// A command line the Zig runner refuses: `shale: <why>`, then the usage.
static int refuse_command(Out *o, Str why, bool usage) {
    say(o, S("shale: "));
    say(o, why);
    say(o, S("\n"));
    if (usage) say(o, cli_usage());
    return flush_and_exit(o, EXIT_REFUSED);
}

// What this C `shale` does not do yet, said as a refusal of its own.
static int not_ported(Out *o, Str what) {
    say(o, S("shale (C): "));
    say(o, what);
    say(o, S(" is not ported yet: only `--check` is (shale --check FILE)\n"));
    return flush_and_exit(o, EXIT_REFUSED);
}

// ── `use`'s reader ──

typedef struct {
    Arena *arena;
    MountVec mounts;
    Str mounts_from;
    Budgets budgets;
    Used used;
} LibraryReader;

// The stop a reader's failure becomes: `why`, the message Zig's run would
// have stopped with, which its loader takes and clears.
static STATUS_MUST_USE Status reader_why_parts(Str *why, bool *got, Arena *a, StrPieces parts, size_t n) {
    StrParts all = {parts.piece, n};
    *got = false;
    Buf b = buf_make(a);
    for (size_t k = 0; k < all.len; k++) TRY(buf_str(&b, at(all, k)));
    *why = buf_view(&b);
    return OK;
}

#define reader_why(why, got, n, a, ...) reader_why_parts((why), (got), (a), (StrPieces){{__VA_ARGS__}}, (n))

// Zig's `inFolder`: whether `abs` is the folder `dir` or a path below it.
static bool in_folder(Str abs, Str dir) {
    if (str_eq(dir, S("/"))) return abs.len > 0 && str_byte(abs, 0) == '/';
    return str_starts_with(abs, dir) && (abs.len == dir.len || str_byte(abs, dir.len) == '/');
}

// A charge past the step budget: Zig's `charge` leaves the count one past it,
// and the budget's stop says how many steps it had.
static STATUS_MUST_USE Status steps_ran_out(LibraryReader *r, Str *why) {
    r->used.steps = r->budgets.steps + 1;
    Buf b = buf_make(r->arena);
    TRY(buf_lit(&b, "the step budget ("));
    TRY(buf_u64(&b, r->budgets.steps));
    TRY(buf_lit(&b, " steps) ran out"));
    *why = buf_view(&b);
    return OK;
}

// Zig's `refuseLinks` (src/shale_host_linux.zig): whether any part of `rest`
// below the mount's folder `dir` is a symbolic link, each part checked with
// `lstat` and charged a step, a missing part ending the walk. On a link,
// `*why` names it as Zig's stop does. DRAGON: the store's boundary. The store
// follows no link below a mount's folder, so a link there could lead a
// library's read out of it. This walk refuses one before the read; a link
// made between the walk and the open is a race the Zig shares.
static STATUS_MUST_USE Status refuse_links(LibraryReader *r, Str dir, Str rest, Str prefix, Str *why, bool *refused) {
    *refused = false;
    if (rest.len == 0) return OK;
    Arena *a = r->arena;
    Str sofar = dir;
    Str left = rest;
    bool done = false;
    Str part;
    size_t shown_end = 0;
    while (cli_split(&left, &done, '/', &part)) {
        if (r->used.steps >= r->budgets.steps) {
            TRY(steps_ran_out(r, why));
            *refused = true;
            return OK;
        }
        r->used.steps++;
        TRY(join(a, sofar, part, &sofar));
        shown_end += part.len + (shown_end > 0 ? 1 : 0);
        const char *c = c_string(a, sofar);
        if (c == NULL) return STOP;
        struct stat st;
        if (lstat(c, &st) != 0) return OK;
        if (S_ISLNK(st.st_mode)) {
            bool got;
            TRY(reader_why(why, &got, 3, a, prefix, str_sub(rest, 0, shown_end), S(" is a symbolic link: the store follows none")));
            *refused = true;
            return OK;
        }
    }
    return OK;
}

// Zig's `-|`: what is left of a budget, never wrapping below zero once a
// charge has stopped one past it.
static uint64_t sat_sub(uint64_t a, uint64_t b) {
    return a > b ? a - b : 0;
}

// Zig's `loadLibrary` (src/script/process.zig) over the Linux host's mounts:
// the path resolved as `pathArg` resolves a mount's, the file read and charged
// as a `read` of it is.
static STATUS_MUST_USE Status load_library(void *ctx, Arena *into, Str path, Str *why, Str *out, bool *got) {
    LibraryReader *r = ctx;
    Arena *a = r->arena;
    *got = false;
    // `pathArg` charges a step a byte of the path.
    if (path.len > sat_sub(r->budgets.steps, r->used.steps)) {
        TRY(steps_ran_out(r, why));
        return OK;
    }
    r->used.steps += path.len;
    size_t colon = cli_index_of(path, ':');
    Str name = str_sub(path, 0, colon);
    Mount *m = NULL;
    for (size_t k = 0; k < r->mounts.len; k++) {
        if (str_eq(at(r->mounts, k).name, name)) m = &at(r->mounts, k);
    }
    if (m == NULL) {
        Buf names = buf_make(a);
        for (size_t k = 0; k < r->mounts.len; k++) {
            if (k > 0 && buf_lit(&names, ", ") != OK) return STOP;
            if (buf_byte(&names, '`') != OK || buf_str(&names, at(r->mounts, k).name) != OK || buf_byte(&names, '`') != OK) return STOP;
        }
        if (r->mounts.len == 0) return reader_why(why, got, 7, a, S("\""), path, S("\": `"), name, S(":` is not a mount: there are none ("), r->mounts_from, S(")"));
        return reader_why(why, got, 9, a, S("\""), path, S("\": `"), name, S(":` is not a mount: the mounts are "), buf_view(&names), S(" ("), r->mounts_from, S(")"));
    }
    if (m->secrets) return reader_why(why, got, 5, a, S("\""), path, S("\": the mount `"), m->name, S("` holds secrets: only `run`'s `secrets` (or `http_get`'s `bearer`) may name its files"));
    Str rest = str_sub(path, colon + 1, path.len);
    if (str_eq(rest, S("."))) rest = S("");
    if (rest.len > 0 && str_byte(rest, 0) == '/') return reader_why(why, got, 3, a, S("\""), path, S("\" is absolute inside its mount: a mount's paths are relative to it"));
    Str left = rest;
    bool done = rest.len == 0;
    Str part = {0};
    while (cli_split(&left, &done, '/', &part)) {
        if (str_eq(part, S(".."))) return reader_why(why, got, 3, a, S("\""), path, S("\" climbs out of its mount with `..`: a folder beside it is reached through a mount of its own (`shale --help` lists this box's mounts)"));
        if (!cli_fat_name(part)) return reader_why(why, got, 5, a, S("\""), path, S("\" has a part (\""), part, S("\") that is not a name: no `.`, `..`, empty parts or characters FAT refuses"));
    }
    const char *dir_c = c_string(a, m->dir);
    struct stat dst;
    if (dir_c == NULL) return STOP;
    if (stat(dir_c, &dst) != 0 || !S_ISDIR(dst.st_mode)) return reader_why(why, got, 5, a, S("the mount `"), m->name, S("` is "), m->dir, S(", which is not a folder there"));
    bool linked = false;
    TRY(refuse_links(r, m->dir, rest, str_sub(path, 0, colon + 1), why, &linked));
    if (linked) return OK;
    Str full = m->dir;
    if (rest.len > 0) TRY(join(a, m->dir, rest, &full));
    // DRAGON: the secrets rule. A secrets mount's folder may sit inside another
    // mount's, and a library read through the outer one would put a token's
    // bytes in a parse's messages (a name, a number). Zig's
    // `refuseSecretsUnder` refuses every path in such a folder; so does this.
    for (size_t k = 0; k < r->mounts.len; k++) {
        const Mount *sm = &at(r->mounts, k);
        if (sm->secrets && in_folder(full, sm->dir)) return reader_why(why, got, 5, a, S("\""), path, S("\" is in the mount `"), sm->name, S("`, which holds secrets"));
    }
    // `countRead`: the read's count first, then the file's size.
    if (r->used.reads >= r->budgets.reads) {
        *why = S("the store-read budget (count) ran out");
        return OK;
    }
    r->used.reads++;
    const char *full_c = c_string(a, full);
    struct stat st;
    if (full_c == NULL) return STOP;
    if (stat(full_c, &st) != 0) {
        if (errno == ENOENT) return reader_why(why, got, 3, a, S("`read(\""), path, S("\")`: not found (FileNotFound): `exists(p)` asks first"));
        return reader_why(why, got, 5, a, S("`read(\""), path, S("\")`: the store failed ("), zig_error_name(errno), S(")"));
    }
    if (!S_ISREG(st.st_mode)) return reader_why(why, got, 3, a, S("`read(\""), path, S("\")`: a folder, not a file (IsDir): `is_dir(p)` asks first"));
    uint64_t size = (uint64_t)st.st_size;
    if (size > r->budgets.source_bytes) {
        *why = S("the library is larger than the source budget");
        return OK;
    }
    if (size > sat_sub(r->budgets.read_bytes, r->used.read_bytes)) {
        *why = S("the store-read budget (bytes) ran out");
        return OK;
    }
    Str text = {0};
    Str err = {0};
    if (read_path(into, full, (size_t)size, &text, &err) != OK) {
        if (str_eq(err, S("OutOfMemory"))) return STOP;
        return reader_why(why, got, 5, a, S("`read(\""), path, S("\")`: the store failed ("), err, S(")"));
    }
    r->used.read_bytes += text.len;
    // `chargeBytes`: a step for every 16 bytes read, rounded up.
    uint64_t charge = (text.len + 15) / 16;
    if (charge > sat_sub(r->budgets.steps, r->used.steps)) {
        TRY(steps_ran_out(r, why));
        return OK;
    }
    r->used.steps += charge;
    *out = text;
    *got = true;
    return OK;
}

// ── the check ──

// The OS path of a script named through a mount (`cookbook:look.shale`); any
// other name as given. `*missing` is set for a `name:` that looks like a
// mount's and is none.
static STATUS_MUST_USE Status script_file(Arena *a, Str given, MountVec mounts, Str *out, bool *missing) {
    *missing = false;
    *out = given;
    size_t colon = cli_index_of(given, ':');
    if (colon == SIZE_MAX) return OK;
    Str name = str_sub(given, 0, colon);
    if (name.len == 0 || cli_index_of(name, '/') != SIZE_MAX) return OK;
    for (size_t k = 0; k < mounts.len; k++) {
        if (str_eq(at(mounts, k).name, name)) {
            Str rest = str_sub(given, colon + 1, given.len);
            if (rest.len == 0) {
                *out = at(mounts, k).dir;
                return OK;
            }
            return join(a, at(mounts, k).dir, rest, out);
        }
    }
    *missing = true;
    return OK;
}

static int check(Out *o, Command *c, MountVec mounts, Str mounts_from, Str source, Preludes preludes, const Arg *args, size_t args_len) {
    Arena *a = o->arena;
    Budgets b = c->budgets;
    Str name = cli_script_name(c);
    Used none = {0, 0, 0};
    if (source.len > b.source_bytes) {
        Stop s = {0};
        memset(&s, 0, sizeof s);
        s.line = 1;
        s.column = 1;
        s.cls = CLASS_BUDGET;
        s.budget = BUDGET_SOURCE;
        s.message = S("the source is larger than the source budget");
        s.function = S("");
        if (cli_write_stop(&o->err, name, &s, none, &b) != OK) return not_ported(o, S("a report past the arena"));
        return flush_and_exit(o, EXIT_STOPPED);
    }
    Arena tree = arena_make(b.machine_bytes + PARSE_ROOM);
    Parser p;
    parse_init(&p, &tree, source, b.nesting);
    LibraryReader reader = {a, mounts, mounts_from, b, none};
    Loader loader = {&reader, load_library};
    Program prog = {0};
    Status s = parse_program(&p, preludes_core(), preludes, loader, &prog);
    Stop stop = {0};
    bool stopped = false;
    if (s != OK) {
        if (!p.stopped) {
            // Zig's machine stop, the one Process.ready gives an OutOfMemory.
            memset(&stop, 0, sizeof stop);
            stop.line = 1;
            stop.column = 1;
            stop.cls = CLASS_BUDGET;
            stop.budget = BUDGET_MACHINE;
            stop.message = S("the machine's memory (the code, the frames, the operands) is past its budget: raise it with `--budget machine_bytes=N` or the script's `-- budget: machine_bytes=N` line");
            stop.function = S("");
        } else {
            stop = p.stop;
        }
        stopped = true;
    } else if (cli_unread_arg(&tree, &prog, args, args_len, &stop, &stopped) != OK) {
        return not_ported(o, S("a check past the arena"));
    }
    if (stopped) {
        if (cli_write_stop(&o->err, name, &stop, reader.used, &b) != OK) return not_ported(o, S("a report past the arena"));
        int code = flush_and_exit(o, EXIT_STOPPED);
        arena_deinit(&tree);
        return code;
    }
    arena_deinit(&tree);
    say(o, S("── checked: it parses and compiles; nothing ran\n"));
    return flush_and_exit(o, EXIT_FINISHED);
}

// The preludes after the core's, as Zig's `loadPreludes`: the Linux and git
// preludes, the repo's `.shale/prelude.shale` if present, and the user's.
static int load_preludes(Out *o, Str root_abs, Prelude *out, size_t *n) {
    Arena *a = o->arena;
    Prelude linux_prelude = {S("the Linux prelude"), preludes_linux()};
    Prelude git_prelude = {S("the git prelude"), preludes_git()};
    SLICE_TYPE(PreludeSlots, Prelude);
    PreludeSlots slots = {out, 4};
    at(slots, 0) = linux_prelude;
    at(slots, 1) = git_prelude;
    *n = 2;
    Str repo = {0};
    if (join(a, root_abs, S(".shale/prelude.shale"), &repo) != OK) return -1;
    Str text = {0};
    Str err = {0};
    if (read_path(a, repo, 64 * 1024, &text, &err) == OK) {
        Prelude r = {S(".shale/prelude.shale"), text};
        at(slots, *n) = r;
        (*n)++;
    } else if (!str_eq(err, S("FileNotFound"))) {
        say(o, S("shale: the preludes: "));
        say(o, repo);
        say(o, S(": "));
        say(o, err);
        say(o, S("\n"));
        return flush_and_exit(o, EXIT_REFUSED);
    }
    const char *pinned = getenv("SHALE_PRELUDE"); // cstr: the environment's
    Str user = {0};
    Str shown = {0};
    bool required = false;
    if (pinned != NULL) {
        if (strlen(pinned) == 0) return -1;
        user = from_c(pinned);
        shown = user;
        required = true;
    } else {
        const char *home = getenv("HOME"); // cstr: the environment's
        if (join(a, from_c(home != NULL ? home : "/"), S(".config/shale/prelude.shale"), &user) != OK) return -1;
        shown = S("~/.config/shale/prelude.shale");
    }
    if (read_path(a, user, 64 * 1024, &text, &err) == OK) {
        Prelude u = {shown, text};
        at(slots, *n) = u;
        (*n)++;
    } else if (required || !str_eq(err, S("FileNotFound"))) {
        say(o, S("shale: the preludes: "));
        if (required) say(o, S("SHALE_PRELUDE="));
        say(o, user);
        say(o, S(": "));
        say(o, err);
        say(o, S("\n"));
        return flush_and_exit(o, EXIT_REFUSED);
    }
    return -1;
}

static int run_main(Out *o, int argc, char **argv) {
    Arena *a = o->arena;
    SLICE_TYPE(Args, char *);
    Args raw = {argv, (size_t)argc};
    Str *words = ARENA_ARRAY(a, Str, raw.len);
    if (words == NULL) return not_ported(o, S("a command line past the arena"));
    SLICE_TYPE(Words, Str);
    Words all = {words, raw.len};
    for (size_t i = 0; i < raw.len; i++) at(all, i) = from_c(at(raw, i));
    if (all.len > 1 && str_eq(at(all, 1), S("--reap"))) return not_ported(o, S("`shale --reap`, a job's reaper,"));
    if (all.len == 2 && (str_eq(at(all, 1), S("--help")) || str_eq(at(all, 1), S("-h")))) return not_ported(o, S("`shale --help`"));
    if (all.len == 2 && str_eq(at(all, 1), S("--version"))) return not_ported(o, S("`shale --version`"));
    const char *env_root = getenv("SHALE_ROOT"); // cstr: the environment's
    Command c;
    Buf why = buf_make(a);
    Words rest = all.len > 0 ? SLICE_SUB(Words, all, 1, all.len) : all;
    if (cli_parse(rest.ptr, rest.len, env_root != NULL, env_root != NULL ? from_c(env_root) : S(""), &c, &why) != OK) return refuse_command(o, buf_view(&why), true);
    if (!c.check) return not_ported(o, S("a run (any command line without `--check`)"));
    Arg more[CLI_ARGS_MOST];
    size_t more_len = 0;
    const Arg *args = c.args;
    size_t args_len = c.args_len;
    if (c.args_stdin) {
        Str text = {0};
        Str err = {0};
        if (read_all(a, 0, 1 << 20, &text, &err) != OK) {
            say(o, S("shale: cannot read the values on stdin: "));
            say(o, err);
            say(o, S("\n"));
            return flush_and_exit(o, EXIT_REFUSED);
        }
        buf_clear(&why);
        if (cli_arg_lines(text, &c, more, &more_len, &why) != OK) return refuse_command(o, buf_view(&why), true);
        args = more;
        args_len = more_len;
    }
    const char *now = getenv("SHALE_NOW"); // cstr: the environment's
    int64_t pinned_now = 0;
    if (now != NULL && !cli_parse_integer(from_c(now), &pinned_now)) {
        say(o, S("shale: SHALE_NOW="));
        say(o, from_c(now));
        say(o, S(" is not an integer\n"));
        return flush_and_exit(o, EXIT_REFUSED);
    }
    const char *home_c = getenv("HOME"); // cstr: the environment's
    Str home = from_c(home_c != NULL ? home_c : "/");
    const char *pinned_mounts = getenv("SHALE_MOUNTS"); // cstr: the environment's
    Str from = {0};
    if (pinned_mounts != NULL) {
        from = from_c(pinned_mounts);
    } else if (join(a, home, S(".config/shale/mounts"), &from) != OK) {
        return not_ported(o, S("a mounts path past the arena"));
    }
    MountVec mounts = MountVec_make(a);
    Str mounts_text = {0};
    Str err = {0};
    if (read_path(a, from, MOUNTS_MOST, &mounts_text, &err) != OK) {
        if (pinned_mounts != NULL || !str_eq(err, S("FileNotFound"))) {
            say(o, S("shale: the mounts: "));
            if (pinned_mounts != NULL) say(o, S("SHALE_MOUNTS="));
            say(o, from);
            say(o, S(": "));
            say(o, err);
            say(o, S("\n"));
            return flush_and_exit(o, EXIT_REFUSED);
        }
    } else {
        buf_clear(&why);
        if (cli_parse_mounts(a, mounts_text, home, &mounts, &why) != OK) {
            say(o, S("shale: the mounts: "));
            say(o, from);
            say(o, S(": "));
            say(o, buf_view(&why));
            say(o, S("\n"));
            return flush_and_exit(o, EXIT_REFUSED);
        }
        for (size_t k = 0; k < mounts.len; k++) at(mounts, k).dir = real_or_same(a, at(mounts, k).dir);
    }
    Str source = {0};
    if (c.has_source) {
        source = c.source;
    } else if (c.has_file) {
        Str file = {0};
        bool missing = 0;
        if (script_file(a, c.file, mounts, &file, &missing) != OK) return not_ported(o, S("a script path past the arena"));
        if (missing) {
            say(o, S("shale: "));
            say(o, c.file);
            say(o, S(": no mount named `"));
            say(o, str_sub(c.file, 0, cli_index_of(c.file, ':')));
            say(o, S("` (the mounts: "));
            say(o, from);
            say(o, S(")\n"));
            return flush_and_exit(o, EXIT_REFUSED);
        }
        if (read_path(a, file, c.budgets.source_bytes + 1, &source, &err) != OK) {
            say(o, S("shale: cannot read "));
            say(o, c.file);
            say(o, S(": "));
            say(o, err);
            say(o, S("\n"));
            return flush_and_exit(o, EXIT_REFUSED);
        }
    } else {
        // A terminal on stdin with no script is a mistake, not a script.
        if (isatty(0)) return refuse_command(o, S("no script: a file, -e SOURCE, or the script on stdin"), true);
        if (read_all(a, 0, c.budgets.source_bytes + 1, &source, &err) != OK) {
            say(o, S("shale: cannot read the script on stdin: "));
            say(o, err);
            say(o, S("\n"));
            return flush_and_exit(o, EXIT_REFUSED);
        }
    }
    buf_clear(&why);
    if (cli_header_budgets(source, &c, &why) != OK) return refuse_command(o, buf_view(&why), false);
    if (cli_write_header_budgets(&o->err, &c) != OK) return not_ported(o, S("a report past the arena"));
    // The root in one spelling, its real path, as the mounts' folders are.
    Str given_root = c.root;
    while (given_root.len > 0 && str_byte(given_root, given_root.len - 1) == '/') given_root = str_sub(given_root, 0, given_root.len - 1);
    Str root = given_root.len == 0 ? given_root : real_or_same(a, given_root);
    Str root_abs = root;
    if (root.len == 0) {
        root_abs = S("/");
    } else if (str_byte(root, 0) != '/') {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof cwd) == NULL || join(a, from_c(cwd), root, &root_abs) != OK) return not_ported(o, S("a root with no working folder"));
    }
    Prelude preludes[4];
    size_t n_preludes = 0;
    int code = load_preludes(o, root_abs, preludes, &n_preludes);
    if (code >= 0) return code;
    Preludes ps = {preludes, n_preludes};
    return check(o, &c, mounts, from, source, ps, args, args_len);
}

int main(int argc, char **argv) {
    Arena a = arena_make((size_t)1 << 30);
    Out o = {&a, buf_make(&a)};
    int code = run_main(&o, argc, argv);
    arena_deinit(&a);
    return code;
}
