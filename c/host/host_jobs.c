// host_jobs.c: the host's job calls: `job_start`, and `job_alive`,
// `job_stop` and `job_wait`, which find a job by the map `job_start` gave or
// by its record's Shale path (`"runs:x/gates.log.job"`), so a later `shale`
// carries on where an earlier one started; and `host_jobs_stop_with_run`,
// which ends the jobs a stopped run started. The processes are jobs.c's.
//
// Include host_run.c and jobs.c first.

static bool host_ends_with(Str s, Str suffix) {
    return s.len >= suffix.len && str_eq(str_sub(s, s.len - suffix.len, s.len), suffix);
}

// A file the host read for the script, charged as a script's read is.
static STATUS_MUST_USE Status host_jobs_charge_read(Run *r, Pos pos, uint64_t n) {
    TRY(builtin_charge_bytes(r, pos, n));
    r->read_bytes += n;
    return OK;
}

// A file of at most 64 KiB read whole into the values' arena: `*why` is the
// error's name when it cannot be (`FileNotFound`, `StreamTooLong` past the
// limit), "" when read.
static STATUS_MUST_USE Status host_jobs_read(Run *r, Pos pos, Str os, Bytes *out, Str *why) {
    *why = S("");
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr ce = file_system_cstr(&cp, os, &c);
    int fd = ce == FILE_SYSTEM_OK ? open(c, O_RDONLY | O_CLOEXEC) : -1;
    if (fd < 0) {
        *why = file_system_err_name(ce != FILE_SYSTEM_OK ? ce : file_system_err_of(errno));
        return OK;
    }
    int err = 0;
    bool read_ok = jobs_read_capped(r->ctx.values, fd, out, &err);
    close(fd);
    if (!read_ok) return run_values_oom(r, pos);
    if (err != 0) *why = file_system_err_name(file_system_err_of(err));
    if (why->len == 0 && out->len > JOBS_RECORD_MAX) *why = S("StreamTooLong");
    return OK;
}

// A job found again: its record as read now, its record's Shale path, and
// its log's.
typedef struct {
    JobRecord record;
    Str shown;
    Str log;
} HostJobFound;

// Finds a job by the map `job_start` gave or its record's Shale path, and
// reads its record.
static STATUS_MUST_USE Status host_jobs_find(Host *h, Run *r, Pos pos, const Value *v, Str name, HostJobFound *out) {
    Str shown = S("");
    if (v->tag == VALUE_STRING) {
        shown = v->as.string;
    } else if (v->tag == VALUE_TABLE) {
        Value rec;
        bool got = v->as.table->kind == TABLE_MAP && ValueMap_get(&v->as.table->map, S("record"), &rec) && rec.tag == VALUE_STRING;
        if (!got) return host_fail(r, pos, CLASS_TYPE, S("`{s}` takes a job (what `job_start` gave) or its record's path, not that table"), MSG_ARGS(MSG_FROM_HOST(name)));
        shown = rec.as.string;
    } else {
        return host_fail(r, pos, CLASS_TYPE, S("`{s}` takes a job or its record's path, not {s}"), MSG_ARGS(MSG_FROM_HOST(name), MSG_FROM_HOST(value_type_name(v))));
    }
    if (!host_ends_with(shown, S(".job"))) return host_fail(r, pos, CLASS_ARGUMENT, S("`{s}`: {s} is not a job's record (`<log>.job`)"), MSG_ARGS(MSG_FROM_HOST(name), MSG_FROM_SCRIPT(shown)));
    Str os = S("");
    TRY(paths_os_path(&h->paths, &builtin_files_of(r)->path, pos, builtin_who(r, BUILTIN_HOST), shown, false, &os));
    Bytes text = {NULL, 0};
    Str why = S("");
    TRY(host_jobs_read(r, pos, os, &text, &why));
    if (str_eq(why, S("FileNotFound"))) return host_fail(r, pos, CLASS_STORE, S("`{s}`: no job's record at {s}"), MSG_ARGS(MSG_FROM_HOST(name), MSG_FROM_SCRIPT(shown)));
    if (why.len > 0) return host_fail(r, pos, CLASS_STORE, S("`{s}`: its record {s} cannot be read: {s}"), MSG_ARGS(MSG_FROM_HOST(name), MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(why)));
    TRY(host_jobs_charge_read(r, pos, text.len));
    // The record is the script's text, and decides what `job_stop` signals:
    // the DRAGON at jobs_launch's read (jobs.c) says what a forged one can
    // reach.
    if (!job_record_parse(text, &out->record)) return host_fail(r, pos, CLASS_STORE, S("`{s}`: {s} is not a job's record"), MSG_ARGS(MSG_FROM_HOST(name), MSG_FROM_SCRIPT(shown)));
    out->shown = shown;
    out->log = str_sub(shown, 0, shown.len - 4);
    return OK;
}

