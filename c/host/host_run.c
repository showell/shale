// host_run.c: the Linux host's `run`, and what it shares with the jobs: the
// host itself (`Host`: its paths, the environment a program inherits, the
// records `--verbose` lists and where the run's time went), the refusals
// and the one guard prologue every program passes (`host_guard_program`),
// the option readers, the environment a program gets, the scrubber of secrets
// (`HostScrub`) and the output's sinks (`HostOut`).
//
// A program's output streams through the scrubber, then to the log, and to
// the keep (keep.c) or `whole`'s buffer, all in an arena of the stream's own
// capped at what is left of the run's arena budget, so a tail of 3 over a
// 10 GB log holds three lines; only the answer goes to the values' arena.
//
// Include spawn.c, keep.c, paths.c, swap_file.c and builtins_files.c first.

#include <sys/stat.h>

// The most a `sleep` waits in one call: ten minutes.
enum { HOST_SLEEP_MAX_MS = 600000 };

// With `stdout_secret`, `out` is stderr's last lines, this many.
enum { HOST_SECRET_ERR_TAIL = 20 };

// The most bytes `stdout_secret` stores, and `secrets` reads of a file.
enum { HOST_SECRET_MAX = 64 * 1024 };

// What a failed program's output keeps in a budget's stop, from its end.
enum { HOST_BUDGET_STOP_OUTPUT = 2000 };

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
} Host;

