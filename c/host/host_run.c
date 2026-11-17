// host_run.c: the Linux host's `run`, the port of Zig Shale's
// src/shale_host_run.zig, and what it shares with the jobs from
// src/shale_host_linux.zig: the host itself (`Host`: its paths, the
// environment a program inherits, the records `--verbose` lists and where
// the run's time went), the refusals (`refuse`), the option readers, the
// scrubber of secrets (`Scrub`) and the output's sinks (`Out`).
//
// A program's output streams through the scrubber, then to the log, and to
// the keep (keep.c) or `whole`'s buffer, all in an arena of the stream's own
// capped at what is left of the run's arena budget, so a tail of 3 over a
// 10 GB log holds three lines; only the answer goes to the values' arena.
//
// Include spawn.c, keep.c, paths.c and builtins_store.c first.

#include <sys/stat.h>

// The most a `sleep` waits in one call: ten minutes.
enum { HOST_SLEEP_MOST_MS = 600000 };

// With `stdout_secret`, `out` is stderr's last lines, this many.
enum { HOST_SECRET_ERR_TAIL = 20 };

// The most bytes `stdout_secret` stores, and `secrets` reads of a file.
enum { HOST_SECRET_MOST = 64 * 1024 };

// What a failed program's output keeps in a budget's stop, from its end.
enum { HOST_BUDGET_STOP_OUTPUT = 2000 };

// The most of an argv the time line keeps, so three programs fit one line.
enum { HOST_ARGV_MOST = 80 };

#define HOST_ELIDED "\xe2\x80\xa6"

// One of the slowest programs: its ms and its argv, cut (`host_keep_ends`).
typedef struct {
    uint64_t ms;
    Str argv;
} HostSlow;

SLICE_TYPE(HostSlows, HostSlow);

// Where a run's time went, for the line after its report: Zig's `Time`.
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
// is another), where its record is, and how a stop names it. Zig's
// `Started`.
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

// The Linux host the CLI hands the core: Zig's `Host`. Its arena is
// `paths.arena`, which outlives the run's values.
typedef struct {
    Paths paths;
    // The environment a program inherits, `K=V` each, `ZIG_PROGRESS` out.
    SpawnStrs env;
    HostTime time;
    // One line each, a program run or a log written: `--verbose`'s `── ran`.
    HostRecords records;
    // The program a job runs under (`shale --reap`); none means no jobs.
    bool has_reaper;
    Str reaper;
    // The jobs this run started, for a stop to end (`host_jobs_stop_with_run`).
    HostStartedJobs started;
} Host;

static void host_make(Host *h, SpawnStrs env) {
    h->env = env;
    memset(&h->time, 0, sizeof h->time);
    h->records = HostRecords_make(h->paths.arena);
    h->has_reaper = false;
    h->started = HostStartedJobs_make(h->paths.arena);
}

// ── the time line ──

// An argv past HOST_ARGV_MOST bytes keeps both its ends around an ellipsis,
// HOST_ARGV_MOST bytes in all, neither cut inside a UTF-8 sequence: the
// start names the program, the end the arguments that tell runs apart.
static bool host_keep_ends(Arena *a, Str argv, Str *out) {
    if (argv.len <= HOST_ARGV_MOST) return arena_dup(a, argv, out);
    size_t room = HOST_ARGV_MOST - (sizeof HOST_ELIDED - 1);
    size_t head = room / 2;
    while (head > 0 && (str_byte(argv, head) & 0xC0) == 0x80) head--;
    size_t tail = argv.len - (room - room / 2);
    while (tail < argv.len && (str_byte(argv, tail) & 0xC0) == 0x80) tail++;
    Buf b = buf_make(a);
    if (buf_str(&b, str_sub(argv, 0, head)) != OK || buf_lit(&b, HOST_ELIDED) != OK || buf_str(&b, str_sub(argv, tail, argv.len)) != OK) return false;
    *out = buf_view(&b);
    return true;
}

// Counts one program's `ms`, keeping its argv if it is among the slowest:
// Zig's `Time.ran`. An argv the arena refuses is left out of the top, as
// Zig's `catch return` leaves it.
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
// `inline`, as host_time_write: only the CLI's report uses the two.
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
// refuses is left out, as Zig's `record` leaves it.
static void host_record(Host *h, Str line) {
    Str kept = S("");
    if (!arena_dup(h->paths.arena, line, &kept)) return;
    if (HostRecords_push(&h->records, kept) != OK) return;
}

// ── stops ──

// A stop of the host's, its message Zig's format `fmt` of `args`.
static STATUS_MUST_USE Status host_fail(Run *r, Pos pos, StopClass cls, Str fmt, MsgArgs args) {
    return path_fail(&builtin_store_of(r)->path, pos, cls, fmt, args);
}

// A budget's stop with Zig's message, as `overBudget` and `streamOver` set
// `run.stop` themselves: class `budget`, the budget named.
static STATUS_MUST_USE Status host_budget_fail(Run *r, Pos pos, BudgetKind b, Buf *m, Status built, Str fallback) {
    Status s = run_fail_built(&r->ctx, pos, CLASS_BUDGET, m, built, fallback);
    r->ctx.stop.budget = b;
    return s;
}

// A stop of the host's whose message takes no argument.
static STATUS_MUST_USE Status host_fail_plain(Run *r, Pos pos, StopClass cls, Str fmt) {
    MsgArgs none = {NULL, 0};
    return path_fail(&builtin_store_of(r)->path, pos, cls, fmt, none);
}

// ── the options ──