// `{ended, code?, signal?}`.
static STATUS_MUST_USE Status host_jobs_result(Run *r, Pos pos, Str ended, bool has_code, uint8_t code, bool has_signal, uint32_t signal, Value *out) {
    Table *t = NULL;
    TRY(run_new_table(r, pos, &t));
    t->kind = TABLE_MAP;
    TRY(run_map_put_str(r, pos, t, S("ended"), ended));
    if (has_code) TRY(run_map_put_int(r, pos, t, S("code"), code));
    if (has_signal) TRY(run_map_put_int(r, pos, t, S("signal"), signal));
    value_table(out, t);
    return OK;
}

// `job_start`'s options, as its options boundary read them.
typedef struct {
    // The log's Shale path; required.
    Str log;
    // A string, the folder the job runs in; nil for the root.
    Value cwd;
    HostEnvOpt env;
    // A string, or nil.
    Value want;
    bool keep_on_stop;
} HostJobOpts;

// `job_start`'s options read, each held to its rules: `log` required, and no
// `secrets`, since nothing scrubs a detached job's log.
static STATUS_MUST_USE Status host_job_opts(Run *r, Pos pos, const Value *ov, HostJobOpts *o) {
    memset(o, 0, sizeof *o);
    value_nil(&o->cwd);
    value_nil(&o->want);
    if (ov->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`job_start`'s options are a map, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(ov))));
    Table *m = ov->as.table;
    if (m->kind == TABLE_LIST) return host_fail_plain(r, pos, CLASS_TYPE, S("`job_start`'s options are a map, not a list"));
    // Nil until given: a script's map never holds nil.
    Value log_v;
    value_nil(&log_v);
    for (size_t i = 0; i < ValueMap_len(&m->map); i++) {
        Str k = ValueMap_key_at(&m->map, i);
        Value x = ValueMap_value_at(&m->map, i);
        if (str_eq(k, S("log"))) {
            log_v = x;
        } else if (str_eq(k, S("cwd"))) {
            Str unused = S("");
            TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &x, S("its path"), &unused));
            o->cwd = x;
        } else if (str_eq(k, S("env"))) {
            TRY(host_env_opt(r, pos, k, &x, false, &o->env));
        } else if (str_eq(k, S("want"))) {
            TRY(host_str_value_opt(r, pos, k, &x, &o->want));
        } else if (str_eq(k, S("keep_on_stop"))) {
            if (x.tag != VALUE_BOOL) return host_fail(r, pos, CLASS_TYPE, S("`job_start`'s `keep_on_stop` is a boolean, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(&x))));
            o->keep_on_stop = x.as.boolean;
        } else if (str_eq(k, S("secrets"))) {
            return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`job_start`: no `secrets` on a job: a detached job's log has nobody to scrub it"));
        } else {
            return host_fail(r, pos, CLASS_ARGUMENT, S("`job_start` takes log, cwd, env, want and keep_on_stop, not `{s}`"), MSG_ARGS(MSG_FROM_SCRIPT(k)));
        }
    }
    if (!host_given(&log_v)) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`job_start` needs a `log`: a job's output goes there, and its record beside it"));
    return host_str_opt(r, pos, S("log"), &log_v, &o->log);
}

// Refuses an argv too long for the job's record, which holds it escaped:
// said here, not as a record the reaper could not write.
static STATUS_MUST_USE Status host_job_argv_fits(Run *r, Pos pos, SpawnStrs argv) {
    uint64_t argv_bytes = argv.len - 1;
    for (size_t i = 0; i < argv.len; i++) {
        Str x = at(argv, i);
        for (size_t k = 0; k < x.len; k++) argv_bytes += (str_byte(x, k) == '\n' || str_byte(x, k) == '\\') ? 2 : 1;
    }
    if (argv_bytes <= JOB_RECORD_ARGV_MAX) return OK;
    return host_fail(r, pos, CLASS_ARGUMENT, S("`job_start`: its argv is {d} bytes in its record, and a record holds {d}"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, argv_bytes)), MSG_FROM_HOST(run_unum(r, JOB_RECORD_ARGV_MAX))));
}

