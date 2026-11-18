// shale.c: the C `shale` command: it reads the command line, the mounts,
// the script and the preludes, parses and compiles the script with them,
// and either stops there (`--check`) or runs it on the VM (script/vm.c),
// printing what it printed on stdout and its report on stderr, with its exit
// code. A host call the C does not run yet is refused as not ported.
// `--help` and `--version` print the usage and the build's mode, and
// `--reap` is a job's reaper (host/jobs.c). This file holds the system
// calls; what it decides is in script/.

// `realpath`, `openat` and `memmem` are GNU's.
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "script/run.h"
#include "host/store.c"
#include "host/path_run.c"
#include "script/regex.c"
#include "script/builtins.c"
#include "script/builtins_text.c"
#include "script/builtins_store.c"
#include "script/builtins_walk.c"
#include "script/builtins_change.c"
#include "script/vm.c"
#include "host/mounts.c"
#include "host/paths.c"
#include "host/undo.c"
#include "host/spawn_argv.c"
#include "host/spawn.c"
#include "host/keep.c"
#include "host/swap_file.c"
#include "host/job_record.c"
#include "host/jobs.c"
#include "host/host_run.c"
#include "host/host_jobs.c"
#include "host/host_procs.c"
#include "host/host_json.c"
#include "host/host_text.c"
#include "host/flat_tree.c"
#include "host/zig_wire.c"
#include "host/zig_ast.c"
#include "host/host_zig.c"
#include "host/host_call.c"
#include "preludes.h"

enum {
    // Exit codes: finished, stopped, could not begin.
    EXIT_FINISHED = 0,
    EXIT_STOPPED = 1,
    EXIT_REFUSED = 2,
    // Room for the parse beyond the machine budget: the C's tree is laid out
    // for itself, and its working lists stay in the arena, so the budget's own
    // bytes are no measure of it.
    // DRAGON: the machine budget. The parse's arena is bounded, but not at
    // Zig's `budget machine` stop: a source that stops there in Zig parses
    // here.
    // REMEDY: the source budget (64 KiB) bounds the tree, so the arena can't
    // grow without end.
    PARSE_ROOM = 256 * 1024 * 1024,
};

// The CLI's own arena, what it says on stderr, and the refusal its arena and
// the parse's set: the CLI's setup is outside any run, and a run's arenas set
// the run's own (`RunCtx.refused`).
typedef struct {
    Arena *arena;
    Buf err;
    bool *refused;
} Out;

// The name of a failed open or read, as the messages print it.
static Str error_name(int e) {
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
    // A NUL would cut the path short, so a path holding one is refused.
    if (s.len > 0 && mem_find_byte(s.ptr, 0, s.len) != NULL) return NULL;
    char *p = arena_alloc(a, s.len + 1, 1);
    if (p == NULL) return NULL;
    Bytes room = {p, s.len + 1};
    if (s.len > 0) mem_copy(room.ptr, s.ptr, s.len);
    at(room, s.len) = 0;
    return p;
}

static Str from_c(const char *s) { // cstr: the system's own
    Str r = {s, strlen(s)};
    return r;
}

// Reads all of `fd` into the arena, at most `limit` bytes; more is refused
// as StreamTooLong.
static STATUS_MUST_USE Status read_all(Arena *a, int fd, size_t limit, Str *out, Str *err) {
    Buf b = buf_make(a);
    char chunk[16384];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            *err = error_name(errno);
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
        *err = error_name(errno);
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

// Writes what is said so far to stderr, and empties it.
static void flush_err(Out *o) {
    if (o->err.len > 0) {
        size_t n = fwrite(o->err.ptr, 1, o->err.len, stderr);
        (void)n;
    }
    fflush(stderr);
    buf_clear(&o->err);
}

static int flush_and_exit(Out *o, int code) {
    flush_err(o);
    return code;
}

// A command line refused: `shale: <why>`, then the usage.
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
    say(o, S(" is not ported yet\n"));
    return flush_and_exit(o, EXIT_REFUSED);
}

// ── `use`'s reader ──