static void host_make(Host *h, SpawnStrs env, struct HostZigProc *zig) {
    if (zig == NULL) c_bug("host_run: a host made with no zig-ast of its run's");
    h->env = env;
    h->zig = zig;
    memset(&h->time, 0, sizeof h->time);
    h->records = HostRecords_make(h->paths.arena);
    h->reaper = S("");
    h->started = HostStartedJobs_make(h->paths.arena);
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

// Counts one program's `ms`, keeping its argv if it is among the slowest.
// An argv the arena refuses is left out of the top, and the count still
// holds it.
static void host_time_ran(HostTime *t, Arena *a, Str argv, uint64_t ms) {
    t->runs++;
    t->run_ms += ms;
    HostSlows top = {t->top, 3};
    size_t k = 0;
    while (k < t->top_len && at(top, k).ms >= ms) k++;
    if (k == 3) return;
    Str kept = S("");
    if (!host_keep_ends(a, argv, &kept)) return;
    if (t->top_len < 3) t->top_len++;
    for (size_t i = (size_t)t->top_len - 1; i > k; i--) at(top, i) = at(top, i - 1);
    at(top, k).ms = ms;
    at(top, k).argv = kept;
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

// One line for `--verbose`'s `── ran`, in the host's arena. A line the arena
// refuses is left out, and the run goes on.
static void host_record(Host *h, Str line) {
    Str kept = S("");
    if (!arena_dup(h->paths.arena, line, &kept)) return;
    if (HostRecords_push(&h->records, kept) != OK) return;
}

// ── stops ──

// A stop of the host's, its message the format `fmt` of `args`.
static STATUS_MUST_USE Status host_fail(Run *r, Pos pos, StopClass cls, Str fmt, MsgArgs args) {
    return path_fail(&builtin_files_of(r)->path, pos, cls, fmt, args);
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
    MsgArgs none = {NULL, 0};
    return path_fail(&builtin_files_of(r)->path, pos, cls, fmt, none);
}

// `s`, a script's string that `who` hands the kernel as its `what` (`argument
// 2`, "`env` F"), as an OsStr into `*out`: the one way a script's string
// becomes one. A NUL in it is refused here, CLASS_REFUSED naming the
// argument, where the cause is known; past here no call can meet one.
static STATUS_MUST_USE Status os_str(Run *r, Pos pos, Str who, Str what, Str s, OsStr *out) {
    if (os_str_of(s, out)) return OK;
    return host_fail(r, pos, CLASS_REFUSED, S("`{s}`'s {s} holds a NUL byte, where the kernel would end it: the program would be handed less than the guards saw"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(what)));
}

// ── the options ──

static STATUS_MUST_USE Status host_int_opt(Run *r, Pos pos, Str who, Str k, const Value *x, uint64_t *out) {
    if (x->tag != VALUE_INT) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be an integer, not {s}"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(k), MSG_FROM_HOST(value_type_name(x))));
    if (x->as.integer < 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`{s}`: its `{s}` must be 0 or more, not {d}"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(k), MSG_FROM_HOST(run_num(r, x->as.integer))));
    *out = (uint64_t)x->as.integer;
    return OK;
}

static STATUS_MUST_USE Status host_str_opt(Run *r, Pos pos, Str who, Str k, const Value *x, Str *out) {
    if (x->tag != VALUE_STRING) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be a string, not {s}"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(k), MSG_FROM_HOST(value_type_name(x))));
    *out = x->as.string;
    return OK;
}

static STATUS_MUST_USE Status host_map_opt(Run *r, Pos pos, Str who, Str k, const Value *x, Table **out) {
    if (x->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be a map, not {s}"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(k), MSG_FROM_HOST(value_type_name(x))));
    if (x->as.table->kind == TABLE_LIST) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be a map, not a list"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(k)));
    *out = x->as.table;
    return OK;
}

static STATUS_MUST_USE Status host_bool_opt(Run *r, Pos pos, Str who, Str k, const Value *x, bool *out) {
    if (x->tag != VALUE_BOOL) return host_fail(r, pos, CLASS_TYPE, S("`{s}`: its `{s}` must be true or false, not {s}"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(k), MSG_FROM_HOST(value_type_name(x))));
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

// What `run` keeps of the program's output, decided once where the options
// are read: the code below switches on it, and never asks which options were
// given.
typedef enum {
    // `head` and `tail` lines (or both), through the filters.
    HOST_KEEP_LINES,
    // `whole = N`: every byte, up to N.
    HOST_KEEP_WHOLE,
    // `stdout_secret`: stdout to the secret's file, stderr's last
    // HOST_SECRET_ERR_TAIL lines kept.
    HOST_KEEP_SECRET,
} HostKeepMode;

// `run`'s options, as the script gives them. An integer option not given is
// HOST_UNSET, a value option nil (a script's map never holds nil), and `env`
// and `secrets` empty. `before`, `after` and `nocase` are 0 or false unset.
typedef struct {
    uint64_t timeout_ms;
    // `re`, `not_re`, `want` and `stage`: strings.
    Value re;
    Value not_re;
    bool nocase;
    uint64_t before;
    uint64_t after;
    uint64_t head;
    uint64_t tail;
    uint64_t width;
    uint64_t whole;
    // `log` and `cwd`: strings. `stdin` and `stdout_secret`: checked at
    // their use.
    Value log;
    Value cwd;
    Value stdin_v;
    HostEnvOpt env;
    HostEnvOpt secrets;
    Value want;
    Value stdout_secret;
    bool secret_replace;
    // HOST_UNSET is no budget: no program's CPU time passes it.
    uint64_t cpu_budget_ms;
    Value stage;
    // The program's niceness, 0 to 19; HOST_UNSET leaves `shale`'s.
    uint64_t nice;
    // Decided from the keep options once they are read: then `head` and
    // `tail` are 0 unset, and `width` KEEP_WIDTH_DEFAULT.
    HostKeepMode keep;
} RunOpts;

// Whether the script gave a value option.
static bool host_given(const Value *opt) {
    return opt->tag != VALUE_NIL;
}

// A string option the script gave, checked to be one, kept as its value.
static STATUS_MUST_USE Status host_str_value_opt(Run *r, Pos pos, Str who, Str k, const Value *x, Value *out) {
    Str unused = S("");
    TRY(host_str_opt(r, pos, who, k, x, &unused));
    *out = *x;
    return OK;
}

// Decides `o->keep` from the keep options the script gave, holding them to
// the mode's rules: `stdout_secret` takes no keep, filter or log; `whole`
// no filter or keep; and without either, `head` or `tail` is required. Then
// the sizes unset are made what the keep takes.
static STATUS_MUST_USE Status host_keep_decide(Run *r, Pos pos, RunOpts *o) {
    bool filtered = host_given(&o->re) || host_given(&o->not_re) || o->before > 0 || o->after > 0 || o->nocase;
    bool kept = o->head != HOST_UNSET || o->tail != HOST_UNSET;
    o->keep = host_given(&o->stdout_secret) ? HOST_KEEP_SECRET : o->whole != HOST_UNSET ? HOST_KEEP_WHOLE : HOST_KEEP_LINES;
    if (o->keep != HOST_KEEP_SECRET && o->secret_replace) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`'s `secret_replace` is `stdout_secret`'s: it says a stored secret may be replaced"));
    switch (o->keep) {
    case HOST_KEEP_SECRET:
        if (o->whole != HOST_UNSET || filtered || kept || o->width != HOST_UNSET || host_given(&o->log)) {
            return host_fail(r, pos, CLASS_ARGUMENT, S("`run`'s `stdout_secret` sends stdout to the secret alone, so it takes no keep, filter or log (`whole`, `head`, `tail`, `re`, `not_re`, `nocase`, `before`, `after`, `width`, `log`): its `out` is stderr's last {d} lines"), MSG_ARGS(MSG_FROM_HOST(S("20"))));
        }
        o->tail = HOST_SECRET_ERR_TAIL;
        break;
    case HOST_KEEP_WHOLE:
        if (filtered || kept) {
            return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`'s `whole` keeps everything, and takes no filter or keep (`re`, `not_re`, `nocase`, `before`, `after`, `head`, `tail`)"));
        }
        break;
    case HOST_KEEP_LINES:
        if (!kept) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run` needs a keep: `head`, `tail` (or both), or `whole = N`: what to keep of the output is the script's to say"));
        break;
    }
    if ((o->before > 0 || o->after > 0) && !host_given(&o->re)) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`'s `before` and `after` are a match's context, and need `re`"));
    if (o->head == HOST_UNSET) o->head = 0;
    if (o->tail == HOST_UNSET) o->tail = 0;
    if (o->width == HOST_UNSET) o->width = KEEP_WIDTH_DEFAULT;
    return OK;
}

// The options read, each held to its rules.
static STATUS_MUST_USE Status host_read_opts(Run *r, Pos pos, const Value *v, RunOpts *o) {
    memset(o, 0, sizeof *o);
    o->timeout_ms = HOST_UNSET;
    o->head = HOST_UNSET;
    o->tail = HOST_UNSET;
    o->width = HOST_UNSET;
    o->whole = HOST_UNSET;
    o->cpu_budget_ms = HOST_UNSET;
    o->nice = HOST_UNSET;
    value_nil(&o->re);
    value_nil(&o->not_re);
    value_nil(&o->log);
    value_nil(&o->cwd);
    value_nil(&o->stdin_v);
    value_nil(&o->want);
    value_nil(&o->stdout_secret);
    value_nil(&o->stage);
    if (v->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`run`'s options are a map, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(v))));
    Table *m = v->as.table;
    if (m->kind == TABLE_LIST) return host_fail_plain(r, pos, CLASS_TYPE, S("`run`'s options are a map, `{{timeout_ms = ..., tail = ...}}`, not a list"));
    Str who = S("run");
    for (size_t i = 0; i < ValueMap_len(&m->map); i++) {
        Str k = ValueMap_key_at(&m->map, i);
        Value x = ValueMap_value_at(&m->map, i);
        if (str_eq(k, S("timeout_ms"))) {
            TRY(host_int_opt(r, pos, who, k, &x, &o->timeout_ms));
        } else if (str_eq(k, S("re"))) {
            TRY(host_str_value_opt(r, pos, who, k, &x, &o->re));
        } else if (str_eq(k, S("not_re"))) {
            TRY(host_str_value_opt(r, pos, who, k, &x, &o->not_re));
        } else if (str_eq(k, S("nocase"))) {
            TRY(host_bool_opt(r, pos, who, k, &x, &o->nocase));
        } else if (str_eq(k, S("before"))) {
            TRY(host_int_opt(r, pos, who, k, &x, &o->before));
        } else if (str_eq(k, S("after"))) {
            TRY(host_int_opt(r, pos, who, k, &x, &o->after));
        } else if (str_eq(k, S("head"))) {
            TRY(host_int_opt(r, pos, who, k, &x, &o->head));
        } else if (str_eq(k, S("tail"))) {
            TRY(host_int_opt(r, pos, who, k, &x, &o->tail));
        } else if (str_eq(k, S("width"))) {
            TRY(host_int_opt(r, pos, who, k, &x, &o->width));
        } else if (str_eq(k, S("stdout_secret"))) {
            o->stdout_secret = x;
        } else if (str_eq(k, S("secret_replace"))) {
            TRY(host_bool_opt(r, pos, who, k, &x, &o->secret_replace));
        } else if (str_eq(k, S("whole"))) {
            if (x.tag != VALUE_INT) return host_fail(r, pos, CLASS_TYPE, S("`run`'s `whole` takes a byte limit, `whole = 65536`, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(&x))));
            TRY(host_int_opt(r, pos, who, k, &x, &o->whole));
        } else if (str_eq(k, S("log"))) {
            TRY(host_str_value_opt(r, pos, who, k, &x, &o->log));
        } else if (str_eq(k, S("cwd"))) {
            TRY(host_str_value_opt(r, pos, who, k, &x, &o->cwd));
        } else if (str_eq(k, S("stdin"))) {
            o->stdin_v = x;
        } else if (str_eq(k, S("env"))) {
            TRY(host_env_opt(r, pos, who, k, &x, false, &o->env));
        } else if (str_eq(k, S("secrets"))) {
            TRY(host_env_opt(r, pos, who, k, &x, true, &o->secrets));
        } else if (str_eq(k, S("want"))) {
            TRY(host_str_value_opt(r, pos, who, k, &x, &o->want));
        } else if (str_eq(k, S("cpu_budget_ms"))) {
            TRY(host_int_opt(r, pos, who, k, &x, &o->cpu_budget_ms));
        } else if (str_eq(k, S("stage"))) {
            TRY(host_str_value_opt(r, pos, who, k, &x, &o->stage));
        } else if (str_eq(k, S("nice"))) {
            TRY(host_int_opt(r, pos, who, k, &x, &o->nice));
            if (o->nice > 19) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`'s `nice` is 0 to 19 (19 the lowest priority), not {d}"), MSG_ARGS(MSG_FROM_HOST(run_num(r, x.as.integer))));
        } else {
            return host_fail(r, pos, CLASS_ARGUMENT, S("`run` takes timeout_ms, re, not_re, nocase, before, after, head, tail, width, whole, log, cwd, stdin, env, secrets, want, stdout_secret, secret_replace, cpu_budget_ms, stage and nice, not `{s}`"), MSG_ARGS(MSG_FROM_SCRIPT(k)));
        }
    }
    if (o->timeout_ms == HOST_UNSET) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run` needs `timeout_ms`: how long the program may take is the script's to say"));
    if (host_given(&o->stage) && o->cpu_budget_ms == HOST_UNSET) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`'s `stage` names the program in a `cpu_budget_ms` stop, and needs one"));
    return host_keep_decide(r, pos, o);
}

// ── the refusals ──

typedef struct {
    const char *names; // cstr: the names, each ended by a space
    const char *why; // cstr: a literal
} HostRefusedGroup;

SLICE_TYPE(HostRefusedGroups, const HostRefusedGroup);

// Programs `run` refuses without a `want`.
static const HostRefusedGroup HOST_REFUSED[] = {
    {"bash sh dash zsh busybox python python3 perl node ", "a shell or an interpreter: Shale is the script"},
    {"head tail awk gawk cut sort uniq wc cat tr ", "a text tool: Shale's builtins and run's own filter and keep do this"},
    {"env xargs timeout nohup setsid stdbuf time ", "a wrapper that runs another program: run that program, with run's own options"},
    {"nice ", "a wrapper that runs another program: run that program, with run's own `nice = N` (0 to 19)"},
};

// Programs `run` refuses even with a `want`.
static const HostRefusedGroup HOST_NEVER[] = {
    {"sed gsed ", "`edit(p, old, new)` changes a file and stops the run when `old` isn't there; `re_replace(s, pattern, new)` then `replace(p, s)` for a pattern; `lines(read(p))`, or `shale cookbook:look.shale at=f:N-M`, for a file's lines"},
    {"grep egrep fgrep ", "`grep_tree(place, needle, {regex, nocase, ext, like, context})` searches a tree; `grep(p, lit)`, `re_lines(read(p), pattern)` or `grep_all(p, lit)` one file; `report_run(argv, {grep = {...}})` or `run`'s `re` a program's output"},
};

// Whether `name` is one of a group's space-ended names.
static bool host_group_has(const HostRefusedGroup *g, Str name) {
    if (name.len == 0) return false;
    Str all = {g->names, strlen(g->names)};
    size_t from = 0;
    while (from < all.len) {
        const char *sp = mem_find_byte(&at(all, from), ' ', all.len - from);
        size_t to = (size_t)(sp - all.ptr);
        if (str_eq(str_sub(all, from, to), name)) return true;
        from = to + 1;
    }
    return false;
}

// `p`'s last part, trailing `/`s dropped.
static Str host_basename(Str p) {
    size_t end = p.len;
    while (end > 0 && str_byte(p, end - 1) == '/') end--;
    size_t start = end;
    while (start > 0 && str_byte(p, start - 1) != '/') start--;
    return str_sub(p, start, end);
}

// argv[0]'s basename with each `.digits` suffix dropped (`python3.12` is
// `python3`).
static Str host_base_name(Str argv0) {
    Str b = host_basename(argv0);
    while (1) {
        size_t dot = b.len;
        while (dot > 0 && str_byte(b, dot - 1) != '.') dot--;
        if (dot == 0) break;
        Str tail = str_sub(b, dot, b.len);
        if (tail.len == 0) break;
        for (size_t i = 0; i < tail.len; i++) {
            uint8_t c = str_byte(tail, i);
            if (c < '0' || c > '9') return b;
        }
        b = str_sub(b, 0, dot - 1);
    }
    return b;
}

// Whether `argv0`, an absolute path, is inside a mount marked ` tools`.
static bool host_under_tools_mount(const Host *h, Str argv0) {
    for (size_t k = 0; k < h->paths.mounts.len; k++) {
        const Mount *m = &at(h->paths.mounts, k);
        if (!m->tools) continue;
        if (argv0.len > m->dir.len && mounts_in_folder(argv0, m->dir)) return true;
    }
    return false;
}

static STATUS_MUST_USE Status host_discard(void *ctx, Str bytes) {
    (void)ctx;
    (void)bytes;
    return OK;
}

// Whether git tracks the program at `path`, relative to `cwd`, for `who`'s
// guard. A git that cannot start stops the run, saying so: read as "no", it
// would refuse the program as untracked, a cause it is not.
static STATUS_MUST_USE Status host_tracked(Host *h, Run *r, Pos pos, Str who, OsStr path, Str cwd, bool *out) {
    OsStr argv_all[5] = {os_str_own(S("git")), os_str_own(S("ls-files")), os_str_own(S("--error-unmatch")), os_str_own(S("--")), path};
    SpawnSpec spec;
    spawn_spec_default(&spec);
    spec.argv.ptr = argv_all;
    spec.argv.len = 5;
    // The folder is a CheckedPath's, made absolute (host_guard_program).
    spec.cwd = os_str_own(cwd);
    spec.env = h->env;
    spec.timeout_ms = 10000;
    SpawnSink sink = {NULL, host_discard};
    SpawnOutcome o;
    SpawnResult got = spawn_run(r->ctx.values, &spec, sink, &o);
    if (got == SPAWN_OUT_OF_MEMORY) return run_values_oom(r, pos);
    if (got != SPAWN_OK) {
        Str why = got == SPAWN_NOT_FOUND ? S("there is no git") : got == SPAWN_NOT_RUNNABLE ? S("git is not runnable") : got == SPAWN_NO_CWD ? S("its folder cannot be entered") : S("the kernel refused a pipe or a fork");
        return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`: a program by path is run only when git tracks it, and git could not start to say: {s}"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(path.s), MSG_FROM_HOST(why)));
    }
    *out = o.ended == SPAWN_EXIT && o.code == 0;
    return OK;
}

// Refuses a program in HOST_NEVER whatever the want; then, without one, a
// program in HOST_REFUSED and a program by a path git does not track, unless
// it is under a ` tools` mount. Used by `run` and `job_start`.
static STATUS_MUST_USE Status host_refuse(Host *h, Run *r, Pos pos, Str who, OsStr argv0_os, Str cwd, bool has_want) {
    Str argv0 = argv0_os.s;
    Str base = host_base_name(argv0);
    Str bare = base;
    while (bare.len > 0 && str_byte(bare, bare.len - 1) >= '0' && str_byte(bare, bare.len - 1) <= '9') bare = str_sub(bare, 0, bare.len - 1);
    HostRefusedGroups never = {HOST_NEVER, sizeof HOST_NEVER / sizeof *HOST_NEVER};
    for (size_t g = 0; g < never.len; g++) {
        const HostRefusedGroup *grp = &at(never, g);
        if (host_group_has(grp, base) || host_group_has(grp, bare)) {
            return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`, even with a want: {s}"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(base), MSG_FROM_HOST(((Str){grp->why, strlen(grp->why)}))));
        }
    }
    if (has_want) return OK;
    HostRefusedGroups refused = {HOST_REFUSED, sizeof HOST_REFUSED / sizeof *HOST_REFUSED};
    for (size_t g = 0; g < refused.len; g++) {
        const HostRefusedGroup *grp = &at(refused, g);
        if (host_group_has(grp, base) || host_group_has(grp, bare)) {
            return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`: {s}; {{want = \"tag: why\"}} to run it anyway"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(base), MSG_FROM_HOST(((Str){grp->why, strlen(grp->why)}))));
        }
    }
    if (str_find_byte(argv0, '/') == NULL || host_under_tools_mount(h, argv0)) return OK;
    bool known = false;
    TRY(host_tracked(h, r, pos, who, argv0_os, cwd, &known));
    if (known) return OK;
    return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`: a program by path that git does not track (a script just written?); {{want = \"tag: why\"}} to run it anyway"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(argv0)));
}

// A program as the guards see it, from `run`'s options or `job_start`'s.
typedef struct {
    // `run` or `job_start`, for the messages.
    Str who;
    SpawnStrs argv;
    // A string, the folder it runs in; nil for the root.
    const Value *cwd;
    const HostEnvOpt *env;
    const HostEnvOpt *secrets;
    bool has_want;
} HostProgram;

// The guards a program passes before it starts, one prologue for `run` and
// `job_start`, giving the folder it runs in as an OS path.
// DRAGON: the secrets rule. A guard that tests other bytes than the program
// is handed lets a secret's path through: a Shale string may hold a NUL, and
// the kernel ends an argument at it, so `"keys\0x"` would pass the secrets
// folder's guard whole and reach the program as `keys`. And `host_refuse`
// hands argv[0] to git (`ls-files`), which must never be shown a secret's
// path.
// REMEDY: the argv is OsStrs and the `env` read through `os_str`, so no NUL
// is here to be cut and every guard tests exactly the bytes the program is
// handed; the secrets guard runs before `host_refuse`, so a path into a
// secrets folder stops the run before git starts. test_run_guards plants
// each, for both callers.
static STATUS_MUST_USE Status host_guard_program(Host *h, Run *r, Pos pos, const HostProgram *p, Str *cwd) {
    RunFiles *rs = builtin_files_of(r);
    if (host_given(p->cwd)) {
        TRY(paths_os_path(&h->paths, &rs->path, pos, builtin_who(r, BUILTIN_HOST), p->cwd->as.string, true, cwd));
    } else {
        *cwd = paths_root_of(&h->paths, &rs->path);
    }
    // The argv's bytes, as the secrets guard reads them.
    SLICE_TYPE(Words, Str);
    Words words = {ARENA_ARRAY(r->ctx.values, Str, p->argv.len), p->argv.len};
    if (words.ptr == NULL) return run_values_oom(r, pos);
    for (size_t i = 0; i < words.len; i++) at(words, i) = at(p->argv, i).s;
    PathsStrs argv = {words.ptr, words.len};
    TRY(paths_refuse_secret_args(&h->paths, &rs->path, pos, p->who, argv, *cwd, p->env->names, p->env->values));
    return host_refuse(h, r, pos, p->who, at(p->argv, 0), *cwd, p->has_want);
}

// ── the output's way: scrub, then log, keep or whole ──

// A secret's name and its bytes.
typedef struct {
    Str name;
    Str value;
} HostSecret;

SLICE_TYPE(HostSecrets, const HostSecret);

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

// Replaces each secret's bytes with `<secret K>` before the log, the keep
// and `whole`. A possible secret split across reads is held
// back in `carry` until it can be decided.
// DRAGON: the secrets rule. A secret's bytes that reach `next` unscrubbed
// are in the script's hands, its log and its report.
// REMEDY: a rest that could still begin a secret waits for the next read;
// the longest whole match wins, and a secret starting inside it widens it,
// so no part of one is passed raw; the last read scans all that is held.
typedef struct {
    HostSecrets secrets;
    Buf carry;
    Buf scanned;
    HostOut *next;
} HostScrub;

static bool host_starts_with(Str s, Str prefix) {
    return s.len >= prefix.len && str_eq(str_sub(s, 0, prefix.len), prefix);
}

// Whether `part` is the start of `whole` and shorter: what more bytes may
// yet make `whole`.
static bool host_is_proper_prefix(Str part, Str whole) {
    return part.len < whole.len && host_starts_with(whole, part);
}

// Whether `rest`, all that is held from here on, could still become a
// secret.
static bool host_scrub_may_begin(const HostScrub *s, Str rest) {
    for (size_t k = 0; k < s->secrets.len; k++) {
        if (host_is_proper_prefix(rest, at(s->secrets, k).value)) return true;
    }
    return false;
}

// The longest secret `rest` begins with, or NULL for none.
static const HostSecret *host_scrub_longest(const HostScrub *s, Str rest) {
    const HostSecret *best = NULL;
    for (size_t k = 0; k < s->secrets.len; k++) {
        const HostSecret *kv = &at(s->secrets, k);
        if (host_starts_with(rest, kv->value) && (best == NULL || kv->value.len > best->value.len)) best = kv;
    }
    return best;
}

// Widens a match in `buf`, from `from` to `*end`, over each secret that
// starts inside it and runs past its end, until none does. False, unless
// this is the `last` read, when a secret could start inside it and run past
// what is held: the next read decides where the match ends.
static bool host_scrub_widen(const HostScrub *s, Str buf, size_t from, bool last, size_t *end) {
    for (size_t j = from + 1; j < *end; j++) {
        Str there = str_sub(buf, j, buf.len);
        for (size_t k = 0; k < s->secrets.len; k++) {
            Str o = at(s->secrets, k).value;
            if (host_starts_with(there, o)) {
                if (j + o.len > *end) *end = j + o.len;
            } else if (!last && host_is_proper_prefix(there, o)) {
                return false;
            }
        }
    }
    return true;
}

// Scrubs what `carry` holds from its start: where a secret starts, its
// match (the longest, widened) goes on as `<secret K>`, and any other byte
// goes on as it is. It stops where what is held could still begin a secret
// or a match's end is not known yet, keeping the rest in `carry` for the
// next read; the `last` read decides all that is held.
static STATUS_MUST_USE Status host_scrub_scan(HostScrub *s, bool last) {
    Str buf = buf_view(&s->carry);
    buf_clear(&s->scanned);
    size_t i = 0;
    while (i < buf.len) {
        Str rest = str_sub(buf, i, buf.len);
        if (!last && host_scrub_may_begin(s, rest)) break;
        const HostSecret *best = host_scrub_longest(s, rest);
        if (best == NULL) {
            TRY(buf_byte(&s->scanned, (char)str_byte(buf, i)));
            i++;
            continue;
        }
        size_t end = i + best->value.len;
        if (!host_scrub_widen(s, buf, i, last, &end)) break;
        TRY(buf_lit(&s->scanned, "<secret "));
        TRY(buf_str(&s->scanned, best->name));
        TRY(buf_byte(&s->scanned, '>'));
        i = end;
    }
    TRY(host_out_write(s->next, buf_view(&s->scanned)));
    size_t left = buf.len - i;
    if (left > 0) mem_move(s->carry.ptr, &at(buf, i), left);
    s->carry.len = left;
    return OK;
}

static STATUS_MUST_USE Status host_scrub_write(void *ctx, Str bytes) {
    HostScrub *s = ctx;
    if (s->secrets.len == 0) return host_out_write(s->next, bytes);
    TRY(buf_str(&s->carry, bytes));
    return host_scrub_scan(s, false);
}

static STATUS_MUST_USE Status host_scrub_finish(HostScrub *s) {
    if (s->secrets.len == 0) return OK;
    return host_scrub_scan(s, true);
}

// `run`'s `re` or `not_re`, compiled, as the keep matches it.
typedef struct {
    RegexProgram program;
    RegexScratch scratch;
} HostPattern;

static bool host_pattern_matches(void *ctx, Str line, uint64_t *work) {
    HostPattern *p = ctx;
    RegexSearched got = regex_search(&p->program, &p->scratch, line, UINT64_MAX);
    *work += got.positions * p->program.insts.len + (got.skipped + 15) / 16;
    return got.found;
}

// What `host_open_regular` gives besides an fd: the path is not a regular
// file (a FIFO, a device, a folder).
enum { HOST_NOT_REGULAR = -1 };

// Opens `c`, which must be a regular file, for writing (made if missing,
// truncated) or reading: 0 with `*fd` set, HOST_NOT_REGULAR, or the errno.
// DRAGON: the run's deadline and the file system's boundaries. A FIFO or a
// device at the path would hold the open, and `shale` with it, until a reader
// or a writer came; a link planted at the last part would be followed.
// REMEDY: O_NONBLOCK so the open returns at once (a FIFO with no reader is
// ENXIO), O_NOFOLLOW so a link is refused (ELOOP), then fstat must say a
// regular file before O_NONBLOCK is cleared and the fd handed back.
static int host_open_regular(const char *c, bool write, int *fd) { // cstr: NUL-ended for the kernel
    int flags = write ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY;
    int got = open(c, flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0666);
    if (got < 0) return errno == ENXIO ? HOST_NOT_REGULAR : errno;
    struct stat st;
    if (fstat(got, &st) != 0) {
        int e = errno;
        close(got);
        return e;
    }
    if (!S_ISREG(st.st_mode)) {
        close(got);
        return HOST_NOT_REGULAR;
    }
    int fl = fcntl(got, F_GETFL, 0);
    if (fl >= 0) fcntl(got, F_SETFL, fl & ~O_NONBLOCK);
    *fd = got;
    return 0;
}

// ── `stdout_secret`'s file ──

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

// The file a program's stdout goes to while it runs: a temporary file beside
// the secret, mode 0600, renamed over it by `host_secret_store`, or removed
// (swap_file.c).
typedef struct {
    // The script's path, `keys:NAME`, for messages.
    Str shown;
    SwapFile file;
} HostSecretFile;

static STATUS_MUST_USE Status host_secret_open(Host *h, Run *r, Pos pos, const Value *v, bool replace, HostSecretFile *out) {
    memset(out, 0, sizeof *out);
    out->file.fd = -1;
    if (v->tag != VALUE_STRING) {
        return host_fail(r, pos, CLASS_TYPE, S("`run`'s `stdout_secret` is a path in a `secrets` mount, `\"keys:NAME\"`, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(v))));
    }
    Str shown = v->as.string;
    Str path = S("");
    TRY(paths_secret_target(&h->paths, &builtin_files_of(r)->path, pos, shown, &path));
    FileSystemErr ce = swap_file_target(&out->file, os_str_own(path));
    if (ce != FILE_SYSTEM_OK) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s} cannot be looked at: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(file_system_err_name(ce))));
    // DRAGON: the secrets rule. The path is a secrets mount's file, which
    // no message may name.
    // REMEDY: every stop below names the script's `shown`, `keys:NAME`,
    // never `path`.
    int err = 0;
    SwapThere there = swap_file_there(&out->file, &err);
    if (there == SWAP_NOT_FILE) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` {s} is there and is not a file"), MSG_ARGS(MSG_FROM_SCRIPT(shown)));
    if (there == SWAP_FILE && !replace) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` {s} is there already: `secret_replace = true` to replace it"), MSG_ARGS(MSG_FROM_SCRIPT(shown)));
    if (there == SWAP_UNSEEN) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s} cannot be looked at: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(file_system_err_name(file_system_err_of(err)))));
    int made = swap_file_open(&out->file, r->ctx.values);
    if (made == SWAP_NO_ROOM) return run_values_oom(r, pos);
    if (made != 0) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s}: the temporary file beside it cannot be made: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(host_errno_tag(made))));
    out->shown = shown;
    return OK;
}