// Refuses a log a job still runs on: its record is the job's only handle,
// and the old reaper would write its end over the new record. A record that
// cannot be read for any reason but its absence is refused too: a job may
// still run there.
static STATUS_MUST_USE Status host_job_refuse_running(Run *r, Pos pos, Str log_shown, Str record_os) {
    Bytes old_text = {NULL, 0};
    Str why = S("");
    TRY(host_jobs_read(r, pos, record_os, &old_text, &why));
    if (str_eq(why, S("FileNotFound"))) return OK;
    if (why.len > 0) return host_fail(r, pos, CLASS_STORE, S("`job_start`: {s}.job cannot be read: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(log_shown), MSG_FROM_HOST(why)));
    TRY(host_jobs_charge_read(r, pos, old_text.len));
    JobRecord old;
    if (!job_record_parse(old_text, &old)) return OK;
    if (!jobs_alive(&old) && (job_record_done(old) || !jobs_reaper_alive(&old))) return OK;
    return host_fail(r, pos, CLASS_HOST, S("`job_start`: a job still runs on {s} (pid {d}, its record {s}.job): `job_stop` or `job_wait` it, or give another log"), MSG_ARGS(MSG_FROM_SCRIPT(log_shown), MSG_FROM_HOST(run_num(r, old.pid)), MSG_FROM_SCRIPT(log_shown)));
}

// The job's log and record as OS paths. The reaper truncates the log and
// writes the record, changes the host makes for the script, so each asks
// `may_change` (host_change_os_path); it makes no folder, so the log's must
// be there; and a log a job still runs on is refused.
static STATUS_MUST_USE Status host_job_place(Host *h, Run *r, Pos pos, Str log_shown, Str *log_os, Str *record_os) {
    TRY(host_change_os_path(h, r, pos, log_shown, S(""), log_os));
    TRY(host_change_os_path(h, r, pos, log_shown, S(".job"), record_os));
    size_t slash = log_os->len;
    while (slash > 0 && str_byte(*log_os, slash - 1) != '/') slash--;
    if (slash > 0) {
        Str dir = str_sub(*log_os, 0, slash > 1 ? slash - 1 : 1);
        FileSystemCPath dir_cp;
        const char *dc = NULL;
        if (file_system_cstr(&dir_cp, dir, &dc) != FILE_SYSTEM_OK || access(dc, F_OK) != 0) return host_fail(r, pos, CLASS_STORE, S("`job_start`: {s}'s directory is not there: the log and its record go in it"), MSG_ARGS(MSG_FROM_SCRIPT(log_shown)));
    }
    return host_job_refuse_running(r, pos, log_shown, *record_os);
}

// The record of a job started: `job ARGV [(want: W)] started (pid P), log
// L[, kept on a stop]`.
static void host_job_record(Host *h, Run *r, Str joined, const HostJobOpts *o, const JobRecord *rec) {
    Buf line = buf_make(r->ctx.values);
    Status ls = buf_lit(&line, "job ");
    if (ls == OK) ls = buf_str(&line, joined);
    if (ls == OK && host_given(&o->want)) ls = buf_lit(&line, " (want: ");
    if (ls == OK && host_given(&o->want)) ls = buf_str(&line, o->want.as.string);
    if (ls == OK && host_given(&o->want)) ls = buf_byte(&line, ')');
    if (ls == OK) ls = buf_lit(&line, " started (pid ");
    if (ls == OK) ls = buf_u64(&line, (uint64_t)rec->pid);
    if (ls == OK) ls = buf_lit(&line, "), log ");
    if (ls == OK) ls = buf_str(&line, o->log);
    if (ls == OK && o->keep_on_stop) ls = buf_lit(&line, ", kept on a stop");
    if (ls == OK) host_record(h, buf_view(&line));
}

// Keeps the job started in the host, past the run's values, for a stop to
// end (`host_jobs_stop_with_run`). A job the host cannot keep would be one a
// stop leaves running unsaid, so it is a stop at once, the job ended first.
// DRAGON: the process boundary. A job the run started and the host lost
// would outlive a stopped run, against the rule that a stop ends its jobs.
// REMEDY: each copy is checked, and a refused one stops the job before the
// arena's stop is said.
static STATUS_MUST_USE Status host_job_keep(Host *h, Run *r, Pos pos, Str joined, Str record_os, const HostJobOpts *o, JobRecord *rec) {
    Arena *ha = h->paths.arena;
    HostStarted kept;
    memset(&kept, 0, sizeof kept);
    Str argv_shown = joined;
    Buf ab = buf_make(ha);
    bool fits = true;
    if (joined.len > 80) {
        fits = buf_str(&ab, str_sub(joined, 0, 80)) == OK && buf_lit(&ab, "...") == OK;
        argv_shown = buf_view(&ab);
    } else {
        fits = arena_dup(ha, joined, &argv_shown);
    }
    fits = fits && arena_dup(ha, record_os, &kept.record_os) && arena_dup(ha, o->log, &kept.log) && arena_dup(ha, rec->start, &kept.start);
    kept.argv = argv_shown;
    kept.pid = rec->pid;
    kept.pgid = rec->pgid;
    kept.keep = o->keep_on_stop;
    if (fits && HostStartedJobs_push(&h->started, kept) == OK) return OK;
    jobs_stop(rec, JOBS_GROUP_GRACE_MS);
    return run_values_oom(r, pos);
}

// `{log, record, pid, pgid}`, the job `job_alive`, `job_stop` and `job_wait`
// take.
static STATUS_MUST_USE Status host_job_answer(Run *r, Pos pos, Str log_shown, const JobRecord *rec, Value *out) {
    Table *t = NULL;
    TRY(run_new_table(r, pos, &t));
    t->kind = TABLE_MAP;
    TRY(run_map_put_str(r, pos, t, S("log"), log_shown));
    Buf sb = buf_make(r->ctx.values);
    if (buf_str(&sb, log_shown) != OK || buf_lit(&sb, ".job") != OK) return run_values_oom(r, pos);
    TRY(run_map_put_str(r, pos, t, S("record"), buf_view(&sb)));
    TRY(run_map_put_int(r, pos, t, S("pid"), rec->pid));
    TRY(run_map_put_int(r, pos, t, S("pgid"), rec->pgid));
    value_table(out, t);
    return OK;
}

// `job_start(argv, opts)`: starts argv under the reaper, once the guards
// every program passes have passed it. It counts against the run's cap on
// programs.
static STATUS_MUST_USE Status host_job_start(Host *h, Run *r, Pos pos, ConstValues args, Value *out) {
    Arena *a = r->ctx.values;
    if (args.len != 2) {
        return host_fail(r, pos, CLASS_ARGUMENT, S("`job_start` takes argv and its options, `job_start({{\"zig\", \"build\"}}, {{log = \"runs:build.log\"}})`, given {d} arguments"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, args.len))));
    }
    SpawnStrs argv = {NULL, 0};
    TRY(host_argv_arg(r, pos, S("job_start"), S(""), S(""), &at(args, 0), &argv));
    HostJobOpts o;
    TRY(host_job_opts(r, pos, &at(args, 1), &o));
    TRY(run_charge(r, pos, argv.len));
    if (r->host_runs >= r->ctx.budgets.programs) return run_budget_stop(&r->ctx, pos, BUDGET_PROGRAMS);
    // No `secrets` on a job; the guards' order is the DRAGON at
    // host_guard_program (host_run.c).
    HostEnvOpt no_secrets;
    memset(&no_secrets, 0, sizeof no_secrets);
    HostProgram prog = {S("job_start"), argv, &o.cwd, &o.env, &no_secrets, host_given(&o.want)};
    Str cwd = S("");
    TRY(host_guard_program(h, r, pos, &prog, &cwd));
    TRY(host_job_argv_fits(r, pos, argv));
    if (h->reaper.len == 0) return host_fail_plain(r, pos, CLASS_HOST, S("`job_start`: this host has no reaper to start a job under"));

    // A job's record line names no `env`, so what `host_run_env` shows of it
    // is let go.
    Buf shown_env = buf_make(a);
    SpawnStrs env = {NULL, 0};
    HostSecrets none = {NULL, 0};
    TRY(host_run_env(h, r, pos, &o.env, &no_secrets, &shown_env, &env, &none));
    bool found = false;
    Str program = S("");
    Str argv0 = at(argv, 0);
    if (jobs_find(a, argv0, env, cwd, &found, &program) != OK) return run_values_oom(r, pos);
    if (!found) return host_fail(r, pos, CLASS_HOST, S("`job_start`: `{s}` is not found{s}"), MSG_ARGS(MSG_FROM_SCRIPT(argv0), MSG_FROM_HOST(str_find_byte(argv0, '/') == NULL ? S(" along PATH") : S(""))));
    Str log_os = S("");
    Str record_os = S("");
    TRY(host_job_place(h, r, pos, o.log, &log_os, &record_os));

    r->host_runs++;
    JobRecord rec;
    switch (jobs_launch(a, h->reaper, record_os, log_os, cwd, program, argv, env, &rec)) {
    case JOBS_LAUNCHED: break;
    case JOBS_OUT_OF_MEMORY: return run_values_oom(r, pos);
    case JOBS_SPAWN: return host_fail_plain(r, pos, CLASS_HOST, S("`job_start`: the reaper could not be forked"));
    case JOBS_NO_RECORD: return host_fail(r, pos, CLASS_HOST, S("`job_start`: the reaper ended without writing a record at {s}.job"), MSG_ARGS(MSG_FROM_SCRIPT(o.log)));
    }
    Str joined = S("");
    TRY(host_argv_joined(r, pos, argv, &joined));
    host_job_record(h, r, joined, &o, &rec);
    TRY(host_job_keep(h, r, pos, joined, record_os, &o, &rec));
    return host_job_answer(r, pos, o.log, &rec, out);
}

