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
    TRY(host_refuse_gh_env(r, pos, p->who, p->env->names));
    TRY(host_refuse_session_env(r, pos, p->who, p->env->names));
    TRY(host_refuse_session_env(r, pos, p->who, p->secrets->names));
    TRY(host_refuse(h, r, pos, p->who, at(p->argv, 0), *cwd, p->has_want));
    return host_refuse_gh(h, r, pos, p->argv, *cwd);
}

// ── a secret's bytes, and the scrubber ──

// A secret's name and its bytes, read from its file (`host_secret_read`).
// DRAGON: the secrets rule. A secret's bytes that reach a message, the
// record or the output unscrubbed are in the script's hands: its log and its
// report.
// REMEDY: host_secret_read's stops name the script's path, never the bytes,
// which go to the environment and the scrubber alone (host_run_env records
// `<secret PATH>`). The scrubber (HostScrub) takes out each secret whole,
// and each run of HOST_SCRUB_PART bytes or more of one, so a program that
// prints a token's first characters or a fragment of it shows none of them;
// it holds back a rest that could still grow into a match until the next
// read, and matches that overlap are one run, so no part of one is passed
// raw; the last read scans all that is held. test_host_run's
// the_scrubber_takes_out_a_part_of_a_secret_twelve_bytes_or_more plants it.
typedef struct {
    Str name;
    Str value;
} HostSecret;

SLICE_TYPE(HostSecrets, const HostSecret);

// The fewest bytes of a secret the scrubber takes out as a part of it: any
// run of this many, anywhere in a secret's value, is that secret's, so a
// prefix (`gh auth status`'s first characters of a token) or a fragment is
// caught as the whole is. A secret shorter than a part is caught whole.
enum { HOST_SCRUB_PART = 12 };

// A part of a secret in the parts' table (`HostScrub.parts`): its bytes'
// hash, the secret's index and where in its value the part starts.
typedef struct {
    uint64_t hash;
    size_t secret;
    size_t from;
    bool used;
} HostScrubPart;

SLICE_TYPE(HostScrubParts, HostScrubPart);

// Replaces each secret's bytes, and each part of a secret
// (HOST_SCRUB_PART bytes or more of it), with `<secret K>` before the log,
// the keep and `whole`. What could still begin one across reads is held
// back in `carry` until it can be decided: the DRAGON at HostSecret.
typedef struct {
    HostSecrets secrets;
    Buf carry;
    Buf scanned;
    HostOut *next;
    // A scan's mark of each byte of `carry`: 1 where a match covers it.
    Buf marks;
    // Every part of every secret of HOST_SCRUB_PART bytes or more, by its
    // hash, open addressed (a power of two of slots, at most half full):
    // made at the first scan (`parts_made`), empty when no secret is that
    // long.
    HostScrubParts parts;
    bool parts_made;
} HostScrub;

static bool host_starts_with(Str s, Str prefix) {
    return s.len >= prefix.len && str_eq(str_sub(s, 0, prefix.len), prefix);
}

// Whether `part` is the start of `whole` and shorter: what more bytes may
// yet make `whole`.
static bool host_is_proper_prefix(Str part, Str whole) {
    return part.len < whole.len && host_starts_with(whole, part);
}

// FNV-1a of the HOST_SCRUB_PART bytes of `s` from `from`.
static uint64_t host_scrub_hash(Str s, size_t from) {
    uint64_t h = 14695981039346656037u;
    for (size_t i = 0; i < HOST_SCRUB_PART; i++) h = (h ^ str_byte(s, from + i)) * 1099511628211u;
    return h;
}