typedef struct {
    Arena *arena;
    MountsItems mounts;
    Str mounts_from;
    Budgets budgets;
    Used used;
} LibraryReader;

// The stop a reader's failure becomes: `why`, the message the run stops
// with, which its loader takes and clears.
static STATUS_MUST_USE Status reader_why_parts(Str *why, bool *got, Arena *a, StrPieces parts, size_t n) {
    StrParts all = {parts.piece, n};
    *got = false;
    Buf b = buf_make(a);
    for (size_t k = 0; k < all.len; k++) TRY(buf_str(&b, at(all, k)));
    *why = buf_view(&b);
    return OK;
}

#define reader_why(why, got, n, a, ...) reader_why_parts((why), (got), (a), (StrPieces){{__VA_ARGS__}}, (n))

// A charge past the step budget: the count is left one past it, and the
// budget's stop says how many steps it had.
static STATUS_MUST_USE Status steps_ran_out(LibraryReader *r, Str *why) {
    r->used.steps = r->budgets.steps + 1;
    Buf b = buf_make(r->arena);
    TRY(buf_lit(&b, "the step budget ("));
    TRY(buf_u64(&b, r->budgets.steps));
    TRY(buf_lit(&b, " steps) ran out"));
    *why = buf_view(&b);
    return OK;
}

// Whether any part of `rest` below the mount's folder `dir` is a symbolic
// link, each part checked with `lstat` and charged a step, a missing part
// ending the walk. On a link, `*why` names it.
// DRAGON: the store's boundary. The store follows no link below a mount's
// folder, so a link there could lead a library's read out of it.
// REMEDY: this walk refuses one before the read; a link made between the
// walk and the open is a race left open.
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

// What is left of a budget, never wrapping below zero once a
// charge has stopped one past it.
static uint64_t sat_sub(uint64_t a, uint64_t b) {
    return a > b ? a - b : 0;
}