// A log searched as it grows, never reading again what it already read.
typedef struct {
    // How far the log was searched.
    uint64_t done;
    // The job whose log was searched.
    bool has_job;
    int32_t pid;
    int64_t started;
    Bytes buf;
} HostScan;

enum { HOST_SCAN_PIECE = 1024 * 1024 };

// Where a look starts: where the last stopped, less `c_len - 1` so a match
// the last read cut is whole; or 0 for a log begun again (shorter than what
// was searched, or another job's).
static uint64_t host_scan_from(HostScan *s, uint64_t size, int32_t pid, int64_t started, size_t c_len) {
    if (size < s->done || (s->has_job && (s->pid != pid || s->started != started))) s->done = 0;
    s->has_job = true;
    s->pid = pid;
    s->started = started;
    return s->done > c_len - 1 ? s->done - (c_len - 1) : 0;
}

// Whether `c` is in what the log at `os` holds now. A missing log is false;
// any other failure stops the run rather than reading as "not yet".
// `shown` is the script's path for `os`, which every message names: `os`
// is a mount's folder.
static STATUS_MUST_USE Status host_scan_look(HostScan *s, Arena *sa, Run *r, Pos pos, Str os, Str shown, Str c, const JobRecord *rec, bool *found) {
    *found = false;
    FileSystemCPath cp;
    const char *p = NULL;
    FileSystemErr ce = file_system_cstr(&cp, os, &p);
    struct stat st;
    if (ce != FILE_SYSTEM_OK || stat(p, &st) != 0) {
        if (ce == FILE_SYSTEM_OK && errno == ENOENT) return OK;
        return host_fail(r, pos, CLASS_STORE, S("`job_wait`: {s} cannot be read: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(file_system_err_name(ce != FILE_SYSTEM_OK ? ce : file_system_err_of(errno)))));
    }
    uint64_t size = (uint64_t)st.st_size;
    if (s->buf.len == 0) {
        char *b = arena_alloc(sa, HOST_SCAN_PIECE + c.len, 1);
        if (b == NULL) return run_values_oom(r, pos);
        s->buf.ptr = b;
        s->buf.len = HOST_SCAN_PIECE + c.len;
    }
    uint64_t from = host_scan_from(s, size, rec->pid, rec->started, c.len);
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) {
            s->done = 0;
            return OK;
        }
        return host_fail(r, pos, CLASS_STORE, S("`job_wait`: {s} cannot be read: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(file_system_err_name(file_system_err_of(errno)))));
    }
    while (from < size) {
        size_t want = s->buf.len < size - from ? s->buf.len : (size_t)(size - from);
        ssize_t got = pread(fd, s->buf.ptr, want, (off_t)from);
        if (got < 0) {
            int e = errno;
            close(fd);
            return host_fail(r, pos, CLASS_STORE, S("`job_wait`: {s} cannot be read: {s}"), MSG_ARGS(MSG_FROM_SCRIPT(shown), MSG_FROM_HOST(file_system_err_name(file_system_err_of(e)))));
        }
        size_t n = (size_t)got;
        Status charged = host_jobs_charge_read(r, pos, n);
        if (charged != OK) {
            close(fd);
            return charged;
        }
        if (n >= c.len && mem_find(s->buf.ptr, n, c.ptr, c.len) != SIZE_MAX) {
            close(fd);
            *found = true;
            return OK;
        }
        if (n < c.len || from + n >= size) break;
        from += n - (c.len - 1);
    }
    close(fd);
    s->done = size;
    return OK;
}

// `job_wait(job, opts)`'s options: `file` and `contains` nil when not
// given, `contains` a string that is not empty when given.
typedef struct {
    uint64_t limit;
    Value file;
    Value contains;
} HostWaitOpts;

// A job seen ended: `found` when its log holds `contains` after all, else
// `exited`. The record is written after the reap, so the log is whole now.
static STATUS_MUST_USE Status host_wait_seen_ended(Host *h, Run *r, Pos pos, HostScan *scan, Arena *sa, const HostWaitOpts *o, Str log, const JobRecord *rec, Value *out) {
    if (host_given(&o->contains)) {
        Str given = log;
        if (host_given(&o->file)) TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &o->file, S("its path"), &given));
        Str os = S("");
        TRY(paths_os_path(&h->paths, &builtin_files_of(r)->path, pos, builtin_who(r, BUILTIN_HOST), given, false, &os));
        bool found = false;
        TRY(host_scan_look(scan, sa, r, pos, os, given, o->contains.as.string, rec, &found));
        if (found) return host_jobs_result(r, pos, S("found"), false, 0, false, 0, out);
    }
    return host_jobs_result(r, pos, S("exited"), rec->has_exit, rec->exit, rec->has_signal, rec->signal, out);
}