static STATUS_MUST_USE Status host_int_opt(Run *r, Pos pos, Str k, const Value *x, uint64_t *out) {
    if (x->tag != VALUE_INT) return host_fail(r, pos, CLASS_TYPE, S("`run`: its `{s}` must be an integer, not {s}"), MSG_ARGS(MSG_FROM_SCRIPT(k), MSG_FROM_HOST(value_type_name(x))));
    if (x->as.integer < 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: its `{s}` must be 0 or more, not {d}"), MSG_ARGS(MSG_FROM_SCRIPT(k), MSG_FROM_HOST(run_num(r, x->as.integer))));
    *out = (uint64_t)x->as.integer;
    return OK;
}

static STATUS_MUST_USE Status host_str_opt(Run *r, Pos pos, Str k, const Value *x, Str *out) {
    if (x->tag != VALUE_STRING) return host_fail(r, pos, CLASS_TYPE, S("`run`: its `{s}` must be a string, not {s}"), MSG_ARGS(MSG_FROM_SCRIPT(k), MSG_FROM_HOST(value_type_name(x))));
    *out = x->as.string;
    return OK;
}

static STATUS_MUST_USE Status host_map_opt(Run *r, Pos pos, Str k, const Value *x, Table **out) {
    if (x->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`run`: its `{s}` must be a map, not {s}"), MSG_ARGS(MSG_FROM_SCRIPT(k), MSG_FROM_HOST(value_type_name(x))));
    if (x->as.table->kind == TABLE_LIST) return host_fail(r, pos, CLASS_TYPE, S("`run`: its `{s}` must be a map, not a list"), MSG_ARGS(MSG_FROM_SCRIPT(k)));
    *out = x->as.table;
    return OK;
}

static STATUS_MUST_USE Status host_bool_opt(Run *r, Pos pos, Str k, const Value *x, bool *out) {
    if (x->tag != VALUE_BOOL) return host_fail(r, pos, CLASS_TYPE, S("`run`: its `{s}` must be true or false, not {s}"), MSG_ARGS(MSG_FROM_SCRIPT(k), MSG_FROM_HOST(value_type_name(x))));
    *out = x->as.boolean;
    return OK;
}

// `run`'s options, as the script gives them: Zig's `Opts`. An option given
// is `has_` set; a table option's table is NULL when not given.
typedef struct {
    bool has_timeout;
    uint64_t timeout_ms;
    bool has_re;
    Str re;
    bool has_not_re;
    Str not_re;
    bool nocase;
    uint64_t before;
    uint64_t after;
    bool has_head;
    uint64_t head;
    bool has_tail;
    uint64_t tail;
    uint64_t width;
    bool width_given;
    bool has_whole;
    uint64_t whole;
    // `log`, `cwd`, `stdin` and `stdout_secret`: the script's values.
    bool has_log;
    Value log;
    bool has_cwd;
    Value cwd;
    bool has_stdin;
    Value stdin_v;
    Table *env;
    Table *secrets;
    bool has_want;
    Str want;
    bool has_stdout_secret;
    Value stdout_secret;
    bool secret_replace;
    bool has_cpu_budget;
    uint64_t cpu_budget_ms;
    bool has_stage;
    Str stage;
} RunOpts;

// The options read and held to Zig's rules: Zig's `readOpts`.
static STATUS_MUST_USE Status host_read_opts(Run *r, Pos pos, const Value *v, RunOpts *o) {
    memset(o, 0, sizeof *o);
    o->width = 200;
    if (v->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`run`'s options are a map, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(v))));
    Table *m = v->as.table;
    if (m->kind == TABLE_LIST) return host_fail_plain(r, pos, CLASS_TYPE, S("`run`'s options are a map, `{{timeout_ms = ..., tail = ...}}`, not a list"));
    for (size_t i = 0; i < ValueMap_len(&m->map); i++) {
        Str k = ValueMap_key_at(&m->map, i);
        Value x = ValueMap_value_at(&m->map, i);
        if (str_eq(k, S("timeout_ms"))) {
            o->has_timeout = true;
            TRY(host_int_opt(r, pos, k, &x, &o->timeout_ms));
        } else if (str_eq(k, S("re"))) {
            o->has_re = true;
            TRY(host_str_opt(r, pos, k, &x, &o->re));
        } else if (str_eq(k, S("not_re"))) {
            o->has_not_re = true;
            TRY(host_str_opt(r, pos, k, &x, &o->not_re));
        } else if (str_eq(k, S("nocase"))) {
            TRY(host_bool_opt(r, pos, k, &x, &o->nocase));
        } else if (str_eq(k, S("before"))) {
            TRY(host_int_opt(r, pos, k, &x, &o->before));
        } else if (str_eq(k, S("after"))) {
            TRY(host_int_opt(r, pos, k, &x, &o->after));
        } else if (str_eq(k, S("head"))) {
            o->has_head = true;
            TRY(host_int_opt(r, pos, k, &x, &o->head));
        } else if (str_eq(k, S("tail"))) {
            o->has_tail = true;
            TRY(host_int_opt(r, pos, k, &x, &o->tail));
        } else if (str_eq(k, S("width"))) {
            TRY(host_int_opt(r, pos, k, &x, &o->width));
            o->width_given = true;
        } else if (str_eq(k, S("stdout_secret"))) {
            o->has_stdout_secret = true;
            o->stdout_secret = x;
        } else if (str_eq(k, S("secret_replace"))) {
            TRY(host_bool_opt(r, pos, k, &x, &o->secret_replace));
        } else if (str_eq(k, S("whole"))) {
            o->has_whole = true;
            TRY(host_int_opt(r, pos, k, &x, &o->whole));
        } else if (str_eq(k, S("log"))) {
            Str unused = S("");
            TRY(host_str_opt(r, pos, k, &x, &unused));
            o->has_log = true;
            o->log = x;
        } else if (str_eq(k, S("cwd"))) {
            Str unused = S("");
            TRY(host_str_opt(r, pos, k, &x, &unused));
            o->has_cwd = true;
            o->cwd = x;
        } else if (str_eq(k, S("stdin"))) {
            o->has_stdin = true;
            o->stdin_v = x;
        } else if (str_eq(k, S("env"))) {
            TRY(host_map_opt(r, pos, k, &x, &o->env));
        } else if (str_eq(k, S("secrets"))) {
            TRY(host_map_opt(r, pos, k, &x, &o->secrets));
        } else if (str_eq(k, S("want"))) {
            o->has_want = true;
            TRY(host_str_opt(r, pos, k, &x, &o->want));
        } else if (str_eq(k, S("cpu_budget_ms"))) {
            o->has_cpu_budget = true;
            TRY(host_int_opt(r, pos, k, &x, &o->cpu_budget_ms));
        } else if (str_eq(k, S("stage"))) {
            o->has_stage = true;
            TRY(host_str_opt(r, pos, k, &x, &o->stage));
        } else {
            return host_fail(r, pos, CLASS_ARGUMENT, S("`run` takes timeout_ms, re, not_re, nocase, before, after, head, tail, width, whole, log, cwd, stdin, env, secrets, want, stdout_secret, secret_replace, cpu_budget_ms and stage, not `{s}`"), MSG_ARGS(MSG_FROM_SCRIPT(k)));
        }
    }
    if (!o->has_timeout) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run` needs `timeout_ms`: how long the program may take is the script's to say"));
    if (o->has_stage && !o->has_cpu_budget) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`'s `stage` names the program in a `cpu_budget_ms` stop, and needs one"));
    if (o->has_stdout_secret) {
        if (o->has_whole || o->has_re || o->has_not_re || o->has_head || o->has_tail || o->before > 0 || o->after > 0 || o->nocase || o->width_given || o->has_log) {
            return host_fail(r, pos, CLASS_ARGUMENT, S("`run`'s `stdout_secret` sends stdout to the secret alone, so it takes no keep, filter or log (`whole`, `head`, `tail`, `re`, `not_re`, `nocase`, `before`, `after`, `width`, `log`): its `out` is stderr's last {d} lines"), MSG_ARGS(MSG_FROM_HOST(S("20"))));
        }
        o->has_tail = true;
        o->tail = HOST_SECRET_ERR_TAIL;
        return OK;
    }
    if (o->secret_replace) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`'s `secret_replace` is `stdout_secret`'s: it says a stored secret may be replaced"));
    if (o->has_whole) {
        if (o->has_re || o->has_not_re || o->has_head || o->has_tail || o->before > 0 || o->after > 0 || o->nocase) {
            return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`'s `whole` keeps everything, and takes no filter or keep (`re`, `not_re`, `nocase`, `before`, `after`, `head`, `tail`)"));
        }
    } else if (!o->has_head && !o->has_tail) {
        return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run` needs a keep: `head`, `tail` (or both), or `whole = N`: what to keep of the output is the script's to say"));
    }
    if ((o->before > 0 || o->after > 0) && !o->has_re) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`'s `before` and `after` are a match's context, and need `re`"));
    return OK;
}

// ── the refusals ──

typedef struct {
    const char *names; // cstr: the names, each ended by a space
    const char *why; // cstr: a literal
} HostRefusedGroup;

SLICE_TYPE(HostRefusedGroups, const HostRefusedGroup);

// Programs `run` refuses without a `want`: Zig's `refused`.
static const HostRefusedGroup HOST_REFUSED[] = {
    {"bash sh dash zsh busybox python python3 perl node ", "a shell or an interpreter: Shale is the script"},
    {"head tail awk gawk cut sort uniq wc cat tr ", "a text tool: Shale's builtins and run's own filter and keep do this"},
    {"env xargs timeout nice nohup setsid stdbuf time ", "a wrapper that runs another program: run that program, with run's own options"},
};

// Programs `run` refuses even with a `want`: Zig's `never`.
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
        const char *sp = memchr(&at(all, from), ' ', all.len - from);
        size_t to = (size_t)(sp - all.ptr);
        if (str_eq(str_sub(all, from, to), name)) return true;
        from = to + 1;
    }
    return false;
}