// Makes the parts' table once, in `carry`'s arena: STOP when the arena
// refuses it.
static STATUS_MUST_USE Status host_scrub_parts(HostScrub *s) {
    if (s->parts_made) return OK;
    s->parts_made = true;
    size_t n = 0;
    for (size_t k = 0; k < s->secrets.len; k++) {
        Str v = at(s->secrets, k).value;
        if (v.len >= HOST_SCRUB_PART) n += v.len - HOST_SCRUB_PART + 1;
    }
    if (n == 0) return OK;
    size_t slots = 16;
    while (slots < 2 * n) slots *= 2;
    HostScrubPart *room = ARENA_ARRAY(s->carry.arena, HostScrubPart, slots);
    if (room == NULL) return STOP;
    memset(room, 0, slots * sizeof *room);
    s->parts = (HostScrubParts){room, slots};
    for (size_t k = 0; k < s->secrets.len; k++) {
        Str v = at(s->secrets, k).value;
        for (size_t from = 0; from + HOST_SCRUB_PART <= v.len; from++) {
            uint64_t h = host_scrub_hash(v, from);
            size_t i = (size_t)h & (slots - 1);
            while (at(s->parts, i).used) i = (i + 1) & (slots - 1);
            at(s->parts, i) = (HostScrubPart){h, k, from, true};
        }
    }
    return OK;
}

// Whether the HOST_SCRUB_PART bytes of `buf` from `j` are a part of a
// secret, that secret's index into `*k`.
static bool host_scrub_part_at(const HostScrub *s, Str buf, size_t j, size_t *k) {
    if (s->parts.len == 0) return false;
    uint64_t h = host_scrub_hash(buf, j);
    size_t mask = s->parts.len - 1;
    Str here = str_sub(buf, j, j + HOST_SCRUB_PART);
    for (size_t i = (size_t)h & mask; at(s->parts, i).used; i = (i + 1) & mask) {
        const HostScrubPart *p = &at(s->parts, i);
        if (p->hash != h) continue; // skip: another part's slot
        Str v = at(s->secrets, p->secret).value;
        if (str_eq(str_sub(v, p->from, p->from + HOST_SCRUB_PART), here)) {
            *k = p->secret;
            return true;
        }
    }
    return false;
}

// Whether `rest`, all that is held from some place on, could still grow into
// a match: a proper prefix of a secret shorter than a part, or, shorter than
// a part itself, inside a longer secret.
static bool host_scrub_may_begin(const HostScrub *s, Str rest) {
    for (size_t k = 0; k < s->secrets.len; k++) {
        Str v = at(s->secrets, k).value;
        if (v.len < HOST_SCRUB_PART) {
            if (host_is_proper_prefix(rest, v)) return true;
        } else if (rest.len < HOST_SCRUB_PART && mem_find(v.ptr, v.len, rest.ptr, rest.len) != SIZE_MAX) {
            return true;
        }
    }
    return false;
}

// The end of the longest whole secret shorter than a part that `buf` holds
// from `j`, else `j`; its index into `*k` when there is one.
static size_t host_scrub_short_at(const HostScrub *s, Str buf, size_t j, size_t *k) {
    size_t end = j;
    Str rest = str_sub(buf, j, buf.len);
    for (size_t q = 0; q < s->secrets.len; q++) {
        Str v = at(s->secrets, q).value;
        if (v.len < HOST_SCRUB_PART && host_starts_with(rest, v) && j + v.len > end) {
            end = j + v.len;
            *k = q;
        }
    }
    return end;
}

// Marks each byte of `buf` that a match covers, in `marks`: a whole secret
// shorter than a part, or a part of a longer one. Matches that overlap or
// touch make one run of marks.
static STATUS_MUST_USE Status host_scrub_mark(HostScrub *s, Str buf) {
    buf_clear(&s->marks);
    TRY(buf_reserve(&s->marks, buf.len));
    for (size_t i = 0; i < buf.len; i++) TRY(buf_byte(&s->marks, 0));
    Bytes m = {s->marks.ptr, s->marks.len};
    size_t upto = 0;
    for (size_t j = 0; j < buf.len; j++) {
        size_t k = 0;
        size_t end = host_scrub_short_at(s, buf, j, &k);
        if (j + HOST_SCRUB_PART <= buf.len && host_scrub_part_at(s, buf, j, &k) && j + HOST_SCRUB_PART > end) end = j + HOST_SCRUB_PART;
        if (end <= upto) continue; // skip: no match here past what is marked
        for (size_t i = j > upto ? j : upto; i < end; i++) at(m, i) = 1;
        upto = end;
    }
    return OK;
}