// The wait itself, its scratch arena the caller's.
static STATUS_MUST_USE Status host_wait_loop(Host *h, Run *r, Pos pos, ConstValues args, const HostWaitOpts *o, Arena *sa, Value *out) {
    uint64_t began = spawn_mono_ms();
    HostScan scan;
    memset(&scan, 0, sizeof scan);
    // The first look after 20 ms, each next one twice as late, to a second.
    uint64_t nap = 20;
    while (1) {
        HostJobFound j;
        TRY(host_jobs_find(h, r, pos, &at(args, 0), S("job_wait"), &j));
        if (host_given(&o->contains)) {
            Str given = j.log;
            if (host_given(&o->file)) TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &o->file, S("its path"), &given));
            Str os = S("");
            TRY(paths_os_path(&h->paths, &builtin_files_of(r)->path, pos, builtin_who(r, BUILTIN_HOST), given, false, &os));
            bool found = false;
            TRY(host_scan_look(&scan, sa, r, pos, os, given, o->contains.as.string, &j.record, &found));
            if (found) return host_jobs_result(r, pos, S("found"), false, 0, false, 0, out);
        }
        if (job_record_done(j.record)) return host_wait_seen_ended(h, r, pos, &scan, sa, o, j.log, &j.record, out);
        bool gone = !jobs_alive(&j.record);
        if (gone && !jobs_reaper_alive(&j.record)) {
            // The reaper is gone, so its last write, if any, is there now.
            HostJobFound again;
            TRY(host_jobs_find(h, r, pos, &at(args, 0), S("job_wait"), &again));
            if (job_record_done(again.record)) return host_wait_seen_ended(h, r, pos, &scan, sa, o, again.log, &again.record, out);
            return host_jobs_result(r, pos, S("lost"), false, 0, false, 0, out);
        }
        uint64_t spent = spawn_mono_ms() - began;
        if (spent >= o->limit) return host_jobs_result(r, pos, S("timeout"), false, 0, false, 0, out);
        uint64_t nap_now = gone ? 10 : nap;
        jobs_sleep_ms(nap_now < o->limit - spent ? nap_now : o->limit - spent);
        nap = nap * 2 < 1000 ? nap * 2 : 1000;
    }
}