// Zig's `std.fs.path.basename`: the last part, trailing `/`s dropped.
static Str host_basename(Str p) {
    size_t end = p.len;
    while (end > 0 && str_byte(p, end - 1) == '/') end--;
    size_t start = end;
    while (start > 0 && str_byte(p, start - 1) != '/') start--;
    return str_sub(p, start, end);
}

// argv[0]'s basename with each `.digits` suffix dropped (`python3.12` is
// `python3`): Zig's `baseName`.
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

// Whether git tracks the program at `path`, relative to `cwd`: Zig's
// `tracked`. A git that cannot start is "no".
static STATUS_MUST_USE Status host_tracked(Host *h, Run *r, Pos pos, Str path, Str cwd, bool *out) {
    Str argv_all[5] = {S("git"), S("ls-files"), S("--error-unmatch"), S("--"), path};
    SpawnSpec spec;
    spawn_spec_default(&spec);
    spec.argv.ptr = argv_all;
    spec.argv.len = 5;
    spec.has_cwd = true;
    spec.cwd = cwd;
    spec.env = h->env;
    spec.timeout_ms = 10000;
    SpawnSink sink = {NULL, host_discard};
    SpawnOutcome o;
    SpawnResult got = spawn_run(r->ctx.values, &spec, sink, &o);
    if (got == SPAWN_OUT_OF_MEMORY) return run_values_oom(r, pos);
    *out = got == SPAWN_OK && o.ended == SPAWN_EXIT && o.code == 0;
    return OK;
}

