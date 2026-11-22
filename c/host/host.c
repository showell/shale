// host.c: the Linux host itself (`Host`: its paths, the environment a
// program inherits, the records `--verbose` lists and where the run's time
// went), and what `run` and the jobs share of it: the stops, the one way a
// script's string becomes an OsStr (`os_str`), the option readers, an
// errno's name and the output's sinks past the scrubber (`HostOut`).
//
// The host's `run` is five files, each included after the one before:
// host.c, host_refusals.c (the programs `run` refuses), host_write_gate.c
// (where the host itself writes), host_run_secrets.c (everything that
// handles a secret) and host_run.c (`run` and `sleep`).
//
// Include spawn.c, keep.c, paths.c, swap_file.c and builtins_files.c first.

#include <sys/stat.h>

// The most of an argv the time line keeps, so three programs fit one line.
enum { HOST_ARGV_MAX = 80 };

#define HOST_ELIDED "\xe2\x80\xa6"

// One of the slowest programs: its ms and its argv, cut (`host_keep_ends`).
typedef struct {
    uint64_t ms;
    Str argv;
} HostSlow;

SLICE_TYPE(HostSlows, HostSlow);

// Where a run's time went, for the line after its report.
typedef struct {
    uint32_t runs;
    uint64_t run_ms;
    // The slowest programs, slowest first; `top_len` of them in use.
    HostSlow top[3];
    uint8_t top_len;
    uint32_t waits;
    uint64_t wait_ms;
    uint64_t sleep_ms;
} HostTime;

VEC_DEFINE(HostRecords, Str)

// A job this run started, as the host keeps it past the run's values: which
// job it is (its leader's pid and start time, so a later job on the same log
// is another), where its record is, and how a stop names it.
typedef struct {
    Str record_os;
    Str log;
    Str argv;
    int32_t pid;
    int32_t pgid;
    Str start;
    bool keep;
} HostStarted;

VEC_DEFINE(HostStartedJobs, HostStarted)

// The Linux host the CLI hands the core. Its arena is
// `paths.arena`, which outlives the run's values.
typedef struct {
    Paths paths;
    // The environment a program inherits, `K=V` each, `ZIG_PROGRESS` out.
    SpawnStrs env;
    HostTime time;
    // One line each, a program run or a log written: `--verbose`'s `── ran`.
    HostRecords records;
    // The program a job runs under (`shale --reap`), an OS path; empty for
    // none, which means no jobs.
    Str reaper;
    // The jobs this run started, for a stop to end (`host_jobs_stop_with_run`).
    HostStartedJobs started;
    // The run's zig-ast (zig_ast.c), which the run's owner holds and ends.
    struct HostZigProc *zig;
    // The run's own temporary folder, an OS path, made at its first program
    // (host_confine.c), which every program gets as TMPDIR and may write;
    // empty until then.
    Str tmp;
} Host;

static void host_make(Host *h, SpawnStrs env, struct HostZigProc *zig) {
    if (zig == NULL) c_bug("host_run: a host made with no zig-ast of its run's");
    h->env = env;
    h->zig = zig;
    memset(&h->time, 0, sizeof h->time);
    h->records = HostRecords_make(h->paths.arena);
    h->reaper = S("");
    h->started = HostStartedJobs_make(h->paths.arena);
    h->tmp = S("");
}

// ── the time line ──

// An argv past HOST_ARGV_MAX bytes keeps both its ends around an ellipsis,
// HOST_ARGV_MAX bytes in all, neither cut inside a UTF-8 sequence: the
// start names the program, the end the arguments that tell runs apart.
static bool host_keep_ends(Arena *a, Str argv, Str *out) {
    if (argv.len <= HOST_ARGV_MAX) return arena_dup(a, argv, out);
    size_t room = HOST_ARGV_MAX - (sizeof HOST_ELIDED - 1);
    size_t head = room / 2;
    while (head > 0 && (str_byte(argv, head) & 0xC0) == 0x80) head--;
    size_t tail = argv.len - (room - room / 2);
    while (tail < argv.len && (str_byte(argv, tail) & 0xC0) == 0x80) tail++;
    Buf b = buf_make(a);
    if (buf_str(&b, str_sub(argv, 0, head)) != OK || buf_lit(&b, HOST_ELIDED) != OK || buf_str(&b, str_sub(argv, tail, argv.len)) != OK) return false;
    *out = buf_view(&b);
    return true;
}