static STATUS_MUST_USE Status host_job_wait(Host *h, Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 2) return host_fail(r, pos, CLASS_ARGUMENT, S("`job_wait` takes a job and its options, `{{timeout_ms = ...}}`, given {d} arguments"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, args.len))));
    const Value *ov = &at(args, 1);
    if (ov->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`job_wait`'s options are a map, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(ov))));
    Table *m = ov->as.table;
    if (m->kind == TABLE_LIST) return host_fail_plain(r, pos, CLASS_TYPE, S("`job_wait`'s options are a map, not a list"));
    HostWaitOpts o;
    o.limit = HOST_UNSET;
    value_nil(&o.file);
    value_nil(&o.contains);
    for (size_t i = 0; i < ValueMap_len(&m->map); i++) {
        Str k = ValueMap_key_at(&m->map, i);
        Value x = ValueMap_value_at(&m->map, i);
        if (str_eq(k, S("timeout_ms"))) {
            TRY(host_int_opt(r, pos, k, &x, &o.limit));
        } else if (str_eq(k, S("file"))) {
            o.file = x;
        } else if (str_eq(k, S("contains"))) {
            TRY(host_str_value_opt(r, pos, k, &x, &o.contains));
        } else {
            return host_fail(r, pos, CLASS_ARGUMENT, S("`job_wait` takes timeout_ms, file and contains, not `{s}`"), MSG_ARGS(MSG_FROM_SCRIPT(k)));
        }
    }
    if (o.limit == HOST_UNSET) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`job_wait` needs `timeout_ms`, under the Bash call's own timeout"));
    if (host_given(&o.file) && !host_given(&o.contains)) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`job_wait`: `file` without `contains`: what to wait for in it"));
    if (host_given(&o.contains) && o.contains.as.string.len == 0) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`job_wait`'s `contains` is \"\": every file holds it"));
    // Each look reads only what the log gained, a piece at a time into one
    // buffer of the call's own, given back when the call ends.
    Arena scratch;
    arena_init(&scratch, (size_t)HOST_SCAN_PIECE * 4, &r->ctx.refused);
    Status s = host_wait_loop(h, r, pos, args, &o, &scratch, out);
    arena_deinit(&scratch);
    return s;
}