// Refuses a program in HOST_NEVER whatever the want; then, without one, a
// program in HOST_REFUSED and a program by a path git does not track, unless
// it is under a ` tools` mount. Used by `run` and `job_start`: Zig's
// `refuse`.
static STATUS_MUST_USE Status host_refuse(Host *h, Run *r, Pos pos, Str who, Str argv0, Str cwd, bool has_want) {
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
    if (argv0.len == 0 || memchr(argv0.ptr, '/', argv0.len) == NULL || host_under_tools_mount(h, argv0)) return OK;
    bool known = false;
    TRY(host_tracked(h, r, pos, argv0, cwd, &known));
    if (known) return OK;
    return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`: a program by path that git does not track (a script just written?); {{want = \"tag: why\"}} to run it anyway"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(argv0)));
}

// ── the output's way: scrub, then log, keep or whole ──

// A secret's name and its bytes.
typedef struct {
    Str name;
    Str value;
} HostSecret;

SLICE_TYPE(HostSecrets, const HostSecret);

// Where scrubbed output goes: the whole log, and the keep or `whole`.
// Zig's `Out`.
typedef struct {
    int log_fd;
    bool log_failed;
    Keep *keeper;
    Buf *whole;
    uint64_t whole_most;
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
        if (o->whole->len + bytes.len > o->whole_most) {
            o->whole_over = true;
        } else {
            TRY(buf_str(o->whole, bytes));
        }
    }
    return OK;
}

// Replaces each secret's bytes with `<secret K>` before the log, the keep
// and `whole`: Zig's `Scrub`. A possible secret split across reads is held
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

static STATUS_MUST_USE Status host_scrub_scan(HostScrub *s, bool last) {
    Str buf = buf_view(&s->carry);
    buf_clear(&s->scanned);
    size_t i = 0;
    while (i < buf.len) {
        Str rest = str_sub(buf, i, buf.len);
        bool wait = false;
        for (size_t k = 0; !last && k < s->secrets.len; k++) {
            Str v = at(s->secrets, k).value;
            if (rest.len < v.len && host_starts_with(v, rest)) wait = true;
        }
        if (wait) break;
        const HostSecret *best = NULL;
        for (size_t k = 0; k < s->secrets.len; k++) {
            const HostSecret *kv = &at(s->secrets, k);
            if (host_starts_with(rest, kv->value) && (best == NULL || kv->value.len > best->value.len)) best = kv;
        }
        if (best != NULL) {
            size_t end = i + best->value.len;
            for (size_t j = i + 1; !wait && j < end; j++) {
                for (size_t k = 0; k < s->secrets.len; k++) {
                    Str o = at(s->secrets, k).value;
                    Str there = str_sub(buf, j, buf.len);
                    if (host_starts_with(there, o)) {
                        if (j + o.len > end) end = j + o.len;
                    } else if (!last && there.len < o.len && host_starts_with(o, there)) {
                        wait = true;
                        break;
                    }
                }
            }
            if (wait) break;
            TRY(buf_lit(&s->scanned, "<secret "));
            TRY(buf_str(&s->scanned, best->name));
            TRY(buf_byte(&s->scanned, '>'));
            i = end;
            continue;
        }
        TRY(buf_byte(&s->scanned, (char)str_byte(buf, i)));
        i++;
    }
    TRY(host_out_write(s->next, buf_view(&s->scanned)));
    size_t left = buf.len - i;
    if (left > 0) memmove(s->carry.ptr, &at(buf, i), left);
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

// ── `stdout_secret`'s file ──

// Zig's `std.os.linux.E` tag of an errno, as `@tagName` gives it.
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
// the secret, mode 0600, renamed over it by `host_secret_store`, or removed.
// Zig's `SecretFile`.
typedef struct {
    // The script's path, `keys:NAME`, for messages.
    Str shown;
    const char *path; // cstr: NUL-ended for the kernel
    const char *tmp; // cstr: NUL-ended for the kernel
    int fd;
    // Whether `tmp` is there to remove.
    bool made;
} HostSecretFile;

static STATUS_MUST_USE Status host_secret_open(Host *h, Run *r, Pos pos, const Value *v, bool replace, HostSecretFile *out) {
    memset(out, 0, sizeof *out);
    out->fd = -1;
    if (v->tag != VALUE_STRING) {
        return host_fail(r, pos, CLASS_TYPE, S("`run`'s `stdout_secret` is a path in a `secrets` mount, `\"keys:NAME\"`, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(v))));
    }
    Str shown = v->as.string;
    Str path = S("");
    TRY(paths_secret_target(&h->paths, &builtin_store_of(r)->path, pos, shown, &path));
    const char *c = NULL;
    if (store_cstr(r->ctx.values, path, &c) != STORE_OK) return run_values_oom(r, pos);
    struct stat st;
    // DRAGON: the secrets rule. The path is a secrets mount's file, which
    // no message may name.
    // REMEDY: every stop below names the script's `shown`, `keys:NAME`,
    // never `path`.
    if (lstat(c, &st) == 0) {
        if (!S_ISREG(st.st_mode)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` {s} is there and is not a file"), MSG_ARGS(MSG_FROM_SCRIPT(shown)));
        if (!replace) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` {s} is there already: `secret_replace = true` to replace it"), MSG_ARGS(MSG_FROM_SCRIPT(shown)));
    } else if (errno != ENOENT) {
        return host_fail(r, pos, CLASS_STORE, S("`run`'s `stdout_secret` {s} cannot be looked at: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(store_err_name(store_err_of(errno)))));
    }
    Str dir = path;
    while (dir.len > 0 && str_byte(dir, dir.len - 1) != '/') dir.len--;
    Str base = str_sub(path, dir.len, path.len);
    if (dir.len > 1) dir.len--;
    if (dir.len == 0) dir = S("/");
    Buf t = buf_make(r->ctx.values);
    if (buf_str(&t, dir) != OK || buf_lit(&t, "/.") != OK || buf_str(&t, base) != OK || buf_byte(&t, '.') != OK || buf_u64(&t, (uint64_t)getpid()) != OK || buf_lit(&t, ".shale-tmp") != OK) return run_values_oom(r, pos);
    const char *tmp = NULL;
    if (store_cstr(r->ctx.values, buf_view(&t), &tmp) != STORE_OK) return run_values_oom(r, pos);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return host_fail(r, pos, CLASS_STORE, S("`run`'s `stdout_secret` {s}: the temporary file beside it cannot be made: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(host_errno_tag(errno))));
    // 0600 whatever the umask; its answer is not read, as in Zig's
    // `createPrivate`.
    int chmodded = fchmod(fd, 0600);
    (void)chmodded;
    out->shown = shown;
    out->path = c;
    out->tmp = tmp;
    out->fd = fd;
    out->made = true;
    return OK;
}

// Renames what the program printed over the secret, giving its bytes.
static STATUS_MUST_USE Status host_secret_store(Run *r, Pos pos, HostSecretFile *s, Str program, bool replace, uint64_t *bytes) {
    off_t end = lseek(s->fd, 0, SEEK_END);
    if (end < 0) return host_fail(r, pos, CLASS_STORE, S("`run`'s `stdout_secret` {s}: what `{s}` printed cannot be measured: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(s->shown), MSG_FROM_SCRIPT(program), MSG_FROM_HOST(host_errno_tag(errno))));
    uint64_t size = (uint64_t)end;
    if (size == 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: `{s}` exited 0 and printed nothing: nothing is stored in {s}"), MSG_ARGS(MSG_FROM_SCRIPT(program), MSG_FROM_SCRIPT(s->shown)));
    if (size > HOST_SECRET_MOST) {
        return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: `{s}` printed {d} bytes, past the {d} `secrets` reads: nothing is stored in {s}"), MSG_ARGS(MSG_FROM_SCRIPT(program), MSG_FROM_HOST(run_unum(r, size)), MSG_FROM_HOST(S("65536")), MSG_FROM_SCRIPT(s->shown)));
    }
    if (fsync(s->fd) != 0) return host_fail(r, pos, CLASS_STORE, S("`run`'s `stdout_secret` {s} cannot be synced: nothing is stored"), MSG_ARGS(MSG_FROM_SCRIPT(s->shown)));
    close(s->fd);
    s->fd = -1;
    long rc = syscall(SYS_renameat2, AT_FDCWD, s->tmp, AT_FDCWD, s->path, replace ? 0u : 1u);
    if (rc != 0) {
        int e = errno;
        if (e == EEXIST) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` {s} was made while `{s}` ran: nothing is stored; `secret_replace = true` to replace it"), MSG_ARGS(MSG_FROM_SCRIPT(s->shown), MSG_FROM_SCRIPT(program)));
        return host_fail(r, pos, CLASS_STORE, S("`run`'s `stdout_secret` {s} cannot be written: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(s->shown), MSG_FROM_HOST(host_errno_tag(e))));
    }
    s->made = false;
    *bytes = size;
    return OK;
}

static void host_secret_discard(HostSecretFile *s) {
    if (s->fd >= 0) close(s->fd);
    s->fd = -1;
    if (s->made) unlink(s->tmp);
    s->made = false;
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
// failed ended, and its output's end: Zig's `overBudget`.
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
        if (s == OK && o->has_code) s = buf_byte(&m, ' ');
        if (s == OK && o->has_code) s = buf_u64(&m, o->code);
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

// The environment a program gets: the inherited one, less what `env` and
// `secrets` set, then `env`'s, then the secrets' (read from their files).
// `shown` gets ` K=V` and ` K=<secret path>` for the record.
static STATUS_MUST_USE Status host_run_env(Host *h, Run *r, Pos pos, const RunOpts *o, Buf *shown, SpawnStrs *env_out, HostSecrets *secrets_out) {
    Arena *a = r->ctx.values;
    size_t extra = o->env != NULL ? ValueMap_len(&o->env->map) : 0;
    size_t nsecrets = o->secrets != NULL ? ValueMap_len(&o->secrets->map) : 0;
    Str *env = ARENA_ARRAY(a, Str, h->env.len + extra + nsecrets);
    if (env == NULL && h->env.len + extra + nsecrets > 0) return run_values_oom(r, pos);
    HostSecret *secrets = ARENA_ARRAY(a, HostSecret, nsecrets);
    if (secrets == NULL && nsecrets > 0) return run_values_oom(r, pos);
    SLICE_TYPE(EnvRoom, Str);
    SLICE_TYPE(SecretRoom, HostSecret);
    EnvRoom room = {env, h->env.len + extra + nsecrets};
    SecretRoom sroom = {secrets, nsecrets};
    size_t n = 0;
    for (size_t i = 0; i < h->env.len; i++) {
        Str e = at(h->env, i);
        const char *eq = e.len > 0 ? memchr(e.ptr, '=', e.len) : NULL;
        Str k = eq != NULL ? str_sub(e, 0, (size_t)(eq - e.ptr)) : e;
        bool over = false;
        for (size_t j = 0; !over && j < extra; j++) over = str_eq(ValueMap_key_at(&o->env->map, j), k);
        for (size_t j = 0; !over && j < nsecrets; j++) over = str_eq(ValueMap_key_at(&o->secrets->map, j), k);
        if (!over) at(room, n++) = e;
    }
    for (size_t j = 0; j < extra; j++) {
        Str k = ValueMap_key_at(&o->env->map, j);
        Value x = ValueMap_value_at(&o->env->map, j);
        Str v = S("");
        TRY(host_str_opt(r, pos, S("env"), &x, &v));
        Buf kv = buf_make(a);
        if (buf_str(&kv, k) != OK || buf_byte(&kv, '=') != OK || buf_str(&kv, v) != OK) return run_values_oom(r, pos);
        at(room, n++) = buf_view(&kv);
        if (buf_byte(shown, ' ') != OK || buf_str(shown, k) != OK || buf_byte(shown, '=') != OK || buf_str(shown, v) != OK) return run_values_oom(r, pos);
    }
    for (size_t j = 0; j < nsecrets; j++) {
        Str k = ValueMap_key_at(&o->secrets->map, j);
        Value x = ValueMap_value_at(&o->secrets->map, j);
        Str given = S("");
        TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &x, S("its path"), &given));
        Str path = S("");
        TRY(paths_secret_path(&h->paths, &builtin_store_of(r)->path, pos, builtin_who(r, BUILTIN_HOST), given, &path));
        Str shown_path = given;
        // DRAGON: the secrets rule. `path` is a secret's file and `bytes`
        // a secret; neither may reach a message or the record.
        // REMEDY: the stops name the script's own path, `shown_path`; the
        // value goes to the environment and the scrubber alone.
        const char *c = NULL;
        if (store_cstr(a, path, &c) != STORE_OK) return run_values_oom(r, pos);
        int fd = open(c, O_RDONLY | O_CLOEXEC);
        char *bytes = NULL;
        size_t len = 0;
        int err = 0;
        if (fd < 0) {
            err = errno;
        } else {
            bytes = arena_alloc(a, HOST_SECRET_MOST + 1, 1);
            if (bytes == NULL) {
                close(fd);
                return run_values_oom(r, pos);
            }
            Bytes into = {bytes, HOST_SECRET_MOST + 1};
            while (len < into.len) {
                ssize_t got = read(fd, &at(into, len), into.len - len);
                if (got < 0 && errno == EINTR) continue;
                if (got < 0) {
                    err = errno;
                    break;
                }
                if (got == 0) break;
                len += (size_t)got;
            }
            close(fd);
        }
        Str why = S("");
        if (err != 0) why = store_err_name(store_err_of(err));
        if (err == 0 && len > HOST_SECRET_MOST) why = S("StreamTooLong");
        if (why.len > 0) return host_fail(r, pos, CLASS_STORE, S("`run`: the secret `{s}`'s file {s} cannot be read: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(k), MSG_FROM_SCRIPT(shown_path), MSG_FROM_HOST(why)));
        Str v = {bytes, len};
        while (v.len > 0 && memchr(" \t\r\n", str_byte(v, 0), 4) != NULL) v = str_sub(v, 1, v.len);
        while (v.len > 0 && memchr(" \t\r\n", str_byte(v, v.len - 1), 4) != NULL) v = str_sub(v, 0, v.len - 1);
        if (v.len == 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: the secret `{s}`'s file {s} is empty"), MSG_ARGS(MSG_FROM_SCRIPT(k), MSG_FROM_SCRIPT(shown_path)));
        Buf kv = buf_make(a);
        if (buf_str(&kv, k) != OK || buf_byte(&kv, '=') != OK || buf_str(&kv, v) != OK) return run_values_oom(r, pos);
        at(room, n++) = buf_view(&kv);
        at(sroom, j).name = k;
        at(sroom, j).value = v;
        if (buf_byte(shown, ' ') != OK || buf_str(shown, k) != OK || buf_lit(shown, "=<secret ") != OK || buf_str(shown, shown_path) != OK || buf_byte(shown, '>') != OK) return run_values_oom(r, pos);
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
    if (!o->has_stdin) return OK;
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
        value_string(&s, at(argv, i));
        TRY(run_list_push(r, pos, list, &s));
    }
    Value lv;
    value_table(&lv, list);
    TRY(run_map_put(r, pos, t, S("argv"), &lv));
    if (o->has_code && o->ended == SPAWN_EXIT) TRY(run_map_put_int(r, pos, t, S("code"), o->code));
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
    if (s == OK && o->has_want) s = buf_lit(&b, " (want: ");
    if (s == OK && o->has_want) s = buf_str(&b, o->want);
    if (s == OK && o->has_want) s = buf_byte(&b, ')');
    if (s == OK) s = buf_lit(&b, " -> ");
    if (s == OK) s = buf_str(&b, host_ended_word(out->ended));
    if (s == OK && out->has_code) s = buf_byte(&b, ' ');
    if (s == OK && out->has_code) s = buf_u64(&b, out->code);
    if (s == OK) s = buf_lit(&b, " (");
    if (s == OK) s = buf_u64(&b, out->ms);
    if (s == OK) s = buf_lit(&b, " ms)");
    if (s == OK) s = buf_str(&b, secret_said);
    if (s == OK) host_record(h, buf_view(&b));
}

// Runs the program, streams its output through the scrubber to the log and
// the keep, and gives the answer: Zig's `runProgram` after its options.
static STATUS_MUST_USE Status host_run_spawned(Host *h, Run *r, Pos pos, SpawnStrs argv, const RunOpts *o, HostRunHeld *held, Value *out) {
    Arena *a = r->ctx.values;
    RunStore *rs = builtin_store_of(r);
    Str cwd = S("");
    if (o->has_cwd) {
        TRY(paths_os_path(&h->paths, &rs->path, pos, builtin_who(r, BUILTIN_HOST), o->cwd.as.string, true, &cwd));
    } else {
        TRY(paths_root_of(&h->paths, &rs->path, pos, &cwd));
    }
    TRY(host_refuse(h, r, pos, S("run"), at(argv, 0), cwd, o->has_want));
    size_t n_env = o->env != NULL ? ValueMap_len(&o->env->map) : 0;
    Str *keys = ARENA_ARRAY(a, Str, n_env);
    Str *vals = ARENA_ARRAY(a, Str, n_env);
    if (n_env > 0 && (keys == NULL || vals == NULL)) return run_values_oom(r, pos);
    SLICE_TYPE(StrRoom, Str);
    StrRoom kroom = {keys, n_env};
    StrRoom vroom = {vals, n_env};
    size_t n_str = 0;
    for (size_t j = 0; j < n_env; j++) {
        Value x = ValueMap_value_at(&o->env->map, j);
        if (x.tag != VALUE_STRING) continue;
        at(kroom, n_str) = ValueMap_key_at(&o->env->map, j);
        at(vroom, n_str) = x.as.string;
        n_str++;
    }
    PathsStrs argv_c = {argv.ptr, argv.len};
    PathsStrs env_keys = {keys, n_str};
    PathsStrs env_vals = {vals, n_str};
    TRY(paths_refuse_secret_args(&h->paths, &rs->path, pos, S("run"), argv_c, cwd, env_keys, env_vals));

    Buf shown_env = buf_make(a);
    SpawnStrs env = {NULL, 0};
    HostSecrets secrets = {NULL, 0};
    TRY(host_run_env(h, r, pos, o, &shown_env, &env, &secrets));
    bool has_stdin = false;
    Str stdin_bytes = S("");
    TRY(host_run_stdin(r, pos, o, &has_stdin, &stdin_bytes));
    if (o->has_stdout_secret) {
        TRY(host_secret_open(h, r, pos, &o->stdout_secret, o->secret_replace, &held->secret));
        held->has_secret = true;
    }

    // The stream's arena, capped at what is left of the run's arena budget:
    // what grows as output arrives grows by how the pipe's reads fall.
    uint64_t most = run_sat_sub(r->ctx.budgets.arena_bytes, a->held);
    held->stream = arena_make((size_t)most);
    held->has_stream = true;
    Arena *sa = &held->stream;
    HostOut sink_out;
    memset(&sink_out, 0, sizeof sink_out);
    sink_out.log_fd = -1;
    Str log_shown = S("");
    if (o->has_log) {
        log_shown = o->log.as.string;
        Str path = S("");
        TRY(paths_os_path(&h->paths, &rs->path, pos, builtin_who(r, BUILTIN_HOST), log_shown, false, &path));
        const char *c = NULL;
        if (store_cstr(a, path, &c) != STORE_OK) return run_values_oom(r, pos);
        held->log_fd = open(c, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (held->log_fd < 0) return host_fail(r, pos, CLASS_STORE, S("`run`: its log {s} cannot be written: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(log_shown), MSG_FROM_HOST(store_err_name(store_err_of(errno)))));
        sink_out.log_fd = held->log_fd;
    }
    Buf whole_buf = buf_make(sa);
    Keep keeper;
    HostPattern patterns[2];
    SLICE_TYPE(HostPatterns, HostPattern);
    HostPatterns pats = {patterns, 2};
    if (o->has_whole) {
        sink_out.whole = &whole_buf;
        sink_out.whole_most = o->whole;
    } else {
        KeepOptions ko;
        keep_options_default(&ko);
        ko.head = (size_t)o->head;
        ko.tail = (size_t)o->tail;
        ko.width = (size_t)o->width;
        ko.before = (size_t)o->before;
        ko.after = (size_t)o->after;
        for (size_t i = 0; i < 2; i++) {
            bool given = i == 0 ? o->has_re : o->has_not_re;
            if (!given) continue;
            Str p = i == 0 ? o->re : o->not_re;
            RegexOptions ro = {o->nocase};
            RegexRefusal why;
            memset(&why, 0, sizeof why);
            if (regex_compile(a, p, ro, &at(pats, i).program, &why) != OK) {
                if (!why.refused) return run_values_oom(r, pos);
                return host_fail(r, pos, CLASS_ARGUMENT, S("`run`'s `{s}`, at its byte {d}: {s}"), MSG_ARGS(MSG_FROM_HOST(i == 0 ? S("re") : S("not_re")), MSG_FROM_HOST(run_unum(r, (uint64_t)why.at + 1)), MSG_FROM_HOST(why.message)));
            }
            if (regex_scratch_make(a, &at(pats, i).program, &at(pats, i).scratch) != OK) return run_values_oom(r, pos);
            KeepPattern kp = {&at(pats, i), host_pattern_matches};
            if (i == 0) {
                ko.re = kp;
            } else {
                ko.not_re = kp;
            }
        }
        if (keep_init(&keeper, sa, ko) != OK) return run_values_oom(r, pos);
        sink_out.keeper = &keeper;
    }
    HostScrub scrub = {secrets, buf_make(sa), buf_make(sa), &sink_out};
    SpawnSink sink = {&scrub, host_scrub_write};

    r->host_runs++;
    SpawnSpec spec;
    spawn_spec_default(&spec);
    spec.argv = argv;
    spec.has_cwd = true;
    spec.cwd = cwd;
    spec.env = env;
    spec.has_stdin = has_stdin;
    spec.stdin_bytes = stdin_bytes;
    spec.stdout_fd = held->has_secret ? held->secret.fd : -1;
    spec.timeout_ms = o->timeout_ms;
    SpawnOutcome outcome;
    memset(&outcome, 0, sizeof outcome);
    Str argv0 = at(argv, 0);
    switch (spawn_run(a, &spec, sink, &outcome)) {
    case SPAWN_OK: break;
    case SPAWN_OUT_OF_MEMORY: return run_values_oom(r, pos);
    case SPAWN_SINK_STOPPED: return sa->hit ? host_stream_over(r, pos, most) : run_values_oom(r, pos);
    case SPAWN_NOT_FOUND: return host_fail(r, pos, CLASS_HOST, S("`run`: `{s}` is not found{s}"), MSG_ARGS(MSG_FROM_SCRIPT(argv0), MSG_FROM_HOST(memchr(argv0.ptr, '/', argv0.len) == NULL ? S(" along PATH") : S(""))));
    case SPAWN_NOT_RUNNABLE: return host_fail(r, pos, CLASS_HOST, S("`run`: `{s}` is there but cannot be run (its permissions, or not a program)"), MSG_ARGS(MSG_FROM_SCRIPT(argv0)));
    case SPAWN_NO_CWD: return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run`: its cwd is not a folder that can be entered"));
    case SPAWN_REFUSED: return host_fail_plain(r, pos, CLASS_HOST, S("`run`: the program could not be started (a pipe, a fork or a pidfd refused)"));
    }
    if (host_scrub_finish(&scrub) != OK) return sa->hit ? host_stream_over(r, pos, most) : run_values_oom(r, pos);

    // The output charged as bulk work and as read bytes.
    TRY(builtin_charge_bytes(r, pos, sink_out.bytes));
    r->read_bytes += sink_out.bytes;
    Buf jb = buf_make(a);
    for (size_t i = 0; i < argv.len; i++) {
        if ((i > 0 && buf_byte(&jb, ' ') != OK) || buf_str(&jb, at(argv, i)) != OK) return run_values_oom(r, pos);
    }
    Str joined = buf_view(&jb);
    host_time_ran(&h->time, h->paths.arena, joined, outcome.ms);
    // Stored only on exit 0, so a failed program leaves nothing half made.
    bool secret_stored = false;
    uint64_t secret_bytes = 0;
    if (held->has_secret && outcome.ended == SPAWN_EXIT && outcome.code == 0) {
        TRY(host_secret_store(r, pos, &held->secret, argv0, o->secret_replace, &secret_bytes));
        secret_stored = true;
    }
    Buf said = buf_make(a);
    if (held->has_secret) {
        Status s = buf_lit(&said, ", stdout to ");
        if (s == OK) s = buf_str(&said, held->secret.shown);
        if (s == OK && secret_stored) s = buf_lit(&said, " (");
        if (s == OK && secret_stored) s = buf_u64(&said, secret_bytes);
        if (s == OK) s = buf_str(&said, secret_stored ? S(" bytes)") : S(" (not written)"));
        if (s != OK) buf_clear(&said);
    }
    host_run_record(h, r, joined, buf_view(&shown_env), o, &outcome, buf_view(&said));
    if (o->has_log) {
        if (fsync(held->log_fd) != 0 && errno == EIO) sink_out.log_failed = true;
        Buf lb = buf_make(a);
        if (buf_lit(&lb, "log ") == OK && buf_str(&lb, log_shown) == OK && buf_str(&lb, sink_out.log_failed ? S(": a write failed, the log is short") : S("")) == OK) host_record(h, buf_view(&lb));
    }

    Table *t = NULL;
    TRY(host_run_answer(r, pos, argv, &outcome, &t));
    Str kept_out = S("");
    if (o->has_whole) {
        if (sink_out.whole_over) {
            return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: the output passed `whole = {d}` bytes ({d}): a keep, `head` and `tail`, takes any size"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, o->whole)), MSG_FROM_HOST(run_unum(r, sink_out.bytes))));
        }
        int64_t lines = (int64_t)host_count_lines(buf_view(&whole_buf));
        if (!arena_dup(a, buf_view(&whole_buf), &kept_out)) return run_values_oom(r, pos);
        TRY(run_map_put_str(r, pos, t, S("out"), kept_out));
        TRY(run_map_put_bool(r, pos, t, S("whole"), true));
        TRY(run_map_put_int(r, pos, t, S("total_lines"), lines));
        TRY(run_map_put_int(r, pos, t, S("matched_lines"), lines));
        TRY(run_map_put_int(r, pos, t, S("kept_lines"), lines));
    } else {
        Kept kept;
        if (keep_finish(&keeper, &kept) != OK) return sa->hit ? host_stream_over(r, pos, most) : run_values_oom(r, pos);
        TRY(run_charge(r, pos, kept.work));
        if (!arena_dup(a, kept.out, &kept_out)) return run_values_oom(r, pos);
        TRY(run_map_put_str(r, pos, t, S("out"), kept_out));
        TRY(run_map_put_int(r, pos, t, S("total_lines"), (int64_t)kept.total));
        TRY(run_map_put_int(r, pos, t, S("matched_lines"), (int64_t)kept.matched));
        TRY(run_map_put_int(r, pos, t, S("kept_lines"), (int64_t)kept.kept));
    }
    if (held->has_secret) {
        TRY(run_map_put_bool(r, pos, t, S("secret_written"), secret_stored));
        TRY(run_map_put_int(r, pos, t, S("secret_bytes"), (int64_t)secret_bytes));
    }
    if (r->read_bytes > r->ctx.budgets.read_bytes) return run_budget_stop(&r->ctx, pos, BUDGET_READ_BYTES);
    // An interrupted `shale` stops here, once the program's group is gone.
    if (outcome.ended == SPAWN_INTERRUPTED) return host_fail(r, pos, CLASS_HOST, S("`run`: shale was told to stop (TERM or HUP) while `{s}` ran; it was ended"), MSG_ARGS(MSG_FROM_SCRIPT(argv0)));
    if (o->has_cpu_budget && outcome.cpu_ms > o->cpu_budget_ms) return host_over_budget(r, pos, o->has_stage ? o->stage : joined, o->cpu_budget_ms, &outcome, kept_out);
    value_table(out, t);
    return OK;
}

// `host("run", argv, opts)`: Zig's `runProgram`.
static STATUS_MUST_USE Status host_run_program(Host *h, Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 2) {
        return host_fail(r, pos, CLASS_ARGUMENT, S("`run` takes argv and its options, `run({{\"zig\", \"build\"}}, {{timeout_ms = ..., tail = 20}})`, given {d} arguments"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, args.len))));
    }
    const Value *av = &at(args, 0);
    if (av->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`run`'s argv is a list of strings, `{{\"zig\", \"build\"}}`, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(av))));
    Table *argv_t = av->as.table;
    if (argv_t->kind == TABLE_MAP) return host_fail_plain(r, pos, CLASS_TYPE, S("`run`'s argv is a list of strings, not a map"));
    if (argv_t->kind == TABLE_EMPTY || argv_t->list.len == 0) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`run` of an empty argv: name the program"));
    size_t n = argv_t->list.len;
    Str *argv = ARENA_ARRAY(r->ctx.values, Str, n);
    if (argv == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(ArgvRoom, Str);
    ArgvRoom room = {argv, n};
    for (size_t i = 0; i < n; i++) {
        const Value *v = &at(argv_t->list, i);
        if (v->tag != VALUE_STRING) {
            return host_fail(r, pos, CLASS_TYPE, S("`run`'s argv[{d}] is {s}, not a string: every argument is one, written as the program takes it"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, i + 1)), MSG_FROM_HOST(value_type_name(v))));
        }
        at(room, i) = v->as.string;
    }
    RunOpts o;
    TRY(host_read_opts(r, pos, &at(args, 1), &o));
    TRY(run_charge(r, pos, n));
    if (r->host_runs >= r->ctx.budgets.programs) return run_budget_stop(&r->ctx, pos, BUDGET_PROGRAMS);
    HostRunHeld held;
    memset(&held, 0, sizeof held);
    held.log_fd = -1;
    held.secret.fd = -1;
    SpawnStrs sargv = {argv, n};
    Status s = host_run_spawned(h, r, pos, sargv, &o, &held, out);
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
    if (ms < 0 || ms > HOST_SLEEP_MOST_MS) return host_fail(r, pos, CLASS_ARGUMENT, S("`sleep` takes 0 to {d} ms, not {d}"), MSG_ARGS(MSG_FROM_HOST(S("600000")), MSG_FROM_HOST(run_num(r, ms))));
    TRY(run_charge(r, pos, 1));
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
    h->time.sleep_ms += (uint64_t)ms;
    value_nil(out);
    return OK;
}