// Counts one program's `ms`, keeping its argv if it is among the slowest;
// STOP when the arena refuses the argv, which the caller reads as running out
// of memory.
static STATUS_MUST_USE Status host_time_ran(HostTime *t, Arena *a, Str argv, uint64_t ms) {
    t->runs++;
    t->run_ms += ms;
    HostSlows top = {t->top, 3};
    size_t k = 0;
    while (k < t->top_len && at(top, k).ms >= ms) k++;
    if (k == 3) return OK;
    Str kept = S("");
    if (!host_keep_ends(a, argv, &kept)) return STOP;
    if (t->top_len < 3) t->top_len++;
    for (size_t i = (size_t)t->top_len - 1; i > k; i--) at(top, i) = at(top, i - 1);
    at(top, k).ms = ms;
    at(top, k).argv = kept;
    return OK;
}

// Said after the report when it passes a second, or always with `--verbose`.
static inline bool host_time_worth_saying(const HostTime *t, bool verbose) {
    return (verbose && (t->runs > 0 || t->waits > 0 || t->sleep_ms > 0)) || t->run_ms + t->wait_ms + t->sleep_ms >= 1000;
}

static STATUS_MUST_USE Status host_seconds(Buf *w, uint64_t ms) {
    TRY(buf_u64(w, ms / 1000));
    TRY(buf_byte(w, '.'));
    TRY(buf_u64(w, ms % 1000 / 100));
    return buf_lit(w, " s");
}

// `── time: 2 programs, 1.4 s in all; the slowest 1.2 s (85%): zig build
// test; ...; 1 job wait, 40.0 s; slept 0.3 s`, a share being of the whole
// line, waits and sleeps included.
static inline STATUS_MUST_USE Status host_time_write(Buf *w, const HostTime *t) {
    TRY(buf_lit(w, "── time: "));
    TRY(buf_u64(w, t->runs));
    TRY(buf_lit(w, " program"));
    if (t->runs != 1) TRY(buf_byte(w, 's'));
    TRY(buf_lit(w, ", "));
    TRY(host_seconds(w, t->run_ms));
    TRY(buf_lit(w, " in all"));
    uint64_t whole = t->run_ms + t->wait_ms + t->sleep_ms;
    const HostSlows top = {(HostSlow *)t->top, t->top_len};
    for (size_t i = 0; i < top.len; i++) {
        TRY(buf_str(w, i == 0 ? S("; the slowest ") : S("; ")));
        TRY(host_seconds(w, at(top, i).ms));
        TRY(buf_lit(w, " ("));
        TRY(buf_u64(w, whole == 0 ? 0 : at(top, i).ms * 100 / whole));
        TRY(buf_lit(w, "%): "));
        TRY(buf_str(w, at(top, i).argv));
    }
    if (t->waits > 0) {
        TRY(buf_lit(w, "; "));
        TRY(buf_u64(w, t->waits));
        TRY(buf_lit(w, " job wait"));
        if (t->waits != 1) TRY(buf_byte(w, 's'));
        TRY(buf_lit(w, ", "));
        TRY(host_seconds(w, t->wait_ms));
    }
    if (t->sleep_ms > 0) {
        TRY(buf_lit(w, "; slept "));
        TRY(host_seconds(w, t->sleep_ms));
    }
    return buf_byte(w, '\n');
}

// One line for `--verbose`'s `── ran`, in the host's arena, its text `b`
// (`built` its making's answer). A line the arena refuses, made or kept, is
// the arena budget's stop, never a line left out.
static STATUS_MUST_USE Status host_record(Host *h, Run *r, Pos pos, const Buf *b, Status built) {
    if (built != OK) return run_values_oom(r, pos);
    Str kept = S("");
    if (!arena_dup(h->paths.arena, buf_view(b), &kept)) return run_values_oom(r, pos);
    if (HostRecords_push(&h->records, kept) != OK) return run_values_oom(r, pos);
    return OK;
}