// `job_alive`, `job_stop` and `job_wait`; `job_start` above. A wait's time
// is counted however it ends, a refusal's among them.
static STATUS_MUST_USE Status host_jobs_call(Host *h, Run *r, Pos pos, Str name, ConstValues args, Value *out) {
    if (str_eq(name, S("job_start"))) return host_job_start(h, r, pos, args, out);
    if (str_eq(name, S("job_alive"))) {
        if (args.len != 1) return host_fail(r, pos, CLASS_ARGUMENT, S("`job_alive` takes a job, given {d} arguments"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, args.len))));
        HostJobFound j;
        TRY(host_jobs_find(h, r, pos, &at(args, 0), name, &j));
        TRY(run_charge(r, pos, 1));
        value_bool(out, jobs_alive(&j.record));
        return OK;
    }
    if (str_eq(name, S("job_stop"))) {
        if (args.len != 1) return host_fail(r, pos, CLASS_ARGUMENT, S("`job_stop` takes a job, given {d} arguments"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, args.len))));
        HostJobFound j;
        TRY(host_jobs_find(h, r, pos, &at(args, 0), name, &j));
        TRY(run_charge(r, pos, 1));
        bool was = jobs_stop(&j.record, JOBS_GROUP_GRACE_MS);
        if (was) {
            Buf b = buf_make(r->ctx.values);
            if (buf_lit(&b, "job ") == OK && buf_str(&b, j.shown) == OK && buf_lit(&b, " stopped") == OK) host_record(h, buf_view(&b));
        }
        value_bool(out, was);
        return OK;
    }
    if (str_eq(name, S("job_wait"))) {
        uint64_t began = spawn_mono_ms();
        Status s = host_job_wait(h, r, pos, args, out);
        h->time.waits++;
        h->time.wait_ms += spawn_mono_ms() - began;
        return s;
    }
    return host_fail(r, pos, CLASS_HOST, S("`{s}`: not on this host"), MSG_ARGS(MSG_FROM_SCRIPT(name)));
}