// Stops the run when the program printed past HOST_SECRET_MAX, however it
// ended: one held to the cap by RLIMIT_FSIZE ends by SIGXFSZ (or its write
// fails) with one byte past it written.
static STATUS_MUST_USE Status host_secret_refuse_over(Run *r, Pos pos, HostSecretFile *s, Str program) {
    off_t end = lseek(s->file.fd, 0, SEEK_END);
    if (end < 0) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s}: what `{s}` printed cannot be measured: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(s->shown), MSG_FROM_SCRIPT(program), MSG_FROM_HOST(host_errno_tag(errno))));
    uint64_t size = (uint64_t)end;
    if (size > HOST_SECRET_MAX) {
        return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: `{s}` printed {d} bytes, past the {d} `secrets` reads: nothing is stored in {s}"), MSG_ARGS(MSG_FROM_SCRIPT(program), MSG_FROM_HOST(run_unum(r, size)), MSG_FROM_HOST(S("65536")), MSG_FROM_SCRIPT(s->shown)));
    }
    return OK;
}

// Renames what the program printed over the secret, giving its bytes. Past
// the cap was refused before (`host_secret_refuse_over`).
static STATUS_MUST_USE Status host_secret_store(Run *r, Pos pos, HostSecretFile *s, Str program, bool replace, uint64_t *bytes) {
    off_t end = lseek(s->file.fd, 0, SEEK_END);
    if (end < 0) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s}: what `{s}` printed cannot be measured: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(s->shown), MSG_FROM_SCRIPT(program), MSG_FROM_HOST(host_errno_tag(errno))));
    uint64_t size = (uint64_t)end;
    if (size == 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: `{s}` exited 0 and printed nothing: nothing is stored in {s}"), MSG_ARGS(MSG_FROM_SCRIPT(program), MSG_FROM_SCRIPT(s->shown)));
    int e = 0;
    SwapEnd end_of = swap_file_commit(&s->file, replace, &e);
    if (end_of == SWAP_UNSYNCED) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s} cannot be synced: nothing is stored"), MSG_ARGS(MSG_FROM_SCRIPT(s->shown)));
    if (end_of != SWAP_DONE) {
        if (end_of == SWAP_TAKEN) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` {s} was made while `{s}` ran: nothing is stored; `secret_replace = true` to replace it"), MSG_ARGS(MSG_FROM_SCRIPT(s->shown), MSG_FROM_SCRIPT(program)));
        return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s} cannot be written: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(s->shown), MSG_FROM_HOST(host_errno_tag(e))));
    }
    *bytes = size;
    return OK;
}

static void host_secret_discard(HostSecretFile *s) {
    swap_file_discard(&s->file);
}

// ── `run` ──

static size_t host_count_lines(Str s) {
    size_t n = 0;
    for (size_t i = 0; i < s.len; i++) n += str_byte(s, i) == '\n';
    if (s.len > 0 && str_byte(s, s.len - 1) != '\n') n++;
    return n;
}

static Str host_ended_word(SpawnEnded e) {
    switch (e) {
    case SPAWN_EXIT: return S("exit");
    case SPAWN_TIMEOUT: return S("timeout");
    case SPAWN_SIGNAL: return S("signal");
    case SPAWN_INTERRUPTED: return S("interrupted");
    }
    c_bug("host_run: a program's end of no kind spawn.c says");
    // Never reached: TCC refuses a function that might return no value.
    return S("");
}

// The stop for a stream past what is left of the arena budget.
static STATUS_MUST_USE Status host_stream_over(Run *r, Pos pos, uint64_t most) {
    Buf m = buf_make(r->ctx.values);
    Status s = buf_lit(&m, "`run`: what it holds of the output is past the arena budget (");
    if (s == OK) s = buf_u64(&m, most);
    if (s == OK) s = buf_lit(&m, " bytes live): a smaller `before`, `head`, `tail` or `whole`");
    return host_budget_fail(r, pos, BUDGET_ARENA, &m, s, S("`run`: what it holds of the output is past the arena budget"));
}

// A budget's stop in place of `run`'s answer, saying how a program that also
// failed ended, and its output's end.
static STATUS_MUST_USE Status host_over_budget(Run *r, Pos pos, Str stage, uint64_t most, const SpawnOutcome *o, Str out) {
    Buf m = buf_make(r->ctx.values);
    Status s = buf_lit(&m, "`run`: stage `");
    if (s == OK) s = buf_str(&m, stage);
    if (s == OK) s = buf_lit(&m, "` took ");
    if (s == OK) s = buf_u64(&m, o->cpu_ms);
    if (s == OK) s = buf_lit(&m, " ms of CPU, over its `cpu_budget_ms` of ");
    if (s == OK) s = buf_u64(&m, most);
    if (s == OK) s = buf_lit(&m, " (");
    if (s == OK) s = buf_u64(&m, o->ms);
    if (s == OK) s = buf_lit(&m, " ms of wall time)");
    bool failed = o->ended != SPAWN_EXIT || o->code != 0;
    if (s == OK && failed) {
        s = buf_lit(&m, "; it also ended ");
        if (s == OK) s = buf_str(&m, host_ended_word(o->ended));
        if (s == OK && o->code != SPAWN_NO_CODE) s = buf_byte(&m, ' ');
        if (s == OK && o->code != SPAWN_NO_CODE) s = buf_u64(&m, (uint64_t)o->code);
        if (s == OK) s = buf_lit(&m, ", its output's end:\n");
        if (s == OK) s = buf_str(&m, str_sub(out, out.len > HOST_BUDGET_STOP_OUTPUT ? out.len - HOST_BUDGET_STOP_OUTPUT : 0, out.len));
    }
    return host_budget_fail(r, pos, BUDGET_CPU, &m, s, S("`run`: a stage is over its `cpu_budget_ms`"));
}