// A library loaded over the Linux host's mounts: the path resolved as a
// script's path to a mount is, the file read and charged as a `read` of it
// is.
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
    if (stat(dir_c, &dst) != 0 || !S_ISDIR(dst.st_mode)) {
        // A folder whose path spells a secrets mount's is not named (a
        // secrets mount itself was refused above).
        if (mounts_spell_secret_folder(r->mounts, m->dir)) return reader_why(why, got, 3, a, S("the mount `"), m->name, S("`'s folder is missing; its path holds a secrets mount's folder, so it is not named"));
        return reader_why(why, got, 5, a, S("the mount `"), m->name, S("` is "), m->dir, S(", which is not a folder there"));
    }
    bool linked = false;
    TRY(refuse_links(r, m->dir, rest, str_sub(path, 0, colon + 1), why, &linked));
    if (linked) return OK;
    Str full = m->dir;
    if (rest.len > 0) TRY(join(a, m->dir, rest, &full));
    // DRAGON: the secrets rule. A secrets mount's folder may sit inside another
    // mount's, and a library read through the outer one would put a token's
    // bytes in a parse's messages (a name, a number).
    // REMEDY: every path in such a folder is refused here, as paths.c refuses
    // a script's.
    for (size_t k = 0; k < r->mounts.len; k++) {
        const Mount *sm = &at(r->mounts, k);
        if (sm->secrets && mounts_in_folder(full, sm->dir)) return reader_why(why, got, 5, a, S("\""), path, S("\" is in the mount `"), sm->name, S("`, which holds secrets"));
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
        return reader_why(why, got, 5, a, S("`read(\""), path, S("\")`: the store failed ("), error_name(errno), S(")"));
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
static STATUS_MUST_USE Status script_file(Arena *a, Str given, MountsItems mounts, Str *out, bool *missing) {
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

// A write's errno by the name Zig's posix layer gives it.
static Str stdout_error_name(int e) {
    switch (e) {
    case EPIPE: return S("BrokenPipe");
    case EBADF: return S("NotOpenForWriting");
    case ENOSPC: return S("NoSpaceLeft");
    case EIO: return S("InputOutput");
    case EDQUOT: return S("DiskQuota");
    case EFBIG: return S("FileTooBig");
    case EACCES: return S("AccessDenied");
    case EPERM: return S("PermissionDenied");
    case EAGAIN: return S("WouldBlock");
    case ECONNRESET: return S("ConnectionResetByPeer");
    case ENOBUFS:
    case ENOMEM: return S("SystemResources");
    default: return S("Unexpected");
    }
}

// Writes what the run printed to stdout, whole: "" once it is written, else
// the name of the error that stopped the write, the rest dropped. A write
// that takes nothing is an error too, never a loop.
static Str write_stdout(Str out) {
    size_t done = 0;
    while (done < out.len) {
        ssize_t n = write(1, &at(out, done), out.len - done);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return stdout_error_name(errno);
        if (n == 0) return S("Unexpected");
        done += (size_t)n;
    }
    return S("");
}

// Says that stdout could not be written, `shale: stdout: <error>`, after
// whatever else is said, and gives the exit code of a run that failed: what
// a script printed and nobody read is no finished run.
static int stdout_failed(Out *o, Str why) {
    say(o, S("shale: stdout: "));
    say(o, why);
    say(o, S("\n"));
    return EXIT_STOPPED;
}

// The machine budget's stop, for a parse or a compile that an arena refused
// (the machine's tree, or the library reader's, both setting the CLI's
// `refused`). A STOP no arena refused is a bug in the C, never a budget.
static void machine_stop(Stop *stop, bool *refused) {
    if (!*refused) c_bug("shale: a parse or compile stopped with no stop recorded and no arena's refusal");
    *refused = false;
    memset(stop, 0, sizeof *stop);
    stop->line = 1;
    stop->column = 1;
    stop->cls = CLASS_BUDGET;
    stop->budget = BUDGET_MACHINE;
    stop->message = S("the machine's memory (the code, the frames, the operands) is past its budget: raise it with `--budget machine_bytes=N` or the script's `-- budget: machine_bytes=N` line");
    stop->function = S("");
}

// The environment a program inherits, `K=V` each, in `environ`'s order,
// without `ZIG_PROGRESS`, or a `zig build` that `run` runs would write its
// progress to the pipe of the build that ran `shale`.
static STATUS_MUST_USE Status base_env(Arena *a, SpawnStrs *out) {
    extern char **environ;
    size_t n = 0;
    for (char **p = environ; *p != NULL; p++) n++; // cstr: the environment's, NULL-ended
    Str *env = ARENA_ARRAY(a, Str, n);
    if (env == NULL && n > 0) return STOP;
    SLICE_TYPE(EnvRoom, Str);
    EnvRoom room = {env, n};
    size_t k = 0;
    for (char **p = environ; *p != NULL; p++) {
        Str e = from_c(*p); // cstr: the environment's, NULL-ended
        if (str_starts_with(e, S("ZIG_PROGRESS="))) continue;
        at(room, k++) = e;
    }
    out->ptr = env;
    out->len = k;
    return OK;
}

// The script parsed and compiled with its preludes, then either reported as
// checked (`--check`) or run on the VM, its output on stdout and its report
// on stderr.
// Where a run's paths are: the root in one spelling (its real path, "" for
// `/`), the same made absolute, and the working folder a relative root is
// against.
typedef struct {
    Str root;
    Str root_abs;
    Str cwd_abs;
} Where;

static int run_script(Out *o, Command *c, MountsItems mounts, Str mounts_from, Where where, Str source, Preludes preludes, const Arg *args, size_t args_len, int64_t now) {
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
    Arena tree;
    arena_init(&tree, b.machine_bytes + PARSE_ROOM, o->refused);
    Parser p;
    parse_init(&p, &tree, source, b.nesting);
    LibraryReader reader = {a, mounts, mounts_from, b, none};
    ParseLoader loader = {&reader, load_library};
    Program prog = {0};
    Status s = parse_program(&p, preludes_core(), preludes, loader, &prog);
    Stop stop = {0};
    bool stopped = false;
    Code code;
    memset(&code, 0, sizeof code);
    if (s != OK) {
        if (!p.stopped) {
            machine_stop(&stop, o->refused);
        } else {
            stop = p.stop;
        }
        stopped = true;
    } else if (cli_unread_arg(&tree, &prog, args, args_len, &stop, &stopped) != OK) {
        machine_stop(&stop, o->refused);
        stopped = true;
    } else if (!stopped && compile_program(&tree, &prog, BUILTINS_ARGS_MAX, b.depth, &code) != OK) {
        machine_stop(&stop, o->refused);
        stopped = true;
    }
    if (stopped) {
        // A run's report says its effects first, with `--verbose`.
        if (c->verbose && !c->check) say(o, S("── effects (0) ──\n"));
        if (cli_write_stop(&o->err, name, &stop, reader.used, &b) != OK) return not_ported(o, S("a report past the arena"));
        int code_out = flush_and_exit(o, EXIT_STOPPED);
        arena_deinit(&tree);
        return code_out;
    }
    if (c->check) {
        arena_deinit(&tree);
        say(o, S("── checked: it parses and compiles; nothing ran\n"));
        return flush_and_exit(o, EXIT_FINISHED);
    }
    // The values, the machine (the frames' slots, the operands, the frames
    // and the calls) and the output, each in an arena under its own budget,
    // and each setting the run's refusal.
    Run r;
    memset(&r, 0, sizeof r);
    Arena values;
    arena_init(&values, b.arena_bytes, &r.ctx.refused);
    Arena machine;
    arena_init(&machine, b.machine_bytes, &r.ctx.refused);
    Arena output;
    arena_init(&output, b.output_bytes + ARENA_BIG, &r.ctx.refused);
    r.ctx = run_ctx_make(&values, &machine, b);
    r.machine = &machine;
    r.program = &prog;
    r.now = now;
    r.out = buf_make(&output);
    r.out_cap = b.output_bytes;
    // The whole output budget at once, so the buffer never grows past its
    // arena.
    r.out.ptr = arena_alloc(&output, r.out_cap, 1);
    r.out.cap = r.out_cap;
    if (r.out.ptr == NULL) return not_ported(o, S("an output budget past the system's memory"));
    r.args = args;
    r.args_len = args_len;
    r.steps = reader.used.steps;
    r.reads = reader.used.reads;
    r.read_bytes = reader.used.read_bytes;
    r.stack = ValueVec_make(&machine);
    regex_cache_init(&r.patterns, &r.ctx.refused);
    // The store, measured under the root (and `<root>/../auth`) as the
    // bare-metal host would hold it.
    LinuxStore linux_store;
    memset(&linux_store, 0, sizeof linux_store);
    linux_store.has_data_base = true;
    linux_store.data_base = where.root;
    linux_store.has_auth_base = true;
    linux_store.auth_base = S("../auth");
    if (where.root.len > 0 && join(a, where.root, S("../auth"), &linux_store.auth_base) != OK) return not_ported(o, S("a root past the arena"));
    // The host's paths: the mounts, the journal `--undo-on-stop` keeps, in
    // an arena of the host's that outlives the run's values.
    Arena host_arena;
    arena_init(&host_arena, (size_t)1 << 30, &r.ctx.refused);
    Host lh;
    memset(&lh, 0, sizeof lh);
    paths_make(&lh.paths, &host_arena, where.root_abs, where.cwd_abs, mounts, mounts_from);
    lh.paths.undo = c->undo_on_stop;
    SpawnStrs env = {NULL, 0};
    if (base_env(&host_arena, &env) != OK) return not_ported(o, S("an environment past the arena"));
    // The run's zig-ast, started at its first Zig call.
    HostZigProc zig;
    host_zig_init(&zig);
    host_make(&lh, env, &zig);
    // The jobs' reaper is this program, `shale --reap`.
    char exe[PATH_MAX];
    ssize_t exe_len = readlink("/proc/self/exe", exe, sizeof exe);
    if (exe_len > 0 && (size_t)exe_len < sizeof exe) {
        Str reaper = S("");
        if (arena_dup(&host_arena, (Str){exe, (size_t)exe_len}, &reaper)) lh.reaper = reaper;
    }
    PathHook hook;
    paths_hook(&lh.paths, &hook);
    RunStore rs;
    memset(&rs, 0, sizeof rs);
    store_linux(&linux_store, &rs.store);
    PathRun path = {&r.ctx, where.root.len == 0 ? S("/") : where.root, builtin_path_charge, &r, &hook};
    rs.path = path;
    rs.effects = Effects_make(&values);
    rs.diffs_summary = !c->verbose;
    r.store = &rs;
    RunHost host = {&lh, host_call};
    r.host = host;
    Status ran = run_ended(&r.ctx, vm_main(&r, &code));
    // The run's zig-ast, if a Zig call started one, ends with the run, before
    // any report line, so no early return below can skip it.
    host_zig_stop(&zig);
    // A stdout that refuses the output does not end the run's work: the
    // report, the jobs' end and the undo still come, then the failure.
    Str out_failed = write_stdout(buf_view(&r.out));
    RunUsed used = {r.steps, r.reads, r.read_bytes, r.out.len};
    Names effects = {NULL, 0};
    if (c->verbose && store_effect_lines(&rs, a, &effects) != OK) return not_ported(o, S("a report past the arena"));
    Names records = {lh.records.ptr, lh.records.len};
    if (cli_write_report(&o->err, name, preludes, c->verbose, effects, records, ran == OK ? NULL : &r.ctx.stop, used, &b) != OK) return not_ported(o, S("a report past the arena"));
    if (host_time_worth_saying(&lh.time, c->verbose) && host_time_write(&o->err, &lh.time) != OK) return not_ported(o, S("a report past the arena"));
    // A stopped run ends the jobs it started (but `keep_on_stop`'s), before
    // any undo, so no job of its writes past the files put back.
    if (ran != OK && host_jobs_stop_with_run(&lh, &o->err) != OK) return not_ported(o, S("a report past the arena"));
    // `--undo-on-stop`: a stopped run puts back what it changed.
    if (c->undo_on_stop && ran != OK) {
        bool all = false;
        Buf undone = buf_make(&host_arena);
        if (undo_changes(&lh.paths, &undone, &all) != OK) return not_ported(o, S("an undo past the arena"));
        // DRAGON: the secrets rule. The undo's lines name absolute paths,
        // which no stop's check sees: they are said here, not through
        // path_fail.
        // REMEDY: only paths may_change let through are journaled, and a
        // secrets folder's path is refused before that (paths_resolve); the
        // lines are asked paths_names_secret, the hook path_fail asks, and
        // one that names a secrets folder ends the process
        // (paths_report_names_no_secret; test_paths holds its death).
        if (paths_report_names_no_secret(&lh.paths, &rs.path, buf_view(&undone)) != OK) return not_ported(o, S("an undo past the arena"));
        say(o, buf_view(&undone));
    }
    int ended = ran == OK ? EXIT_FINISHED : EXIT_STOPPED;
    if (out_failed.len > 0) ended = stdout_failed(o, out_failed);
    int exit_code = flush_and_exit(o, ended);
    arena_deinit(&host_arena);
    regex_cache_deinit(&r.patterns);
    arena_deinit(&output);
    arena_deinit(&machine);
    arena_deinit(&values);
    arena_deinit(&tree);
    return exit_code;
}

// ── `--help` and `--version` ──

// The folder of a path as /proc/self/exe spells one (absolute, no `//`): the
// path without its last part, `/` for a part at the root, and
// false for `/` itself.
static bool help_dirname(Str p, Str *out) {
    size_t end = p.len;
    while (end > 0 && str_byte(p, end - 1) == '/') end--;
    while (end > 0 && str_byte(p, end - 1) != '/') end--;
    if (end == 0) return false;
    while (end > 1 && str_byte(p, end - 1) == '/') end--;
    *out = str_sub(p, 0, end);
    return true;
}

// The checkout this binary runs from, as `<checkout>/zig-out/bin/shale` would
// be: three folders up from /proc/self/exe, when it holds
// docs/SHALE-AGENTS.md.
static bool help_checkout(Arena *a, Str *out) {
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe);
    if (n <= 0 || (size_t)n >= sizeof exe) return false;
    Str up = {exe, (size_t)n};
    for (int k = 0; k < 3; k++) {
        if (!help_dirname(up, &up)) return false;
    }
    Str agents = {0};
    if (join(a, up, S("docs/SHALE-AGENTS.md"), &agents) != OK) return false;
    const char *c = c_string(a, agents);
    if (c == NULL || access(c, F_OK) != 0) return false;
    return arena_dup(a, up, out);
}

// `shale --help`: the usage, where the docs are and the mounts
// in force, on stdout, each mount with its folder but a `secrets` mount's. A
// mounts file that does not read is said on stderr after the rest, exit 2.
static int help_main(Out *o) {
    Arena *a = o->arena;
    Buf w = buf_make(a);
    Str checkout = {0};
    bool in_checkout = help_checkout(a, &checkout);
    bool built = buf_str(&w, cli_usage()) == OK && buf_lit(&w, "\nThe docs (start with the first):\n") == OK;
    if (built && in_checkout) {
        built = buf_lit(&w, "  ") == OK && buf_str(&w, checkout) == OK && buf_lit(&w, "/docs/SHALE-AGENTS.md\n  ") == OK &&
                buf_str(&w, checkout) == OK && buf_lit(&w, "/docs/SHALE.md\n") == OK;
    } else if (built) {
        built = buf_lit(&w, "  docs/SHALE-AGENTS.md and docs/SHALE.md at github.com/showell/shale\n") == OK;
    }
    if (!built) return not_ported(o, S("a help past the arena"));
    const char *home_c = getenv("HOME"); // cstr: the environment's
    const char *pinned = getenv("SHALE_MOUNTS"); // cstr: the environment's
    MountsLoaded loaded;
    Str why = S("");
    MountsResult mr = mounts_load(a, pinned != NULL, pinned != NULL ? from_c(pinned) : S(""), from_c(home_c != NULL ? home_c : "/"), &loaded, &why);
    if (mr == MOUNTS_OUT_OF_MEMORY) return not_ported(o, S("the mounts past the arena"));
    if (mr == MOUNTS_MALFORMED) {
        Str failed = write_stdout(buf_view(&w));
        say(o, S("shale: the mounts: "));
        say(o, why);
        say(o, S("\n"));
        if (failed.len > 0) (void)stdout_failed(o, failed);
        return flush_and_exit(o, EXIT_REFUSED);
    }
    built = buf_lit(&w, "\nThe mounts, `name:path` in a script (") == OK && buf_str(&w, loaded.from) == OK && buf_lit(&w, "):\n") == OK;
    if (built && loaded.mounts.len == 0) built = buf_lit(&w, "  none\n") == OK;
    for (size_t k = 0; built && k < loaded.mounts.len; k++) {
        const Mount *m = &at(loaded.mounts, k);
        built = buf_lit(&w, "  ") == OK && buf_str(&w, m->name) == OK;
        // DRAGON: the secrets rule. Agents run `--help` to learn the language,
        // and its output lands in their transcripts, so a secrets mount's
        // folder printed here would name where the tokens are.
        // REMEDY: a secrets mount is said by its name alone.
        if (m->secrets) {
            built = built && buf_lit(&w, ":  secrets, for `run`'s `secrets` only (its folder is not shown)\n") == OK;
            continue;
        }
        built = built && buf_lit(&w, ":  ") == OK && buf_str(&w, m->dir) == OK;
        if (built && m->access == MOUNT_READ_ONLY) built = buf_lit(&w, "  ro") == OK;
        if (built && m->tools) built = buf_lit(&w, "  tools") == OK;
        built = built && buf_byte(&w, '\n') == OK;
    }
    if (!built) return not_ported(o, S("a help past the arena"));
    Str failed = write_stdout(buf_view(&w));
    return flush_and_exit(o, failed.len > 0 ? stdout_failed(o, failed) : EXIT_FINISHED);
}

// `shale --version`: the mode this `shale` was built in.
static int version_main(Out *o) {
    Buf w = buf_make(o->arena);
    if (buf_lit(&w, "shale ") != OK || buf_str(&w, cli_build_mode()) != OK || buf_byte(&w, '\n') != OK) return not_ported(o, S("a version past the arena"));
    Str failed = write_stdout(buf_view(&w));
    return flush_and_exit(o, failed.len > 0 ? stdout_failed(o, failed) : EXIT_FINISHED);
}

// The preludes after the core's: the Linux and git
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
    if (join(a, root_abs, S(".shale/prelude.shale"), &repo) != OK) return not_ported(o, S("a prelude's path past the arena"));
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
        if (join(a, from_c(home != NULL ? home : "/"), S(".config/shale/prelude.shale"), &user) != OK) return not_ported(o, S("a prelude's path past the arena"));
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
    // `shale --reap ...` is a job's reaper, detached by `job_start`; its
    // environment is the job's.
    if (all.len > 1 && str_eq(at(all, 1), S("--reap"))) {
        SpawnStrs env = {NULL, 0};
        if (base_env(a, &env) != OK) return 2;
        return jobs_reap_main(a, argv + 2, (size_t)argc - 2, env);
    }
    if (all.len == 2 && (str_eq(at(all, 1), S("--help")) || str_eq(at(all, 1), S("-h")))) return help_main(o);
    if (all.len == 2 && str_eq(at(all, 1), S("--version"))) return version_main(o);
    const char *env_root = getenv("SHALE_ROOT"); // cstr: the environment's
    Command c;
    Buf why = buf_make(a);
    Words rest = all.len > 0 ? SLICE_SUB(Words, all, 1, all.len) : all;
    if (cli_parse(rest.ptr, rest.len, env_root != NULL, env_root != NULL ? from_c(env_root) : S(""), &c, &why) != OK) return refuse_command(o, buf_view(&why), true);
    Arg more[CLI_ARGS_MAX];
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
    MountsLoaded loaded;
    Str mounts_why = S("");
    MountsResult mr = mounts_load(a, pinned_mounts != NULL, pinned_mounts != NULL ? from_c(pinned_mounts) : S(""), home, &loaded, &mounts_why);
    if (mr == MOUNTS_OUT_OF_MEMORY) return not_ported(o, S("the mounts past the arena"));
    if (mr == MOUNTS_MALFORMED) {
        say(o, S("shale: the mounts: "));
        say(o, mounts_why);
        say(o, S("\n"));
        return flush_and_exit(o, EXIT_REFUSED);
    }
    MountsItems mounts = Mounts_items(&loaded.mounts);
    Str from = loaded.from;
    Str err = {0};
    Str source = {0};
    if (c.script == CLI_SCRIPT_SOURCE) {
        source = c.source;
    } else if (c.script == CLI_SCRIPT_FILE) {
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
    // Said before the run starts, so it leads the run's own output.
    flush_err(o);
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
    if (now == NULL) pinned_now = (int64_t)time(NULL);
    Where where = {root, root_abs, S("")};
    char cwd_now[PATH_MAX];
    if (getcwd(cwd_now, sizeof cwd_now) == NULL) return not_ported(o, S("a run with no working folder"));
    if (!arena_dup(a, from_c(cwd_now), &where.cwd_abs)) return not_ported(o, S("a working folder past the arena"));
    return run_script(o, &c, mounts, from, where, source, ps, args, args_len, pinned_now);
}

int main(int argc, char **argv) {
    spawn_quiet_sigpipe();
    bool refused = false;
    Arena a;
    arena_init(&a, (size_t)1 << 30, &refused);
    Out o = {&a, buf_make(&a), &refused};
    int code = run_main(&o, argc, argv);
    arena_deinit(&a);
    return code;
}
