// host_run_secrets.c: everything `run` does with a secret: the guards a
// program's argv and `env` pass before it starts (`host_guard_program`),
// each `secrets` file read into the environment (`host_run_env`), the
// scrubber that takes each secret's bytes out of the output (`HostScrub`),
// and `stdout_secret`'s file, bounded and stored whole.
//
// Include host.c, host_refusals.c and host_write_gate.c first.

// With `stdout_secret`, `out` is stderr's last lines, this many.
enum { HOST_SECRET_ERR_TAIL = 20 };

// The most bytes `stdout_secret` stores, and `secrets` reads of a file.
enum { HOST_SECRET_MAX = 64 * 1024 };

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
    if (!p->has_want) TRY(host_refuse_env(r, pos, p->who, p->env->names));
    return host_refuse(h, r, pos, p->who, at(p->argv, 0), *cwd, p->has_want);
}

// ── a secret's bytes, and the scrubber ──

// A secret's name and its bytes, read from its file (`host_secret_read`).
// DRAGON: the secrets rule. A secret's bytes that reach a message, the
// record or the output unscrubbed are in the script's hands: its log and its
// report.
// REMEDY: host_secret_read's stops name the script's path, never the bytes,
// which go to the environment and the scrubber alone (host_run_env records
// `<secret PATH>`). The scrubber (HostScrub) holds back a rest that could
// still begin a secret until the next read; the longest whole match wins,
// and a secret starting inside it widens it, so no part of one is passed
// raw; the last read scans all that is held.
typedef struct {
    Str name;
    Str value;
} HostSecret;

SLICE_TYPE(HostSecrets, const HostSecret);

// Replaces each secret's bytes with `<secret K>` before the log, the keep
// and `whole`. A possible secret split across reads is held back in `carry`
// until it can be decided: the DRAGON at HostSecret.
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

// ── a secret's file ──

// A secret's file as an OS path, `given` being the script's path: for
// `secrets` (`stored` false) a file any path reaches, a secrets mount's
// included (paths_secret_path); for `stdout_secret` (`stored` true) a file
// directly in a secrets mount (paths_secret_target).
// DRAGON: the secrets rule. The path given is a secrets mount's file, which
// no message or record may name.
// REMEDY: it goes to the file calls alone (file_system_cstr,
// swap_file_target), and every stop and record of a secret names the
// script's `given` (`keys:NAME`); host_secret_read and host_secret_open, its
// two callers, are the only code that holds one.
static STATUS_MUST_USE Status host_secret_os_path(Host *h, Run *r, Pos pos, Str given, bool stored, OsStr *out) {
    PathRun *path_run = &builtin_files_of(r)->path;
    Str path = S("");
    if (stored) {
        TRY(paths_secret_target(&h->paths, path_run, pos, given, &path));
    } else {
        TRY(paths_secret_path(&h->paths, path_run, pos, builtin_who(r, BUILTIN_HOST), given, &path));
    }
    *out = os_str_own(path);
    return OK;
}

// ── `secrets`: each file read into the environment ──

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
// run. Its path is the DRAGON at host_secret_os_path, its bytes the one at
// HostSecret.
static STATUS_MUST_USE Status host_secret_read(Host *h, Run *r, Pos pos, Str name, Str given, Str *value) {
    OsStr path;
    TRY(host_secret_os_path(h, r, pos, given, false, &path));
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr ce = file_system_cstr(&cp, path, &c);
    // Neither waited on (a FIFO) nor followed (a link): host_open_regular.
    int fd = -1;
    int err = ce == FILE_SYSTEM_OK ? host_open_regular(c, false, &fd) : ENAMETOOLONG;
    if (err == HOST_NOT_REGULAR) return host_fail(r, pos, CLASS_REFUSED, S("`run`: the secret `{s}`'s file {s} is there and is not a file"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(name), MESSAGE_FROM_SCRIPT(given)));
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
    if (why.len > 0) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`: the secret `{s}`'s file {s} cannot be read: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(name), MESSAGE_FROM_SCRIPT(given), MESSAGE_FROM_HOST(why)));
    Str v = {bytes, len};
    while (v.len > 0 && mem_find_byte(" \t\r\n", str_byte(v, 0), 4) != NULL) v = str_sub(v, 1, v.len);
    while (v.len > 0 && mem_find_byte(" \t\r\n", str_byte(v, v.len - 1), 4) != NULL) v = str_sub(v, 0, v.len - 1);
    if (v.len == 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: the secret `{s}`'s file {s} is empty"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(name), MESSAGE_FROM_SCRIPT(given)));
    OsStr checked;
    if (!os_str_of(v, &checked)) return host_fail(r, pos, CLASS_REFUSED, S("`run`: the secret `{s}`'s file {s} holds a NUL byte, which no environment can carry"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(name), MESSAGE_FROM_SCRIPT(given)));
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

// ── `stdout_secret`'s file ──

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
        return host_fail(r, pos, CLASS_TYPE, S("`run`'s `stdout_secret` is a path in a `secrets` mount, `\"keys:NAME\"`, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(value_type_name(v))));
    }
    Str shown = v->as.string;
    // Every stop names `shown`, never `path`: the DRAGON at
    // host_secret_os_path.
    OsStr path;
    TRY(host_secret_os_path(h, r, pos, shown, true, &path));
    FileSystemErr ce = swap_file_target(&out->file, path);
    if (ce != FILE_SYSTEM_OK) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s} cannot be looked at: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown), MESSAGE_FROM_HOST(file_system_err_name(ce))));
    int err = 0;
    SwapThere there = swap_file_there(&out->file, &err);
    if (there == SWAP_NOT_FILE) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` {s} is there and is not a file"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown)));
    if (there == SWAP_FILE && !replace) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` {s} is there already: `secret_replace = true` to replace it"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown)));
    if (there == SWAP_UNSEEN) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s} cannot be looked at: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown), MESSAGE_FROM_HOST(file_system_err_name(file_system_err_of(err)))));
    int made = swap_file_open(&out->file, r->ctx.values);
    if (made == SWAP_NO_ROOM) return run_values_oom(r, pos);
    if (made != 0) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s}: the temporary file beside it cannot be made: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown), MESSAGE_FROM_HOST(host_errno_tag(made))));
    out->shown = shown;
    return OK;
}