// What `run` holds while a program runs, released on every way out (a
// defer, in C): the stream's arena, the log and the secret's file.
typedef struct {
    Arena stream;
    bool has_stream;
    int log_fd;
    HostSecretFile secret;
    bool has_secret;
} HostRunHeld;

static void host_run_release(HostRunHeld *held) {
    if (held->has_secret) host_secret_discard(&held->secret);
    if (held->log_fd >= 0) close(held->log_fd);
    held->log_fd = -1;
    if (held->has_stream) arena_deinit(&held->stream);
    held->has_stream = false;
}

// Reads into `into` until it is full or the file ends: 0 with `*len` read,
// or the errno.
static int host_read_up_to(int fd, Bytes into, size_t *len) {
    *len = 0;
    while (*len < into.len) {
        ssize_t got = read(fd, &at(into, *len), into.len - *len);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) return errno;
        if (got == 0) return 0;
        *len += (size_t)got;
    }
    return 0;
}

// A secret's value: the file at the script's `given` (a secrets mount's
// path) read whole, at most HOST_SECRET_MAX bytes, space trimmed from both
// ends. A file that is not there, not a file, too long or empty stops the
// run.
// DRAGON: the secrets rule. `path` is a secret's file and `bytes` a secret;
// neither may reach a message or the record.
// REMEDY: the stops name the script's own path, `given`; the value goes to
// the environment and the scrubber alone.
static STATUS_MUST_USE Status host_secret_read(Host *h, Run *r, Pos pos, Str name, Str given, Str *value) {
    Str path = S("");
    TRY(paths_secret_path(&h->paths, &builtin_files_of(r)->path, pos, builtin_who(r, BUILTIN_HOST), given, &path));
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr ce = file_system_cstr(&cp, os_str_own(path), &c);
    // Neither waited on (a FIFO) nor followed (a link): host_open_regular.
    int fd = -1;
    int err = ce == FILE_SYSTEM_OK ? host_open_regular(c, false, &fd) : ENAMETOOLONG;
    if (err == HOST_NOT_REGULAR) return host_fail(r, pos, CLASS_REFUSED, S("`run`: the secret `{s}`'s file {s} is there and is not a file"), MSG_ARGS(MSG_FROM_SCRIPT(name), MSG_FROM_SCRIPT(given)));
    char *bytes = NULL;
    size_t len = 0;
    if (err == 0) {
        bytes = arena_alloc(r->ctx.values, HOST_SECRET_MAX + 1, 1);
        if (bytes == NULL) {
            close(fd);
            return run_values_oom(r, pos);
        }
        Bytes into = {bytes, HOST_SECRET_MAX + 1};
        err = host_read_up_to(fd, into, &len);
        close(fd);
    }
    Str why = S("");
    if (err != 0) why = file_system_err_name(file_system_err_of(err));
    if (err == 0 && len > HOST_SECRET_MAX) why = S("StreamTooLong");
    if (why.len > 0) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`: the secret `{s}`'s file {s} cannot be read: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(name), MSG_FROM_SCRIPT(given), MSG_FROM_HOST(why)));
    Str v = {bytes, len};
    while (v.len > 0 && mem_find_byte(" \t\r\n", str_byte(v, 0), 4) != NULL) v = str_sub(v, 1, v.len);
    while (v.len > 0 && mem_find_byte(" \t\r\n", str_byte(v, v.len - 1), 4) != NULL) v = str_sub(v, 0, v.len - 1);
    if (v.len == 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: the secret `{s}`'s file {s} is empty"), MSG_ARGS(MSG_FROM_SCRIPT(name), MSG_FROM_SCRIPT(given)));
    OsStr checked;
    if (!os_str_of(v, &checked)) return host_fail(r, pos, CLASS_REFUSED, S("`run`: the secret `{s}`'s file {s} holds a NUL byte, which no environment can carry"), MSG_ARGS(MSG_FROM_SCRIPT(name), MSG_FROM_SCRIPT(given)));
    *value = v;
    return OK;
}

// Whether the inherited `K=V` is one that `env` or `secrets` sets anew.
static bool host_env_overridden(Str inherited, const HostEnvOpt *env, const HostEnvOpt *secrets) {
    const char *eq = str_find_byte(inherited, '=');
    Str k = eq != NULL ? str_sub(inherited, 0, (size_t)(eq - inherited.ptr)) : inherited;
    for (size_t j = 0; j < env->names.len; j++) {
        if (str_eq(at(env->names, j), k)) return true;
    }
    for (size_t j = 0; j < secrets->names.len; j++) {
        if (str_eq(at(secrets->names, j), k)) return true;
    }
    return false;
}

// `K=V`, in the values' arena, of a name and a value read through `os_str`
// (host_env_opt) or a secret's checked bytes (host_secret_read).
static STATUS_MUST_USE Status host_env_pair(Run *r, Pos pos, Str k, Str v, OsStr *out) {
    Buf kv = buf_make(r->ctx.values);
    if (buf_str(&kv, k) != OK || buf_byte(&kv, '=') != OK || buf_str(&kv, v) != OK) return run_values_oom(r, pos);
    *out = os_str_own(buf_view(&kv));
    return OK;
}

// The environment a program gets, `run`'s or a job's: the inherited one,
// less what `env` and `secrets` set, then `env`'s, then the secrets' (read
// from their files). `shown` gets ` K=V` and ` K=<secret path>` for the
// record.
static STATUS_MUST_USE Status host_run_env(Host *h, Run *r, Pos pos, const HostEnvOpt *envs, const HostEnvOpt *secret_paths, Buf *shown, SpawnStrs *env_out, HostSecrets *secrets_out) {
    Arena *a = r->ctx.values;
    size_t extra = envs->names.len;
    size_t nsecrets = secret_paths->names.len;
    OsStr *env = ARENA_ARRAY(a, OsStr, h->env.len + extra + nsecrets);
    if (env == NULL && h->env.len + extra + nsecrets > 0) return run_values_oom(r, pos);
    HostSecret *secrets = ARENA_ARRAY(a, HostSecret, nsecrets);
    if (secrets == NULL && nsecrets > 0) return run_values_oom(r, pos);
    SLICE_TYPE(EnvRoom, OsStr);
    SLICE_TYPE(SecretRoom, HostSecret);
    EnvRoom room = {env, h->env.len + extra + nsecrets};
    SecretRoom sroom = {secrets, nsecrets};
    size_t n = 0;
    for (size_t i = 0; i < h->env.len; i++) {
        OsStr e = at(h->env, i);
        if (!host_env_overridden(e.s, envs, secret_paths)) at(room, n++) = e;
    }
    for (size_t j = 0; j < extra; j++) {
        Str k = at(envs->names, j);
        Str v = at(envs->values, j);
        TRY(host_env_pair(r, pos, k, v, &at(room, n)));
        n++;
        if (buf_byte(shown, ' ') != OK || buf_str(shown, k) != OK || buf_byte(shown, '=') != OK || buf_str(shown, v) != OK) return run_values_oom(r, pos);
    }
    for (size_t j = 0; j < nsecrets; j++) {
        Str k = at(secret_paths->names, j);
        Str given = at(secret_paths->values, j);
        Str v = S("");
        TRY(host_secret_read(h, r, pos, k, given, &v));
        TRY(host_env_pair(r, pos, k, v, &at(room, n)));
        n++;
        at(sroom, j).name = k;
        at(sroom, j).value = v;
        if (buf_byte(shown, ' ') != OK || buf_str(shown, k) != OK || buf_lit(shown, "=<secret ") != OK || buf_str(shown, given) != OK || buf_byte(shown, '>') != OK) return run_values_oom(r, pos);
    }
    env_out->ptr = env;
    env_out->len = n;
    secrets_out->ptr = secrets;
    secrets_out->len = nsecrets;
    return OK;
}

// `run`'s stdin: a string, or a `whole` run's result, whose `out` is the
// program's output whole. `*has` false for none.
static STATUS_MUST_USE Status host_run_stdin(Run *r, Pos pos, const RunOpts *o, bool *has, Str *out) {
    *has = false;
    if (!host_given(&o->stdin_v)) return OK;
    const Value *v = &o->stdin_v;
    if (v->tag == VALUE_STRING) {
        *has = true;
        *out = v->as.string;
        return OK;
    }
    if (v->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`run`'s stdin is a string or a run's result, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(v))));
    Table *t = v->as.table;
    if (t->kind != TABLE_MAP) return host_fail_plain(r, pos, CLASS_TYPE, S("`run`'s stdin is a string or a run's result, not a list"));
    Value whole;
    Value got;
    if (ValueMap_get(&t->map, S("whole"), &whole) && whole.tag == VALUE_BOOL && whole.as.boolean && ValueMap_get(&t->map, S("out"), &got) && got.tag == VALUE_STRING) {
        *has = true;
        *out = got.as.string;
        return OK;
    }
    return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`'s stdin is a kept result: its `out` is not the program's output (lines left out, lines cut); run it with `whole = N` to pipe it"));
}