// ── stops ──

// A stop of the host's, its message the format `fmt` of `args`.
static STATUS_MUST_USE Status host_fail(Run *r, Pos pos, StopClass cls, Str fmt, MessageArgs args) {
    Status s = path_fail(&builtin_files_of(r)->path, pos, cls, fmt, args);
    run_nums_copied(r);
    return s;
}

// A budget's stop with the message `m` holds, for `host_over_budget` and
// `host_stream_over`, which build their own: class `budget`, the budget
// named.
static STATUS_MUST_USE Status host_budget_fail(Run *r, Pos pos, BudgetKind b, Buf *m, Status built, Str fallback) {
    Status s = run_fail_built(&r->ctx, pos, CLASS_BUDGET, m, built, fallback);
    r->ctx.stop.budget = b;
    return s;
}

// A stop of the host's whose message takes no argument.
static STATUS_MUST_USE Status host_fail_plain(Run *r, Pos pos, StopClass cls, Str fmt) {
    MessageArgs none = {NULL, 0};
    return path_fail(&builtin_files_of(r)->path, pos, cls, fmt, none);
}

// `s`, a script's string that `who` hands the kernel as its `what` (`argument
// 2`, "`env` F"), as an OsStr into `*out`: the one way a script's string
// becomes one. A NUL in it is refused here, CLASS_REFUSED naming the
// argument, where the cause is known; past here no call can meet one.
static STATUS_MUST_USE Status os_str(Run *r, Pos pos, Str who, Str what, Str s, OsStr *out) {
    if (os_str_of(s, out)) return OK;
    return host_fail(r, pos, CLASS_REFUSED, S("`{s}`'s {s} holds a NUL byte, where the kernel would end it: the program would be handed less than the guards saw"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(what)));
}

// ── the options ──

static STATUS_MUST_USE Status host_int_opt(Run *r, Pos pos, Str who, Str k, const Value *x, uint64_t *out) {
    if (x->tag != VALUE_INT) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be an integer, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(k), MESSAGE_FROM_HOST(value_type_name(x))));
    if (x->as.integer < 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`{s}`: its `{s}` must be 0 or more, not {d}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(k), MESSAGE_FROM_HOST(run_num(r, x->as.integer))));
    *out = (uint64_t)x->as.integer;
    return OK;
}

static STATUS_MUST_USE Status host_str_opt(Run *r, Pos pos, Str who, Str k, const Value *x, Str *out) {
    if (x->tag != VALUE_STRING) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be a string, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(k), MESSAGE_FROM_HOST(value_type_name(x))));
    *out = x->as.string;
    return OK;
}

static STATUS_MUST_USE Status host_map_opt(Run *r, Pos pos, Str who, Str k, const Value *x, Table **out) {
    if (x->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be a map, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(k), MESSAGE_FROM_HOST(value_type_name(x))));
    if (x->as.table->kind == TABLE_LIST) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be a map, not a list"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(k)));
    *out = x->as.table;
    return OK;
}

static STATUS_MUST_USE Status host_bool_opt(Run *r, Pos pos, Str who, Str k, const Value *x, bool *out) {
    if (x->tag != VALUE_BOOL) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be true or false, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(k), MESSAGE_FROM_HOST(value_type_name(x))));
    *out = x->as.boolean;
    return OK;
}

// An integer option the script did not give. A script's integer is at most
// INT64_MAX, so no option given is this.
#define HOST_UNSET UINT64_MAX

// A program's `env` or `secrets` as the options boundary read it: each name
// beside its string (`env`'s value, a secret's path as the script gave it),
// in the map's order. None given is empty.
typedef struct {
    PathsStrs names;
    PathsStrs values;
} HostEnvOpt;