// Stops the run when the program printed past HOST_SECRET_MAX, however it
// ended: one held to the cap by RLIMIT_FSIZE ends by SIGXFSZ (or its write
// fails) with one byte past it written.
static STATUS_MUST_USE Status host_secret_refuse_over(Run *r, Pos pos, HostSecretFile *s, Str program) {
    off_t end = lseek(s->file.fd, 0, SEEK_END);
    if (end < 0) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s}: what `{s}` printed cannot be measured: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(s->shown), MESSAGE_FROM_SCRIPT(program), MESSAGE_FROM_HOST(host_errno_tag(errno))));
    uint64_t size = (uint64_t)end;
    if (size > HOST_SECRET_MAX) {
        return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: `{s}` printed {d} bytes, past the {d} `secrets` reads: nothing is stored in {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(program), MESSAGE_FROM_HOST(run_unum(r, size)), MESSAGE_FROM_HOST(S("65536")), MESSAGE_FROM_SCRIPT(s->shown)));
    }
    return OK;
}

// Renames what the program printed over the secret, giving its bytes. Past
// the cap was refused before (`host_secret_refuse_over`).
static STATUS_MUST_USE Status host_secret_store(Run *r, Pos pos, HostSecretFile *s, Str program, bool replace, uint64_t *bytes) {
    off_t end = lseek(s->file.fd, 0, SEEK_END);
    if (end < 0) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s}: what `{s}` printed cannot be measured: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(s->shown), MESSAGE_FROM_SCRIPT(program), MESSAGE_FROM_HOST(host_errno_tag(errno))));
    uint64_t size = (uint64_t)end;
    if (size == 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: `{s}` exited 0 and printed nothing: nothing is stored in {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(program), MESSAGE_FROM_SCRIPT(s->shown)));
    int e = 0;
    SwapEnd end_of = swap_file_commit(&s->file, replace, &e);
    if (end_of == SWAP_UNSYNCED) return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s} cannot be synced: nothing is stored"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(s->shown)));
    if (end_of != SWAP_DONE) {
        if (end_of == SWAP_TAKEN) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `stdout_secret` {s} was made while `{s}` ran: nothing is stored; `secret_replace = true` to replace it"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(s->shown), MESSAGE_FROM_SCRIPT(program)));
        return host_fail(r, pos, CLASS_FILE_SYSTEM, S("`run`'s `stdout_secret` {s} cannot be written: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(s->shown), MESSAGE_FROM_HOST(host_errno_tag(e))));
    }
    *bytes = size;
    return OK;
}

static void host_secret_discard(HostSecretFile *s) {
    swap_file_discard(&s->file);
}

// Makes the secret's file the program's stdout, held to the cap.
// DRAGON: the budgets. The secret's file is the program's stdout, which no
// pipe bounds: it could fill the disk before the cap is checked at exit.
// REMEDY: RLIMIT_FSIZE at the cap plus one, set in the child before exec
// (spawn.c), so the kernel stops the file one byte past the cap and
// `host_secret_refuse_over` sees it; test_run_guards prints 10 MB at it.
static void host_secret_as_stdout(const HostSecretFile *s, SpawnSpec *spec) {
    spec->stdout_fd = s->file.fd;
    spec->file_max = HOST_SECRET_MAX + 1;
}

// `stdout_secret`'s end, once the program's: refused past its cap however
// the program ended, stored only on exit 0 (so a failed program leaves
// nothing half made, `*stored` false), and `said` for the record (`, stdout
// to keys:x (N bytes)`).
static STATUS_MUST_USE Status host_run_secret_end(Run *r, Pos pos, HostSecretFile *secret, Str argv0, bool replace, const SpawnOutcome *o, Buf *said, bool *stored, uint64_t *bytes) {
    TRY(host_secret_refuse_over(r, pos, secret, argv0));
    if (o->ended == SPAWN_EXIT && o->code == 0) {
        TRY(host_secret_store(r, pos, secret, argv0, replace, bytes));
        *stored = true;
    }
    Status s = buf_lit(said, ", stdout to ");
    if (s == OK) s = buf_str(said, secret->shown);
    if (s == OK && *stored) s = buf_lit(said, " (");
    if (s == OK && *stored) s = buf_u64(said, *bytes);
    if (s == OK) s = buf_str(said, *stored ? S(" bytes)") : S(" (not written)"));
    if (s != OK) buf_clear(said);
    return OK;
}