// The answer's `argv`, `code`, `ended`, `ms` and `cpu_ms`.
static STATUS_MUST_USE Status host_run_answer(Run *r, Pos pos, SpawnStrs argv, const SpawnOutcome *o, Table **out) {
    Table *t = NULL;
    TRY(run_new_table(r, pos, &t));
    t->kind = TABLE_MAP;
    Table *list = NULL;
    TRY(run_new_table(r, pos, &list));
    list->kind = TABLE_LIST;
    for (size_t i = 0; i < argv.len; i++) {
        Value s;
        value_string(&s, at(argv, i).s);
        TRY(run_list_push(r, pos, list, &s));
    }
    Value lv;
    value_table(&lv, list);
    TRY(run_map_put(r, pos, t, S("argv"), &lv));
    if (o->code != SPAWN_NO_CODE && o->ended == SPAWN_EXIT) TRY(run_map_put_int(r, pos, t, S("code"), o->code));
    TRY(run_map_put_str(r, pos, t, S("ended"), o->ended == SPAWN_INTERRUPTED ? S("signal") : host_ended_word(o->ended)));
    TRY(run_map_put_int(r, pos, t, S("ms"), (int64_t)o->ms));
    TRY(run_map_put_int(r, pos, t, S("cpu_ms"), (int64_t)o->cpu_ms));
    *out = t;
    return OK;
}