// Whether a started job still runs and has not ended by the run's own hand:
// its record, while it is this job's, says whether it was waited to its end.
static bool host_started_running(Arena *a, const HostStarted *s) {
    JobRecord ident;
    memset(&ident, 0, sizeof ident);
    ident.pid = s->pid;
    ident.pgid = s->pgid;
    ident.start = s->start;
    char *p = arena_cstr(a, s->record_os);
    Bytes text = {NULL, 0};
    if (p != NULL && jobs_read_small(a, p, &text)) {
        JobRecord r;
        if (job_record_parse(text, &r) && r.pid == s->pid && str_eq(r.start, s->start) && job_record_done(r)) return false;
    }
    return jobs_alive(&ident);
}

// A run that stopped ends the jobs it started, each as `job_stop` does, but
// those started with `keep_on_stop`, and says so in a line naming each. A
// job already stopped or waited to its end is passed by, and only a job this
// run started is ever signalled.
static inline STATUS_MUST_USE Status host_jobs_stop_with_run(Host *h, Buf *w) {
    Arena *a = h->paths.arena;
    Buf stopped = buf_make(a);
    Buf kept = buf_make(a);
    size_t n_stopped = 0;
    size_t n_kept = 0;
    for (size_t i = 0; i < h->started.len; i++) {
        const HostStarted *j = &at(h->started, i);
        if (!host_started_running(a, j)) continue;
        Buf *into = j->keep ? &kept : &stopped;
        if (into->len > 0) TRY(buf_lit(into, ", "));
        TRY(buf_byte(into, '`'));
        TRY(buf_str(into, j->argv));
        TRY(buf_lit(into, "` (log "));
        TRY(buf_str(into, j->log));
        TRY(buf_byte(into, ')'));
        if (j->keep) {
            n_kept++;
            continue;
        }
        JobRecord ident;
        memset(&ident, 0, sizeof ident);
        ident.pid = j->pid;
        ident.pgid = j->pgid;
        ident.start = j->start;
        jobs_stop(&ident, JOBS_GROUP_GRACE_MS);
        if (jobs_alive(&ident)) TRY(buf_lit(into, " still alive after KILL"));
        n_stopped++;
    }
    if (n_stopped > 0) {
        TRY(buf_lit(w, "── stopped with the run, "));
        TRY(buf_u64(w, n_stopped));
        TRY(buf_lit(w, " job"));
        if (n_stopped != 1) TRY(buf_byte(w, 's'));
        TRY(buf_lit(w, " it started: "));
        TRY(buf_str(w, buf_view(&stopped)));
        TRY(buf_byte(w, '\n'));
    }
    if (n_kept > 0) {
        TRY(buf_lit(w, "── left running (keep_on_stop): "));
        TRY(buf_str(w, buf_view(&kept)));
        TRY(buf_byte(w, '\n'));
    }
    return OK;
}