// The secret whose match starts at `j`, where a run of marks starts: the
// longest whole one, else the one a part is of.
static const HostSecret *host_scrub_name_at(const HostScrub *s, Str buf, size_t j) {
    size_t k = 0;
    if (host_scrub_short_at(s, buf, j, &k) > j) return &at(s->secrets, k);
    if (j + HOST_SCRUB_PART <= buf.len && host_scrub_part_at(s, buf, j, &k)) return &at(s->secrets, k);
    return &at(s->secrets, 0);
}

// Scrubs what `carry` holds from its start: each run of marked bytes goes on
// as `<secret K>`, and any other byte as it is. Unless this is the `last`
// read, it stops where what is held could still grow into a match (`held`),
// or where a run reaches that place, since the next read may lengthen it,
// keeping the rest in `carry`.
static STATUS_MUST_USE Status host_scrub_scan(HostScrub *s, bool last) {
    TRY(host_scrub_parts(s));
    Str buf = buf_view(&s->carry);
    size_t held = buf.len;
    if (!last) {
        size_t from = buf.len >= HOST_SCRUB_PART ? buf.len - HOST_SCRUB_PART + 1 : 0;
        for (size_t j = from; j < buf.len && held == buf.len; j++) {
            if (host_scrub_may_begin(s, str_sub(buf, j, buf.len))) held = j;
        }
    }
    TRY(host_scrub_mark(s, buf));
    Str marks = buf_view(&s->marks);
    buf_clear(&s->scanned);
    size_t i = 0;
    while (i < held) {
        if (str_byte(marks, i) == 0) {
            TRY(buf_byte(&s->scanned, (char)str_byte(buf, i)));
            i++;
            continue;
        }
        size_t end = i;
        while (end < buf.len && str_byte(marks, end) != 0) end++;
        if (!last && end >= held) break;
        TRY(buf_lit(&s->scanned, "<secret "));
        TRY(buf_str(&s->scanned, host_scrub_name_at(s, buf, i)->name));
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
// less what `env` and `secrets` set and what the host sets, then `env`'s,
// then the secrets' (read from their files), then what the host sets
// (host_confine_sets: TMPDIR and the caches), each unless `env` sets it.
// `shown` gets ` K=V` and ` K=<secret path>` for the record.
static STATUS_MUST_USE Status host_run_env(Host *h, Run *r, Pos pos, const HostEnvOpt *envs, const HostEnvOpt *secret_paths, Buf *shown, SpawnStrs *env_out, HostSecrets *secrets_out) {
    Arena *a = r->ctx.values;
    HostConfineSet set_room[HOST_CONFINE_SETS];
    HostConfineSets sets = {set_room, HOST_CONFINE_SETS};
    TRY(host_confine_sets(h, r, pos, S("run"), sets));
    size_t extra = envs->names.len;
    size_t nsecrets = secret_paths->names.len;
    OsStr *env = ARENA_ARRAY(a, OsStr, h->env.len + extra + nsecrets + HOST_CONFINE_SETS);
    if (env == NULL) return run_values_oom(r, pos);
    HostSecret *secrets = ARENA_ARRAY(a, HostSecret, nsecrets);
    if (secrets == NULL && nsecrets > 0) return run_values_oom(r, pos);
    SLICE_TYPE(EnvRoom, OsStr);
    SLICE_TYPE(SecretRoom, HostSecret);
    EnvRoom room = {env, h->env.len + extra + nsecrets + HOST_CONFINE_SETS};
    SecretRoom sroom = {secrets, nsecrets};
    size_t n = 0;
    for (size_t i = 0; i < h->env.len; i++) {
        OsStr e = at(h->env, i);
        bool host_set = false;
        for (size_t k = 0; k < sets.len; k++) host_set = host_set || (str_starts_with(e.s, at(sets, k).name) && e.s.len > at(sets, k).name.len && str_byte(e.s, at(sets, k).name.len) == '=');
        if (!host_env_overridden(e.s, envs, secret_paths) && !host_set) at(room, n++) = e;
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
    for (size_t k = 0; k < sets.len; k++) {
        bool given = false;
        for (size_t j = 0; j < extra; j++) given = given || str_eq(at(envs->names, j), at(sets, k).name);
        if (given) continue; // skip: the script's own `env` sets it
        TRY(host_env_pair(r, pos, at(sets, k).name, at(sets, k).value, &at(room, n)));
        n++;
    }
    env_out->ptr = env;
    env_out->len = n;
    secrets_out->ptr = secrets;
    secrets_out->len = nsecrets;
    return OK;
}

// ── `secret_args`: a secret in an argument ──

// A secret's place in an argument, `<secret NAME>`: the text the scrubber
// makes of its value, so the argv as every record shows it is the argv as
// the program's scrubbed output would.
#define HOST_SECRET_MARK "<secret "

// Whether a `secret_args` name is a word, letters, digits and `_`, not first
// a digit: its marker then ends at its first `>`, read one way.
static bool host_secret_arg_name_ok(Str name) {
    if (name.len == 0) return false;
    for (size_t i = 0; i < name.len; i++) {
        uint8_t c = str_byte(name, i);
        bool word = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (i > 0 && c >= '0' && c <= '9');
        if (!word) return false;
    }
    return true;
}

// A marker found in an argument: where it starts and ends (past its `>`),
// and which of `secret_args`' names it holds (`names.len` for none).
typedef struct {
    size_t start;
    size_t end;
    size_t which;
} HostSecretMark;

// The first marker in `s` at or after `from`: false for none. A `<secret `
// with no `>` after it is a marker of no name.
static bool host_secret_mark_next(Str s, size_t from, PathsStrs names, HostSecretMark *m) {
    Str mark = S(HOST_SECRET_MARK);
    Str rest = str_sub(s, from, s.len);
    size_t found = mem_find(rest.ptr, rest.len, mark.ptr, mark.len);
    if (found == SIZE_MAX) return false;
    m->start = from + found;
    size_t name_at = m->start + mark.len;
    const char *close = str_find_byte(str_sub(s, name_at, s.len), '>');
    m->end = close == NULL ? s.len : (size_t)(close - s.ptr) + 1;
    Str name = close == NULL ? S("") : str_sub(s, name_at, m->end - 1);
    m->which = names.len;
    for (size_t j = 0; j < names.len; j++) {
        if (str_eq(at(names, j), name)) m->which = j;
    }
    return true;
}

// Holds `secret_args` to its rules before any secret is read: each name a
// word (host_secret_arg_name_ok) and none also a `secrets` name; each name's
// marker in an argument after the program's; and every marker in the argv
// one of its names. With no `secret_args`, the argv is the script's to say,
// markers and all.
// DRAGON: the secrets rule. A secret that names the program would pass the
// guards (host_refuse, which reads argv[0], and paths_refuse_secret_args) by
// its marker and run as its value, a program no guard read; and a marker the
// script misspelled would reach the program as its text, quietly.
// REMEDY: a marker in argv[0] and a marker of no name are refused here,
// before any file is read or program started, as is a name no marker uses;
// test_run_secret_args plants each.
static STATUS_MUST_USE Status host_secret_args_check(Run *r, Pos pos, SpawnStrs argv, const HostEnvOpt *args, const HostEnvOpt *secrets) {
    PathsStrs names = args->names;
    if (names.len == 0) return OK;
    for (size_t j = 0; j < names.len; j++) {
        Str k = at(names, j);
        if (!host_secret_arg_name_ok(k)) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`'s `secret_args` name `{s}` is not a word (letters, digits and `_`, not first a digit): its place in an argument is `<secret NAME>`"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(k)));
        for (size_t e = 0; e < secrets->names.len; e++) {
            if (str_eq(at(secrets->names, e), k)) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`: `{s}` is a name in both `secrets` and `secret_args`: the output would name two secrets as one, `<secret {s}>`"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(k), MESSAGE_FROM_SCRIPT(k)));
        }
    }
    bool *used = ARENA_ARRAY(r->ctx.values, bool, names.len);
    if (used == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(Used, bool);
    Used u = {used, names.len};
    for (size_t j = 0; j < names.len; j++) at(u, j) = false;
    for (size_t i = 0; i < argv.len; i++) {
        Str s = at(argv, i).s;
        HostSecretMark m;
        size_t from = 0;
        while (from < s.len && host_secret_mark_next(s, from, names, &m)) {
            if (m.which == names.len) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`'s argument {d} holds `<secret ` with no name `secret_args` gives: each `<secret NAME>` in the argv is one of its names"), MESSAGE_ARGS(MESSAGE_FROM_HOST(run_unum(r, i + 1))));
            if (i == 0) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s program is the secret `{s}`: a secret fills an argument after the program's, which the guards read as the script wrote it"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(at(names, m.which))));
            at(u, m.which) = true;
            from = m.end;
        }
    }
    for (size_t j = 0; j < names.len; j++) {
        if (!at(u, j)) return host_fail(r, pos, CLASS_ARGUMENT, S("`run`'s `secret_args` gives `{s}`, and no argument holds `<secret {s}>`"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(at(names, j)), MESSAGE_FROM_SCRIPT(at(names, j))));
    }
    return OK;
}

// The argv the program is handed: each `<secret NAME>` in the script's argv
// (held by host_secret_args_check) replaced by that secret's value, read
// from its file as `secrets` reads one (host_secret_read), so a value is
// trimmed and NUL-free and its file held to the same checks. `into` gets
// each secret, for the scrubber, and `shown` ` argv NAME=<secret PATH>` for
// the record. With no `secret_args`, `*filled` is `argv`.
// DRAGON: the secrets rule. The filled argv holds the values: a record, a
// stop, the answer's `argv` or the time line built from it would show one.
// REMEDY: the filled argv goes to the spawn alone (host_run_spawned's
// `spec.argv`); everything else is built from the script's `argv`, whose
// markers are what the scrubber makes of the values. test_run_secret_args
// holds the record, the answer, the time line and the output to it.
static STATUS_MUST_USE Status host_secret_args_fill(Host *h, Run *r, Pos pos, SpawnStrs argv, const HostEnvOpt *args, Buf *shown, SpawnStrs *filled, HostSecret *into) {
    PathsStrs names = args->names;
    *filled = argv;
    if (names.len == 0) return OK;
    SLICE_TYPE(SecretRoom, HostSecret);
    SecretRoom room = {into, names.len};
    for (size_t j = 0; j < names.len; j++) {
        Str k = at(names, j);
        Str given = at(args->values, j);
        Str v = S("");
        TRY(host_secret_read(h, r, pos, k, given, &v));
        at(room, j).name = k;
        at(room, j).value = v;
        if (buf_lit(shown, " argv ") != OK || buf_str(shown, k) != OK || buf_lit(shown, "=<secret ") != OK || buf_str(shown, given) != OK || buf_byte(shown, '>') != OK) return run_values_oom(r, pos);
    }
    OsStr *out = ARENA_ARRAY(r->ctx.values, OsStr, argv.len);
    if (out == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(ArgvRoom, OsStr);
    ArgvRoom aroom = {out, argv.len};
    for (size_t i = 0; i < argv.len; i++) {
        Str s = at(argv, i).s;
        Buf b = buf_make(r->ctx.values);
        HostSecretMark m;
        size_t from = 0;
        while (from < s.len && host_secret_mark_next(s, from, names, &m)) {
            if (buf_str(&b, str_sub(s, from, m.start)) != OK || buf_str(&b, at(room, m.which).value) != OK) return run_values_oom(r, pos);
            from = m.end;
        }
        if (buf_str(&b, str_sub(s, from, s.len)) != OK) return run_values_oom(r, pos);
        // The script's bytes and a secret's, each checked NUL-free
        // (host_argv_arg, host_secret_read).
        at(aroom, i) = os_str_own(buf_view(&b));
    }
    filled->ptr = out;
    filled->len = argv.len;
    return OK;
}

// ── a fenced secret: one program's few commands alone ──

// The fenced secret's file name, in a secrets mount (or a folder of one):
// the box's classic GitHub token with `public_repo`, which opens a pull
// request from a fork (cookbook/open_pull_request.shale).
// DRAGON: the secrets rule. `secrets` hands any secret to any program, and
// this token writes every public repository its owner may reach: a script
// could hand it to `gh pr merge`, `gh api -X POST`, `gh repo delete`, curl
// (`http`'s `bearer` among them) or a `gh` planted first on PATH.
// REMEDY: a run that names it is held to HOST_SECRET_FENCE_USES' shapes
// (host_secret_fence), each option by name and each value by its shape, a
// head's owner one of the remotes file's; it is the only secret, with no
// `env`, `want`, `unconfined`, `secret_args` or `stdout_secret`, and it runs
// as the system's own gh (host_unconfined_program) with HOST_UNCONFINED_PATH;
// in `secret_args` it is refused. It is checked before any secret is read.
// test_run_guards' a_fenced_secret_goes_to_gh_pr_create_view_and_close_alone
// plants each refusal. What is left: a file of the same bytes under another
// name, which only the secrets folder's owner can make.
#define HOST_SECRET_FENCED "github-pr-token"

#define HOST_FENCE_LEN(a) (sizeof(a) / sizeof *(a))

// One command the fenced secret may go to, `gh pr <command>`: its options
// that take the next word as their value, its options that take none, and
// whether it takes one positional word, a pull request's number. Every
// command needs `--repo`; `create` needs `--head`, `--title` and
// `--body-file` too, so gh never asks, forks or pushes.
typedef struct {
    const char *command; // cstr: a literal
    HostConfineArgs values;
    HostConfineArgs flags;
    bool number;
} HostFenceUse;

SLICE_TYPE(HostFenceUses, const HostFenceUse);

static const char *const HOST_FENCE_CREATE_VALUES[] = {"--repo", "--base", "--head", "--title", "--body-file"};
static const char *const HOST_FENCE_CREATE_FLAGS[] = {"--draft"};
static const char *const HOST_FENCE_VIEW_VALUES[] = {"--repo", "--json"};
static const char *const HOST_FENCE_CLOSE_VALUES[] = {"--repo"};

static const HostFenceUse HOST_SECRET_FENCE_USES[] = {
    {"create", {HOST_FENCE_CREATE_VALUES, HOST_FENCE_LEN(HOST_FENCE_CREATE_VALUES)}, {HOST_FENCE_CREATE_FLAGS, HOST_FENCE_LEN(HOST_FENCE_CREATE_FLAGS)}, false},
    {"view", {HOST_FENCE_VIEW_VALUES, HOST_FENCE_LEN(HOST_FENCE_VIEW_VALUES)}, {NULL, 0}, true},
    {"close", {HOST_FENCE_CLOSE_VALUES, HOST_FENCE_LEN(HOST_FENCE_CLOSE_VALUES)}, {NULL, 0}, true},
};

static bool host_fence_word_byte(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

// Whether `s` is a name GitHub gives an owner or a repository: letters,
// digits, `.`, `_` and `-`, not first `.` or `-`.
static bool host_fence_name(Str s) {
    if (s.len == 0 || str_byte(s, 0) == '.' || str_byte(s, 0) == '-') return false;
    for (size_t i = 0; i < s.len; i++) {
        if (!host_fence_word_byte(str_byte(s, i))) return false;
    }
    return true;
}

// Whether `s` is a branch's name as the fence takes it: name bytes and `/`,
// not first `-`, `.` or `/`, and no `..`.
static bool host_fence_branch(Str s) {
    if (s.len == 0 || str_byte(s, 0) == '-' || str_byte(s, 0) == '.' || str_byte(s, 0) == '/') return false;
    if (mem_find(s.ptr, s.len, "..", 2) != SIZE_MAX) return false;
    for (size_t i = 0; i < s.len; i++) {
        uint8_t c = str_byte(s, i);
        if (!host_fence_word_byte(c) && c != '/') return false;
    }
    return true;
}

// Whether `s` is `OWNER/NAME` with no host before it: gh sends GH_TOKEN to
// github.com alone, and a `HOST/OWNER/NAME` names another host.
static bool host_fence_repo(Str s) {
    const char *slash = str_find_byte(s, '/');
    if (slash == NULL) return false;
    size_t at = (size_t)(slash - s.ptr);
    return host_fence_name(str_sub(s, 0, at)) && host_fence_name(str_sub(s, at + 1, s.len));
}

// Whether the script's secret path `given` is the fenced secret's file: its
// OS path's last name, so any mount's name or folder for it is the same.
static STATUS_MUST_USE Status host_secret_is_fenced(Host *h, Run *r, Pos pos, Str given, bool *out) {
    OsStr path;
    TRY(host_secret_os_path(h, r, pos, given, false, &path));
    *out = str_eq(host_base_name(path.s), S(HOST_SECRET_FENCED));
    return OK;
}

static STATUS_MUST_USE Status host_fence_refused(Run *r, Pos pos, Str why) {
    return host_fail(r, pos, CLASS_REFUSED, S("`run`: the secret " HOST_SECRET_FENCED " goes to `gh pr create`, `gh pr view` and `gh pr close` alone, held to their options (c/host/host_run_secrets.c's HOST_SECRET_FENCE_USES), even with a want: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(why)));
}

// A refusal that names a word of the script's argv.
static STATUS_MUST_USE Status host_fence_refused_word(Run *r, Pos pos, const char *what, Str word) { // cstr: a literal
    Buf b = buf_make(r->ctx.values);
    if (buf_str(&b, (Str){what, strlen(what)}) != OK || buf_str(&b, word) != OK) return run_values_oom(r, pos);
    return host_fence_refused(r, pos, buf_view(&b));
}

// Holds a fenced command's words after `gh pr <command>` to `use`.
static STATUS_MUST_USE Status host_fence_command(Run *r, Pos pos, SpawnStrs argv, const HostFenceUse *use) {
    bool seen[8] = {false};
    SLICE_TYPE(Seen, bool);
    Seen s = {seen, 8};
    size_t positional = 0;
    for (size_t i = 3; i < argv.len; i++) {
        Str w = at(argv, i).s;
        if (!str_starts_with(w, S("-"))) {
            if (!use->number) return host_fence_refused_word(r, pos, "`gh pr create` takes no word but its options' values, not ", w);
            bool digits = w.len > 0 && w.len <= 12;
            for (size_t k = 0; k < w.len && digits; k++) digits = str_byte(w, k) >= '0' && str_byte(w, k) <= '9';
            if (!digits || positional > 0) return host_fence_refused_word(r, pos, "it names one pull request by its number, not ", w);
            positional++;
            continue;
        }
        bool flag = false;
        for (size_t k = 0; k < use->flags.len; k++) flag = flag || str_eq(w, (Str){at(use->flags, k), strlen(at(use->flags, k))});
        if (flag) continue;
        size_t which = use->values.len;
        for (size_t k = 0; k < use->values.len; k++) {
            if (str_eq(w, (Str){at(use->values, k), strlen(at(use->values, k))})) which = k;
        }
        if (which == use->values.len) return host_fence_refused_word(r, pos, "an option it does not name: ", w);
        if (at(s, which)) return host_fence_refused_word(r, pos, "an option given twice: ", w);
        at(s, which) = true;
        if (i + 1 >= argv.len) return host_fence_refused_word(r, pos, "an option with no value: ", w);
        Str v = at(argv, ++i).s;
        bool ok = v.len > 0;
        if (str_eq(w, S("--repo"))) ok = host_fence_repo(v);
        if (str_eq(w, S("--base"))) ok = host_fence_branch(v);
        if (str_eq(w, S("--json"))) {
            for (size_t k = 0; k < v.len && ok; k++) {
                uint8_t c = str_byte(v, k);
                ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ',';
            }
        }
        if (str_eq(w, S("--head"))) {
            const char *colon = str_find_byte(v, ':');
            ok = colon != NULL && host_fence_name(str_sub(v, 0, (size_t)(colon - v.ptr))) && host_fence_branch(str_sub(v, (size_t)(colon - v.ptr) + 1, v.len));
            if (ok) {
                Str remotes = S("");
                TRY(host_unconfined_remotes(r, pos, &remotes));
                if (!host_remote_owner_listed(remotes, str_sub(v, 0, (size_t)(colon - v.ptr)))) return host_fence_refused_word(r, pos, "a head's owner is a github.com owner of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), not the one in ", v);
            }
        }
        if (!ok) return host_fence_refused_word(r, pos, "a value not of its option's shape (`--repo OWNER/NAME`, `--head OWNER:BRANCH`, `--base BRANCH`, `--json a,b`, no other empty): ", v);
    }
    // `--repo` is the first value of each; `create` needs all but `--base`.
    if (!at(s, 0)) return host_fence_refused(r, pos, S("it names its repository, `--repo OWNER/NAME`"));
    if (str_eq((Str){use->command, strlen(use->command)}, S("create")) && (!at(s, 2) || !at(s, 3) || !at(s, 4))) return host_fence_refused(r, pos, S("`gh pr create` names its `--head`, `--title` and `--body-file`, so gh never asks, forks or pushes"));
    if (use->number && positional == 0) return host_fence_refused(r, pos, S("it names one pull request by its number"));
    return OK;
}

// Whether this run hands the fenced secret over, into `*fenced`, and if so
// holds it to the DRAGON at HOST_SECRET_FENCED. `env`, `secrets` and
// `secret_args` are the run's; `want`, `unconfined` and `stdout_secret` say
// whether each was given.
static STATUS_MUST_USE Status host_secret_fence(Host *h, Run *r, Pos pos, SpawnStrs argv, const HostEnvOpt *env, const HostEnvOpt *secrets, const HostEnvOpt *secret_args, bool want, bool unconfined, bool stdout_secret, bool *fenced) {
    *fenced = false;
    for (size_t j = 0; j < secret_args->names.len; j++) {
        bool is = false;
        TRY(host_secret_is_fenced(h, r, pos, at(secret_args->values, j), &is));
        if (is) return host_fence_refused(r, pos, S("it goes in `secrets`, never `secret_args`"));
    }
    for (size_t j = 0; j < secrets->names.len; j++) {
        bool is = false;
        TRY(host_secret_is_fenced(h, r, pos, at(secrets->values, j), &is));
        *fenced = *fenced || is;
    }
    if (!*fenced) return OK;
    if (secrets->names.len != 1 || secret_args->names.len != 0) return host_fence_refused(r, pos, S("it is the run's only secret"));
    if (!str_eq(at(secrets->names, 0), S("GH_TOKEN"))) return host_fence_refused(r, pos, S("it goes to gh as GH_TOKEN"));
    if (env->names.len > 0 || want || unconfined || stdout_secret) return host_fence_refused(r, pos, S("its run takes no `env`, `want`, `unconfined` or `stdout_secret`"));
    if (!str_eq(at(argv, 0).s, S("gh"))) return host_fence_refused_word(r, pos, "its program is `gh`, named bare (the system's own), not ", at(argv, 0).s);
    if (argv.len < 3 || !str_eq(at(argv, 1).s, S("pr"))) return host_fence_refused(r, pos, S("its command is `gh pr create`, `gh pr view` or `gh pr close`"));
    HostFenceUses uses = {HOST_SECRET_FENCE_USES, HOST_FENCE_LEN(HOST_SECRET_FENCE_USES)};
    for (size_t k = 0; k < uses.len; k++) {
        const HostFenceUse *use = &at(uses, k);
        if (str_eq(at(argv, 2).s, (Str){use->command, strlen(use->command)})) return host_fence_command(r, pos, argv, use);
    }
    return host_fence_refused_word(r, pos, "its command is `gh pr create`, `gh pr view` or `gh pr close`, not gh pr ", at(argv, 2).s);
}

// A fenced run's argv and environment: the system's own gh by its path, and
// HOST_UNCONFINED_PATH for the PATH (gh runs git along it). It stays
// confined.
static STATUS_MUST_USE Status host_secret_fence_pin(Host *h, Run *r, Pos pos, SpawnStrs argv, SpawnStrs *out, SpawnStrs *env) {
    Str program = S("");
    TRY(host_unconfined_program(h, r, pos, at(argv, 0).s, S("gh"), &program));
    OsStr *copy = ARENA_ARRAY(r->ctx.values, OsStr, argv.len);
    if (copy == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(CopyRoom, OsStr);
    CopyRoom words = {copy, argv.len};
    at(words, 0) = os_str_own(program);
    for (size_t i = 1; i < argv.len; i++) at(words, i) = at(argv, i);
    out->ptr = copy;
    out->len = argv.len;
    return host_unconfined_env(r, pos, env, S(""), false);
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