// The record of a program run: `run ARGV[ ENV] [(want: W)] -> ENDED[ CODE]
// (MS ms)[, stdout to ...]`.
static void host_run_record(Host *h, Run *r, Str joined, Str shown_env, const RunOpts *o, const SpawnOutcome *out, Str secret_said) {
    Buf b = buf_make(r->ctx.values);
    Status s = buf_lit(&b, "run ");
    if (s == OK) s = buf_str(&b, joined);
    if (s == OK) s = buf_str(&b, shown_env);
    if (s == OK && host_given(&o->want)) s = buf_lit(&b, " (want: ");
    if (s == OK && host_given(&o->want)) s = buf_str(&b, o->want.as.string);
    if (s == OK && host_given(&o->want)) s = buf_byte(&b, ')');
    if (s == OK) s = buf_lit(&b, " -> ");
    if (s == OK) s = buf_str(&b, host_ended_word(out->ended));
    if (s == OK && out->code != SPAWN_NO_CODE) s = buf_byte(&b, ' ');
    if (s == OK && out->code != SPAWN_NO_CODE) s = buf_u64(&b, (uint64_t)out->code);
    if (s == OK) s = buf_lit(&b, " (");
    if (s == OK) s = buf_u64(&b, out->ms);
    if (s == OK) s = buf_lit(&b, " ms)");
    if (s == OK) s = buf_str(&b, secret_said);
    if (s == OK) host_record(h, buf_view(&b));
}

// A path the host itself writes for the script (`run`'s log, a job's log and
// record, `suffix` added to the script's path first), as an OS path, once
// `may_change` let it change.
// DRAGON: the file system's boundaries and the undo journal. A file the host
// opens for writing outside `change_may` would write through a read-only mount
// and past `--undo-on-stop`'s journal.
// REMEDY: every such open takes its path from here; test_run_guards plants a
// log into a read-only mount and one put back by an undo.
static STATUS_MUST_USE Status host_change_os_path(Host *h, Run *r, Pos pos, Str given, Str suffix, Str *out) {
    RunFiles *rs = builtin_files_of(r);
    Str full = given;
    if (suffix.len > 0) {
        Buf b = buf_make(r->ctx.values);
        if (buf_str(&b, given) != OK || buf_str(&b, suffix) != OK) return run_values_oom(r, pos);
        full = buf_view(&b);
    }
    CheckedPath p;
    TRY(path_arg(&rs->path, pos, builtin_who(r, BUILTIN_HOST), full, false, &p));
    TRY(change_may(r, pos, &p));
    return paths_absolute(&h->paths, &rs->path, pos, mounts_trim_slash(p.full.s), out);
}

// The stop when a sink refused the output: the stream's arena past its cap
// is the arena budget's stop (`most` the bytes it had), else the values'
// arena ran out.
static STATUS_MUST_USE Status host_stream_refused(Run *r, Pos pos, const Arena *sa, uint64_t most) {
    return sa->hit ? host_stream_over(r, pos, most) : run_values_oom(r, pos);
}