// `env` or `secrets` (`k`), read once into names and their strings, for
// `who`, `run` or `job_start`: a value that is not a string, and a NUL in a
// name or an `env` value, stop the run here (`os_str`), so the guards and
// the environment below test no value's type and meet no NUL. A secret's
// value is its file's path (`paths`), held to a path's rules, and its stop
// says so.
static STATUS_MUST_USE Status host_env_opt(Run *r, Pos pos, Str who, Str k, const Value *x, bool paths, HostEnvOpt *out) {
    Table *t = NULL;
    TRY(host_map_opt(r, pos, who, k, x, &t));
    size_t n = ValueMap_len(&t->map);
    Str *names = ARENA_ARRAY(r->ctx.values, Str, n);
    Str *values = ARENA_ARRAY(r->ctx.values, Str, n);
    if (n > 0 && (names == NULL || values == NULL)) return run_values_oom(r, pos);
    SLICE_TYPE(EnvRoom, Str);
    EnvRoom nroom = {names, n};
    EnvRoom vroom = {values, n};
    for (size_t j = 0; j < n; j++) {
        Value v = ValueMap_value_at(&t->map, j);
        Str name = ValueMap_key_at(&t->map, j);
        Str what = S("");
        TRY(paths_cat(&builtin_files_of(r)->path, pos, S("`"), k, S("` name"), &what));
        OsStr checked;
        TRY(os_str(r, pos, who, what, name, &checked));
        at(nroom, j) = name;
        if (paths) {
            TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &v, S("its path"), &at(vroom, j)));
        } else {
            TRY(host_str_opt(r, pos, who, k, &v, &at(vroom, j)));
            TRY(paths_cat(&builtin_files_of(r)->path, pos, S("`env` "), name, S(""), &what));
            TRY(os_str(r, pos, who, what, at(vroom, j), &checked));
        }
    }
    out->names.ptr = names;
    out->names.len = n;
    out->values.ptr = values;
    out->values.len = n;
    return OK;
}

// Whether the script gave a value option.
static bool host_given(const Value *opt) {
    return opt->tag != VALUE_NIL;
}

// ── an errno's name ──

// An errno's name as the messages spell it.
static Str host_errno_tag(int e) {
    switch (e) {
    case 0: return S("SUCCESS");
    case EPERM: return S("PERM");
    case ENOENT: return S("NOENT");
    case EIO: return S("IO");
    case EBADF: return S("BADF");
    case EACCES: return S("ACCES");
    case EEXIST: return S("EXIST");
    case EXDEV: return S("XDEV");
    case ENOTDIR: return S("NOTDIR");
    case EISDIR: return S("ISDIR");
    case EINVAL: return S("INVAL");
    case ENOSPC: return S("NOSPC");
    case EROFS: return S("ROFS");
    case ENAMETOOLONG: return S("NAMETOOLONG");
    case ELOOP: return S("LOOP");
    case EDQUOT: return S("DQUOT");
    case EMFILE: return S("MFILE");
    case ENFILE: return S("NFILE");
    case EBUSY: return S("BUSY");
    default: return S("UNKNOWN");
    }
}

// ── the output's sinks ──

// Where scrubbed output goes: the whole log, and the keep or `whole`.
typedef struct {
    int log_fd;
    bool log_failed;
    Keep *keeper;
    Buf *whole;
    uint64_t whole_max;
    bool whole_over;
    uint64_t bytes;
} HostOut;

static STATUS_MUST_USE Status host_out_write(HostOut *o, Str bytes) {
    o->bytes += bytes.len;
    if (o->log_fd >= 0 && !o->log_failed) {
        size_t done = 0;
        while (done < bytes.len) {
            ssize_t w = write(o->log_fd, &at(bytes, done), bytes.len - done);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) {
                o->log_failed = true;
                break;
            }
            done += (size_t)w;
        }
    }
    if (o->keeper != NULL) TRY(keep_feed(o->keeper, bytes));
    if (o->whole != NULL) {
        if (o->whole->len + bytes.len > o->whole_max) {
            o->whole_over = true;
        } else {
            TRY(buf_str(o->whole, bytes));
        }
    }
    return OK;
}
