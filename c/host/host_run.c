// host_run.c: the Linux host's `run` and `sleep`: `run`'s options, the
// program started, its output streamed, and the answer.
//
// A program's output streams through the scrubber (host_run_secrets.c),
// then to the log, and to the keep (keep.c) or `whole`'s buffer, all in an
// arena of the stream's own capped at what is left of the run's arena
// budget, so a tail of 3 over a 10 GB log holds three lines; only the answer
// goes to the values' arena.
//
// Include host.c, host_refusals.c, host_write_gate.c and host_run_secrets.c
// first.

// The most a `sleep` waits in one call: ten minutes.
enum { HOST_SLEEP_MAX_MS = 600000 };

// What a failed program's output keeps in a budget's stop, from its end.
enum { HOST_BUDGET_STOP_OUTPUT = 2000 };

// ── the options ──

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

// ── `run` ──

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
static STATUS_MUST_USE Status host_run_record(Host *h, Run *r, Pos pos, Str joined, Str shown_env, const RunOpts *o, const SpawnOutcome *out, Str secret_said) {
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
    return host_record(h, r, pos, &b, s);
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
        IntText nth;
        TRY(paths_cat(&builtin_files_of(r)->path, pos, S("argument "), uint_text(&nth, i + 1), S(""), &what));
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

// The log's record, `log PATH`, which says so when a write failed (fsync's
// EIO among them) and the log is short.
static STATUS_MUST_USE Status host_run_log_record(Host *h, Run *r, Pos pos, int fd, Str shown, bool failed) {
    if (fsync(fd) != 0 && errno == EIO) failed = true;
    Buf lb = buf_make(r->ctx.values);
    Status s = buf_lit(&lb, "log ");
    if (s == OK) s = buf_str(&lb, shown);
    if (s == OK) s = buf_str(&lb, failed ? S(": a write failed, the log is short") : S(""));
    return host_record(h, r, pos, &lb, s);
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
    if (secret) host_secret_as_stdout(&held->secret, &spec);
    if (o->nice != HOST_UNSET) spec.nice = (int)o->nice;
    spec.timeout_ms = o->timeout_ms;
    SpawnOutcome outcome;
    TRY(host_run_spawn(r, pos, &spec, &sinks, &held->stream, most, &outcome));

    // The output charged as bulk work and as read bytes.
    TRY(builtin_charge_bytes(r, pos, sinks.out.bytes));
    r->read_bytes += sinks.out.bytes;
    Str joined = S("");
    TRY(host_argv_joined(r, pos, argv, &joined));
    if (host_time_ran(&h->time, h->paths.arena, joined, outcome.ms) != OK) return run_values_oom(r, pos);
    Str argv0 = at(argv, 0).s;
    bool secret_stored = false;
    uint64_t secret_bytes = 0;
    Buf said = buf_make(a);
    if (secret) TRY(host_run_secret_end(r, pos, &held->secret, argv0, o->secret_replace, &outcome, &said, &secret_stored, &secret_bytes));
    TRY(host_run_record(h, r, pos, joined, buf_view(&shown_env), o, &outcome, buf_view(&said)));
    if (host_given(&o->log)) TRY(host_run_log_record(h, r, pos, held->log_fd, o->log.as.string, sinks.out.log_failed));

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