// A program's argv, `run`'s or `job_start`'s (`who`): a list of strings, one
// at least. Both callers' stops say the same: an example for a value not a
// list, and why for an argument not a string.
static STATUS_MUST_USE Status host_argv_arg(Run *r, Pos pos, Str who, const Value *av, SpawnStrs *out) {
    Str example = S(", `{\"zig\", \"build\"}`");
    Str why = S(": every argument is one, written as the program takes it");
    if (av->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`{s}`'s argv is a list of strings{s}, not {s}"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_HOST(example), MSG_FROM_HOST(value_type_name(av))));
    Table *argv_t = av->as.table;
    if (argv_t->kind == TABLE_MAP) return host_fail(r, pos, CLASS_TYPE, S("`{s}`'s argv is a list of strings, not a map"), MSG_ARGS(MSG_FROM_HOST(who)));
    if (argv_t->kind == TABLE_EMPTY || argv_t->list.len == 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`{s}` of an empty argv: name the program"), MSG_ARGS(MSG_FROM_HOST(who)));
    size_t n = argv_t->list.len;
    OsStr *argv = ARENA_ARRAY(r->ctx.values, OsStr, n);
    if (argv == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(ArgvRoom, OsStr);
    ArgvRoom room = {argv, n};
    for (size_t i = 0; i < n; i++) {
        const Value *v = &at(argv_t->list, i);
        if (v->tag != VALUE_STRING) {
            return host_fail(r, pos, CLASS_TYPE, S("`{s}`'s argv[{d}] is {s}, not a string{s}"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_HOST(run_unum(r, i + 1)), MSG_FROM_HOST(value_type_name(v)), MSG_FROM_HOST(why)));
        }
    }
    // A NUL is refused once every item is known to be a string.
    for (size_t i = 0; i < n; i++) {
        Str what = S("");
        TRY(paths_cat(&builtin_files_of(r)->path, pos, S("argument "), run_unum(r, i + 1), S(""), &what));
        TRY(os_str(r, pos, who, what, at(argv_t->list, i).as.string, &at(room, i)));
    }
    out->ptr = argv;
    out->len = n;
    return OK;
}

// The argv as one line, its parts by spaces: the time line's and the
// records' name for a program.
static STATUS_MUST_USE Status host_argv_joined(Run *r, Pos pos, SpawnStrs argv, Str *out) {
    Buf jb = buf_make(r->ctx.values);
    for (size_t i = 0; i < argv.len; i++) {
        if ((i > 0 && buf_byte(&jb, ' ') != OK) || buf_str(&jb, at(argv, i).s) != OK) return run_values_oom(r, pos);
    }
    *out = buf_view(&jb);
    return OK;
}

// Opens `run`'s log, the script's `shown`, for writing. The log is a change
// the host makes for the script: `may_change` first, then an open that
// neither waits on a FIFO nor follows a link (both DRAGONs at their
// functions).
static STATUS_MUST_USE Status host_run_open_log(Host *h, Run *r, Pos pos, Str shown, int *fd) {
    Str path = S("");
    TRY(host_change_os_path(h, r, pos, shown, S(""), &path));
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr ce = file_system_cstr(&cp, os_str_own(path), &c);
    int opened = ce == FILE_SYSTEM_OK ? host_open_regular(c, true, fd) : ENAMETOOLONG;
    if (opened == HOST_NOT_REGULAR) return host_fail(r, pos, CLASS_REFUSED, S("`run`: its log {s} is there and is not a file"), MSG_ARGS(MSG_FROM_SCRIPT(shown)));
    if (opened != 0) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`: its log {s} cannot be written: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(file_system_err_name(file_system_err_of(opened)))));
    return OK;
}

// Where a program's output streams, in the stream's arena: the scrubber,
// then the log and the keep or `whole`'s buffer. Made in place
// (`host_run_sinks`), since each points at the next.
typedef struct {
    HostOut out;
    Buf whole;
    Keep keeper;
    // `re` and `not_re`, as the keeper matches them.
    HostPattern re;
    HostPattern not_re;
    HostScrub scrub;
} HostRunSinks;

// `run`'s `re` or `not_re` (`name`) compiled into `p`, in the values' arena,
// and `kp` the keep's hold of it. A pattern the regex refuses stops the run,
// naming its byte.
static STATUS_MUST_USE Status host_run_pattern(Run *r, Pos pos, Str name, Str pattern, bool nocase, HostPattern *p, KeepPattern *kp) {
    Arena *a = r->ctx.values;
    RegexOptions ro = {nocase};
    RegexRefusal why;
    memset(&why, 0, sizeof why);
    if (regex_compile(a, pattern, ro, &p->program, &why) != OK) {
        if (!why.refused) return run_values_oom(r, pos);
        return host_fail(r, pos, CLASS_ARGUMENT, S("`run`'s `{s}`, at its byte {d}: {s}"), MSG_ARGS(MSG_FROM_HOST(name), MSG_FROM_HOST(run_unum(r, (uint64_t)why.at + 1)), MSG_FROM_HOST(why.message)));
    }
    if (regex_scratch_make(a, &p->program, &p->scratch) != OK) return run_values_oom(r, pos);
    KeepPattern hold = {p, host_pattern_matches};
    *kp = hold;
    return OK;
}

// Makes the sinks in place: the log opened when given, then `whole`'s buffer
// or the keep (its patterns compiled), and the scrubber in front of them.
static STATUS_MUST_USE Status host_run_sinks(Host *h, Run *r, Pos pos, const RunOpts *o, HostRunHeld *held, HostSecrets secrets, HostRunSinks *s) {
    Arena *sa = &held->stream;
    memset(s, 0, sizeof *s);
    s->out.log_fd = -1;
    if (host_given(&o->log)) {
        TRY(host_run_open_log(h, r, pos, o->log.as.string, &held->log_fd));
        s->out.log_fd = held->log_fd;
    }
    s->whole = buf_make(sa);
    switch (o->keep) {
    case HOST_KEEP_WHOLE:
        s->out.whole = &s->whole;
        s->out.whole_max = o->whole;
        break;
    case HOST_KEEP_LINES:
    case HOST_KEEP_SECRET: {
        KeepOptions ko;
        keep_options_default(&ko);
        ko.head = (size_t)o->head;
        ko.tail = (size_t)o->tail;
        ko.width = (size_t)o->width;
        ko.before = (size_t)o->before;
        ko.after = (size_t)o->after;
        if (host_given(&o->re)) TRY(host_run_pattern(r, pos, S("re"), o->re.as.string, o->nocase, &s->re, &ko.re));
        if (host_given(&o->not_re)) TRY(host_run_pattern(r, pos, S("not_re"), o->not_re.as.string, o->nocase, &s->not_re, &ko.not_re));
        if (keep_init(&s->keeper, sa, ko) != OK) return run_values_oom(r, pos);
        s->out.keeper = &s->keeper;
        break;
    }
    }
    s->scrub.secrets = secrets;
    s->scrub.carry = buf_make(sa);
    s->scrub.scanned = buf_make(sa);
    s->scrub.next = &s->out;
    return OK;
}

// Starts the program as `spec` says and streams its output into the sinks
// until it ends. One that could not be started stops the run, saying why.
static STATUS_MUST_USE Status host_run_spawn(Run *r, Pos pos, const SpawnSpec *spec, HostRunSinks *s, const Arena *sa, uint64_t most, SpawnOutcome *outcome) {
    SpawnSink sink = {&s->scrub, host_scrub_write};
    Str argv0 = at(spec->argv, 0).s;
    memset(outcome, 0, sizeof *outcome);
    r->host_runs++;
    switch (spawn_run(r->ctx.values, spec, sink, outcome)) {
    case SPAWN_OK: break;
    case SPAWN_OUT_OF_MEMORY: return run_values_oom(r, pos);
    case SPAWN_SINK_STOPPED: return host_stream_refused(r, pos, sa, most);
    case SPAWN_NOT_FOUND: return host_fail(r, pos, CLASS_HOST, S("`run`: `{s}` is not found{s}"), MSG_ARGS(MSG_FROM_SCRIPT(argv0), MSG_FROM_HOST(str_find_byte(argv0, '/') == NULL ? S(" along PATH") : S(""))));
    case SPAWN_NOT_RUNNABLE: return host_fail(r, pos, CLASS_HOST, S("`run`: `{s}` is there but cannot be run (its permissions, or not a program)"), MSG_ARGS(MSG_FROM_SCRIPT(argv0)));
    case SPAWN_NO_CWD: return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: its cwd is not a folder that can be entered: {s}"), MSG_ARGS(MSG_FROM_HOST(file_system_err_name(file_system_err_of(outcome->err)))));
    case SPAWN_REFUSED: return host_fail(r, pos, CLASS_HOST, S("`run`: the program could not be started (a pipe, a fork or a pidfd refused): {s}"), MSG_ARGS(MSG_FROM_HOST(file_system_err_name(file_system_err_of(outcome->err)))));
    }
    if (host_scrub_finish(&s->scrub) != OK) return host_stream_refused(r, pos, sa, most);
    return OK;
}

// `stdout_secret`'s end, once the program's: refused past its cap however
// the program ended, stored only on exit 0 (so a failed program leaves
// nothing half made, `*stored` false), and `said` for the record (`, stdout
// to keys:x (N bytes)`).
static STATUS_MUST_USE Status host_run_secret_end(Run *r, Pos pos, HostRunHeld *held, Str argv0, bool replace, const SpawnOutcome *o, Buf *said, bool *stored, uint64_t *bytes) {
    TRY(host_secret_refuse_over(r, pos, &held->secret, argv0));
    if (o->ended == SPAWN_EXIT && o->code == 0) {
        TRY(host_secret_store(r, pos, &held->secret, argv0, replace, bytes));
        *stored = true;
    }
    Status s = buf_lit(said, ", stdout to ");
    if (s == OK) s = buf_str(said, held->secret.shown);
    if (s == OK && *stored) s = buf_lit(said, " (");
    if (s == OK && *stored) s = buf_u64(said, *bytes);
    if (s == OK) s = buf_str(said, *stored ? S(" bytes)") : S(" (not written)"));
    if (s != OK) buf_clear(said);
    return OK;
}

// The log's record, `log PATH`, which says so when a write failed (fsync's
// EIO among them) and the log is short.
static void host_run_log_record(Host *h, Run *r, int fd, Str shown, bool failed) {
    if (fsync(fd) != 0 && errno == EIO) failed = true;
    Buf lb = buf_make(r->ctx.values);
    if (buf_lit(&lb, "log ") == OK && buf_str(&lb, shown) == OK && buf_str(&lb, failed ? S(": a write failed, the log is short") : S("")) == OK) host_record(h, buf_view(&lb));
}

// The answer's `out` and its line counts, by the keep: `whole`'s buffer
// whole (refused past `whole = N`), or what the keep kept, its filters'
// work charged to the caller's steps. `*kept_out` is the `out` given.
static STATUS_MUST_USE Status host_run_out(Run *r, Pos pos, const RunOpts *o, HostRunSinks *s, const Arena *sa, uint64_t most, Table *t, Str *kept_out) {
    Arena *a = r->ctx.values;
    switch (o->keep) {
    case HOST_KEEP_WHOLE: {
        if (s->out.whole_over) {
            return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: `whole = {d}` is a byte limit, and the output was {d} bytes: raise it, or keep lines with `head` and `tail`, which take any size"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, o->whole)), MSG_FROM_HOST(run_unum(r, s->out.bytes))));
        }
        int64_t lines = (int64_t)host_count_lines(buf_view(&s->whole));
        if (!arena_dup(a, buf_view(&s->whole), kept_out)) return run_values_oom(r, pos);
        TRY(run_map_put_str(r, pos, t, S("out"), *kept_out));
        TRY(run_map_put_bool(r, pos, t, S("whole"), true));
        TRY(run_map_put_int(r, pos, t, S("total_lines"), lines));
        TRY(run_map_put_int(r, pos, t, S("matched_lines"), lines));
        TRY(run_map_put_int(r, pos, t, S("kept_lines"), lines));
        break;
    }
    case HOST_KEEP_LINES:
    case HOST_KEEP_SECRET: {
        Kept kept;
        if (keep_finish(&s->keeper, &kept) != OK) return host_stream_refused(r, pos, sa, most);
        TRY(run_charge(r, pos, kept.work));
        if (!arena_dup(a, kept.out, kept_out)) return run_values_oom(r, pos);
        TRY(run_map_put_str(r, pos, t, S("out"), *kept_out));
        TRY(run_map_put_int(r, pos, t, S("total_lines"), (int64_t)kept.total));
        TRY(run_map_put_int(r, pos, t, S("matched_lines"), (int64_t)kept.matched));
        TRY(run_map_put_int(r, pos, t, S("kept_lines"), (int64_t)kept.kept));
        break;
    }
    }
    return OK;
}

// Runs the program, streams its output through the scrubber to the log and
// the keep, and gives the answer, once `host_run_program` has read the
// options.
static STATUS_MUST_USE Status host_run_spawned(Host *h, Run *r, Pos pos, SpawnStrs argv, const RunOpts *o, HostRunHeld *held, Value *out) {
    Arena *a = r->ctx.values;
    // The guards' order is the DRAGON at host_guard_program.
    HostProgram prog = {S("run"), argv, &o->cwd, &o->env, &o->secrets, host_given(&o->want)};
    Str cwd = S("");
    TRY(host_guard_program(h, r, pos, &prog, &cwd));

    Buf shown_env = buf_make(a);
    SpawnStrs env = {NULL, 0};
    HostSecrets secrets = {NULL, 0};
    TRY(host_run_env(h, r, pos, &o->env, &o->secrets, &shown_env, &env, &secrets));
    bool has_stdin = false;
    Str stdin_bytes = S("");
    TRY(host_run_stdin(r, pos, o, &has_stdin, &stdin_bytes));
    bool secret = o->keep == HOST_KEEP_SECRET;
    if (secret) {
        TRY(host_secret_open(h, r, pos, &o->stdout_secret, o->secret_replace, &held->secret));
        held->has_secret = true;
    }

    // The stream's arena, capped at what is left of the run's arena budget:
    // what grows as output arrives grows by how the pipe's reads fall.
    uint64_t most = run_sat_sub(r->ctx.budgets.arena_bytes, a->held);
    arena_init(&held->stream, (size_t)most, &r->ctx.refused);
    held->has_stream = true;
    HostRunSinks sinks;
    TRY(host_run_sinks(h, r, pos, o, held, secrets, &sinks));

    SpawnSpec spec;
    spawn_spec_default(&spec);
    spec.argv = argv;
    // The folder is a CheckedPath's, made absolute (host_guard_program).
    spec.cwd = os_str_own(cwd);
    spec.env = env;
    spec.stdin_bytes = has_stdin ? &stdin_bytes : NULL;
    spec.stdout_fd = secret ? held->secret.file.fd : -1;
    // DRAGON: the budgets. The secret's file is the program's stdout, which
    // no pipe bounds: it could fill the disk before the cap is checked at
    // exit.
    // REMEDY: RLIMIT_FSIZE at the cap plus one, set in the child before exec
    // (spawn.c), so the kernel stops the file one byte past the cap and
    // `host_secret_refuse_over` sees it; test_run_guards prints 10 MB at it.
    if (secret) spec.file_max = HOST_SECRET_MAX + 1;
    if (o->nice != HOST_UNSET) spec.nice = (int)o->nice;
    spec.timeout_ms = o->timeout_ms;
    SpawnOutcome outcome;
    TRY(host_run_spawn(r, pos, &spec, &sinks, &held->stream, most, &outcome));

    // The output charged as bulk work and as read bytes.
    TRY(builtin_charge_bytes(r, pos, sinks.out.bytes));
    r->read_bytes += sinks.out.bytes;
    Str joined = S("");
    TRY(host_argv_joined(r, pos, argv, &joined));
    host_time_ran(&h->time, h->paths.arena, joined, outcome.ms);
    Str argv0 = at(argv, 0).s;
    bool secret_stored = false;
    uint64_t secret_bytes = 0;
    Buf said = buf_make(a);
    if (secret) TRY(host_run_secret_end(r, pos, held, argv0, o->secret_replace, &outcome, &said, &secret_stored, &secret_bytes));
    host_run_record(h, r, joined, buf_view(&shown_env), o, &outcome, buf_view(&said));
    if (host_given(&o->log)) host_run_log_record(h, r, held->log_fd, o->log.as.string, sinks.out.log_failed);

    Table *t = NULL;
    TRY(host_run_answer(r, pos, argv, &outcome, &t));
    Str kept_out = S("");
    TRY(host_run_out(r, pos, o, &sinks, &held->stream, most, t, &kept_out));
    if (secret) {
        TRY(run_map_put_bool(r, pos, t, S("secret_written"), secret_stored));
        TRY(run_map_put_int(r, pos, t, S("secret_bytes"), (int64_t)secret_bytes));
    }
    if (r->read_bytes > r->ctx.budgets.read_bytes) return run_budget_stop(&r->ctx, pos, BUDGET_READ_BYTES);
    // An interrupted `shale` stops here, once the program's group is gone.
    if (outcome.ended == SPAWN_INTERRUPTED) return host_fail(r, pos, CLASS_HOST, S("`run`: shale was told to stop (TERM or HUP) while `{s}` ran; it was ended"), MSG_ARGS(MSG_FROM_SCRIPT(argv0)));
    if (outcome.cpu_ms > o->cpu_budget_ms) return host_over_budget(r, pos, host_given(&o->stage) ? o->stage.as.string : joined, o->cpu_budget_ms, &outcome, kept_out);
    value_table(out, t);
    return OK;
}

// `host("run", argv, opts)`.
static STATUS_MUST_USE Status host_run_program(Host *h, Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 2) {
        return host_fail(r, pos, CLASS_ARGUMENT, S("`run` takes argv and its options, `run({{\"zig\", \"build\"}}, {{timeout_ms = ..., tail = 20}})`, given {d} arguments"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, args.len))));
    }
    SpawnStrs argv = {NULL, 0};
    TRY(host_argv_arg(r, pos, S("run"), &at(args, 0), &argv));
    RunOpts o;
    TRY(host_read_opts(r, pos, &at(args, 1), &o));
    TRY(run_charge(r, pos, argv.len));
    if (r->host_runs >= r->ctx.budgets.programs) return run_budget_stop(&r->ctx, pos, BUDGET_PROGRAMS);
    HostRunHeld held;
    memset(&held, 0, sizeof held);
    held.log_fd = -1;
    held.secret.file.fd = -1;
    Status s = host_run_spawned(h, r, pos, argv, &o, &held, out);
    host_run_release(&held);
    return s;
}

// `host("sleep", ms)`: a wait that is no program, so it spends none of the
// `programs` budget.
static STATUS_MUST_USE Status host_sleep(Host *h, Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 1) return host_fail(r, pos, CLASS_ARGUMENT, S("`sleep` takes one argument, the ms, given {d}"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, args.len))));
    const Value *v = &at(args, 0);
    if (v->tag != VALUE_INT) return host_fail(r, pos, CLASS_TYPE, S("`sleep`'s ms must be an integer, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(v))));
    int64_t ms = v->as.integer;
    if (ms < 0 || ms > HOST_SLEEP_MAX_MS) return host_fail(r, pos, CLASS_ARGUMENT, S("`sleep` takes 0 to {d} ms, not {d}"), MSG_ARGS(MSG_FROM_HOST(S("600000")), MSG_FROM_HOST(run_num(r, ms))));
    TRY(run_charge(r, pos, 1));
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
    h->time.sleep_ms += (uint64_t)ms;
    value_nil(out);
    return OK;
}
