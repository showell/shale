// host_unconfined.c: `run`'s `unconfined = "tag: why"`, the programs `run`
// starts outside the Landlock confinement (host_confine.c), because what
// they need no rule allows: ssh's key files. There are four, each held to a
// narrow shape: a `git push`, and `ssh`, `scp` and `rsync` to a host of the
// hosts file alone (a deploy's). Every other program, and every
// other shape of these, is refused; a `want` is beside the point, and the
// run's record says `(unconfined: ...)`.
//
// Include host.c, host_confine.c and host_refusals.c first.

// The hosts file, the one home of the hosts an unconfined `ssh`, `scp` or
// `rsync` may reach: the box's own, beside the mounts file,
// `~/.config/shale/hosts`, or the file SHALE_HOSTS names (a test's). One
// host a line, `user@host`, as the deploy scripts write it (and in a copy's
// operand `user@host:path`); `#` lines and blank ones are skipped. Shale
// ships none: no file is no host, and a host by another spelling (no user,
// a name for the address, an `ssh://` URL) is another host, refused.
enum { HOST_UNCONFINED_HOSTS_MAX = 64 * 1024 };

// git's own settings an unconfined push runs under, ahead of the script's
// argv: no hook, no fsmonitor and no credential helper of the repository's
// (each a program git would start, outside the confinement), and ssh itself
// as ssh. They come before every `-c` of the script's, which
// host_unconfined_git refuses, so none is undone.
static const char *const HOST_UNCONFINED_GIT[] = {"-c", "core.hooksPath=/dev/null", "-c", "core.fsmonitor=false", "-c", "credential.helper=", "-c", "core.sshCommand=ssh"};

// The options an unconfined `scp` takes: quiet, recursive, times kept, and
// HOST_UNCONFINED_SSH_SETTINGS' `-o`s. Any other (`-F`, `-S`, `-J`, `-i`)
// could name a program, a config or a hop.
static const char *const HOST_UNCONFINED_SCP_FLAGS[] = {"-q", "-r", "-p"};

// The ssh settings an unconfined `ssh` or `scp` takes as `-o SETTING` (or
// `-oSETTING`), and an rsync's `-e ssh` after it: no prompt, and none of
// ssh's own messages, which name the bare address the scrubber does not know
// when it came from `secret_args`. Each is exactly as written here; no other
// setting is taken, since one (ProxyCommand, LocalCommand, Include,
// IdentityAgent) could name a program, a file or a hop.
static const char *const HOST_UNCONFINED_SSH_SETTINGS[] = {"BatchMode=yes", "LogLevel=QUIET"};

// The letters an unconfined `rsync`'s short options may bundle (`-avz`,
// `-rlt`), and its long options, each whole or, ending in `=`, by its prefix.
// `-e`/`--rsh` is taken only as `ssh`, with HOST_UNCONFINED_SSH_SETTINGS'
// `-o`s after it.
static const char HOST_UNCONFINED_RSYNC_LETTERS[] = "avzrltpgoDqcnhP";
static const char *const HOST_UNCONFINED_RSYNC_LONG[] = {"--delete", "--mkpath", "--checksum", "--dry-run", "--itemize-changes", "--chmod=", "--exclude="};

SLICE_TYPE(HostConfineArgs, const char *const);

// An unconfined program's argv twice over: `filled`, what the program is
// handed, `secret_args`' values in place of their markers
// (host_secret_args_fill), which every check reads; and `script`, the
// script's own, word for word the same but for the markers, which every
// refusal names.
// DRAGON: the secrets rule. A check of the script's argv would see
// `<secret PROD>` where the program sees a host, and pass or refuse a host
// nobody checked; a refusal built from the filled argv would print the
// value.
// REMEDY: the checks read `filled` alone, and a refusal names `script`'s
// word at the same place (host_unconfined_shown), never `filled`'s.
// test_run_guards' an_unconfined_deploy_s_host_may_be_a_secret_and_is_never_
// shown plants both.
typedef struct {
    SpawnStrs filled;
    SpawnStrs script;
} HostUnconfinedArgv;

// The program's word `i`, as the program sees it.
static Str host_unconfined_word(const HostUnconfinedArgv *a, size_t i) {
    return at(a->filled, i).s;
}

// The word a refusal of word `i` names: the script's, a secret's marker in
// place of its value.
static Str host_unconfined_shown(const HostUnconfinedArgv *a, size_t i) {
    return at(a->script, i).s;
}

// Whether `s` is one of `names`, or begins with one of them that ends in
// `=`.
static bool host_unconfined_named(HostConfineArgs names, Str s) {
    for (size_t i = 0; i < names.len; i++) {
        Str n = {at(names, i), strlen(at(names, i))};
        if (str_eq(s, n)) return true;
        if (n.len > 0 && str_byte(n, n.len - 1) == '=' && str_starts_with(s, n)) return true;
    }
    return false;
}

// Whether `line`, trimmed, is a host's line: `user@host`, one `@` with text
// on both sides, and no space, tab, `:` or `/`.
static bool host_unconfined_host_line(Str line) {
    size_t at_signs = 0;
    size_t at_at = 0;
    for (size_t i = 0; i < line.len; i++) {
        uint8_t c = str_byte(line, i);
        if (c == ' ' || c == '\t' || c == ':' || c == '/') return false;
        if (c == '@') {
            at_signs++;
            at_at = i;
        }
    }
    return at_signs == 1 && at_at > 0 && at_at + 1 < line.len;
}

// The hosts file's text into `*out`, each line checked: "" for none (no
// SHALE_HOSTS and no ~/.config/shale/hosts). A SHALE_HOSTS that names no
// file, a file that cannot be read and a line that is not a host's stop
// the run, naming the file and the line.
static STATUS_MUST_USE Status host_unconfined_hosts(Run *r, Pos pos, Str *out) {
    Arena *a = r->ctx.values;
    PathRun *pr = &builtin_files_of(r)->path;
    *out = S("");
    const char *pinned = getenv("SHALE_HOSTS"); // cstr: the environment's
    const char *home = getenv("HOME"); // cstr: the environment's
    Str path = S("");
    if (pinned != NULL) {
        path = (Str){pinned, strlen(pinned)};
    } else if (home != NULL && str_starts_with((Str){home, strlen(home)}, S("/"))) {
        TRY(paths_cat(pr, pos, (Str){home, strlen(home)}, S("/.config/shale/hosts"), S(""), &path));
    } else {
        return OK;
    }
    Str text = S("");
    FileSystemErr e = file_system_read_file(a, os_str_own(path), HOST_UNCONFINED_HOSTS_MAX, &text);
    if (e == FILE_SYSTEM_OUT_OF_MEMORY) return run_values_oom(r, pos);
    if (e == FILE_SYSTEM_FILE_NOT_FOUND && pinned == NULL) return OK; // skip: no hosts file is no host allowed
    if (e != FILE_SYSTEM_OK) return host_fail(r, pos, CLASS_HOST, S("`run`'s `unconfined`: the hosts file {s} cannot be read: {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(path), MESSAGE_FROM_HOST(file_system_err_name(e))));
    size_t from = 0;
    size_t n = 0;
    while (from <= text.len) {
        size_t to = path_part_end_at(text, from, '\n');
        Str line = mounts_trim(str_sub(text, from, to), true);
        from = to + 1;
        n++;
        if (line.len == 0 || str_byte(line, 0) == '#') continue;
        if (!host_unconfined_host_line(line)) return host_fail(r, pos, CLASS_HOST, S("`run`'s `unconfined`: the hosts file {s}, line {s}, is not a host's: one `user@host` a line"), MESSAGE_ARGS(MESSAGE_FROM_HOST(path), MESSAGE_FROM_HOST(run_unum(r, n))));
    }
    *out = text;
    return OK;
}

// Whether `host` is a line of the hosts file's text `hosts`.
static bool host_unconfined_host(Str hosts, Str host) {
    size_t from = 0;
    while (from <= hosts.len) {
        size_t to = path_part_end_at(hosts, from, '\n');
        Str line = mounts_trim(str_sub(hosts, from, to), true);
        from = to + 1;
        if (line.len > 0 && str_eq(line, host)) return true;
    }
    return false;
}

// Whether the operand `s` is remote, `host:path` (a `:` before any `/`),
// its host into `*host`.
static bool host_unconfined_remote(Str s, Str *host) {
    for (size_t i = 0; i < s.len; i++) {
        uint8_t c = str_byte(s, i);
        if (c == '/') return false;
        if (c == ':') {
            *host = str_sub(s, 0, i);
            return true;
        }
    }
    return false;
}

static STATUS_MUST_USE Status host_unconfined_no_host(Run *r, Pos pos, Str prog, Str host) {
    return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} goes only to a host of the hosts file (~/.config/shale/hosts, or SHALE_HOSTS's), not `{s}`"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(host)));
}

// The refusal of word `i`'s host, `host` as the program sees it: named as
// the script wrote it when a secret filled the word (its host part, or the
// word whole), else as `host`.
static STATUS_MUST_USE Status host_unconfined_no_host_at(Run *r, Pos pos, Str prog, const HostUnconfinedArgv *a, size_t i, Str host) {
    Str shown = host_unconfined_shown(a, i);
    if (str_eq(shown, host_unconfined_word(a, i))) return host_unconfined_no_host(r, pos, prog, host);
    Str shown_host = shown;
    (void)host_unconfined_remote(shown, &shown_host); // ignored: a word with no `host:` is named whole
    return host_unconfined_no_host(r, pos, prog, shown_host);
}

// Whether `s` is one of HOST_UNCONFINED_SSH_SETTINGS.
static bool host_unconfined_ssh_setting(Str s) {
    HostConfineArgs settings = {HOST_UNCONFINED_SSH_SETTINGS, sizeof HOST_UNCONFINED_SSH_SETTINGS / sizeof *HOST_UNCONFINED_SSH_SETTINGS};
    for (size_t i = 0; i < settings.len; i++) {
        if (str_eq(s, (Str){at(settings, i), strlen(at(settings, i))})) return true;
    }
    return false;
}

static STATUS_MUST_USE Status host_unconfined_bad_setting(Run *r, Pos pos, Str prog, Str shown) {
    return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes `-o` for BatchMode=yes and LogLevel=QUIET alone, not `{s}`: another setting could name a program, a file or a hop"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(shown)));
}

// An ssh `-o` at word `*i`, `-o SETTING` or `-oSETTING`, held to
// HOST_UNCONFINED_SSH_SETTINGS: `*taken` when word `*i` is one, `*i` moved
// past it.
static STATUS_MUST_USE Status host_unconfined_ssh_o(Run *r, Pos pos, Str prog, const HostUnconfinedArgv *a, size_t *i, bool *taken) {
    *taken = false;
    Str w = host_unconfined_word(a, *i);
    if (str_eq(w, S("-o"))) {
        if (*i + 1 >= a->filled.len) return host_unconfined_bad_setting(r, pos, prog, S(""));
        if (!host_unconfined_ssh_setting(host_unconfined_word(a, *i + 1))) return host_unconfined_bad_setting(r, pos, prog, host_unconfined_shown(a, *i + 1));
        *i += 2;
        *taken = true;
    } else if (str_starts_with(w, S("-o"))) {
        if (!host_unconfined_ssh_setting(str_sub(w, 2, w.len))) return host_unconfined_bad_setting(r, pos, prog, host_unconfined_shown(a, *i));
        *i += 1;
        *taken = true;
    }
    return OK;
}

// Whether `v`, an rsync's remote shell, is `ssh` with only
// HOST_UNCONFINED_SSH_SETTINGS' `-o`s after it, each word one space apart.
static bool host_unconfined_rsh_ok(Str v) {
    if (!str_starts_with(v, S("ssh"))) return false;
    size_t from = 3;
    bool want_setting = false;
    while (from < v.len) {
        if (str_byte(v, from) != ' ') return false;
        from++;
        size_t to = path_part_end_at(v, from, ' ');
        Str w = str_sub(v, from, to);
        from = to;
        if (want_setting) {
            if (!host_unconfined_ssh_setting(w)) return false;
            want_setting = false;
        } else if (str_eq(w, S("-o"))) {
            want_setting = true;
        } else if (!(str_starts_with(w, S("-o")) && host_unconfined_ssh_setting(str_sub(w, 2, w.len)))) {
            return false;
        }
    }
    return !want_setting;
}

// A local operand `p` of an unconfined copy, from `cwd`, held to the root
// and the mounts (a secrets mount's folder in none: host_confine_secrets_apart
// came first), every link followed; `written` when the copy writes it, which
// a read-only mount refuses.
// DRAGON: the file system's boundaries. An unconfined `scp` or `rsync`
// reads and writes any file of the user's, so a key file could go to the
// host, or a file the host gives could land over one.
// REMEDY: each local operand is resolved (realpath, or its folder's when it
// is not yet there) and must lie under the root or a mount, a written one
// under a writable one. test_run_guards' an_unconfined_run_is_a_git_push_
// or_a_deploy_to_the_prod_host_alone plants an operand outside.
static STATUS_MUST_USE Status host_unconfined_local(Host *h, Run *r, Pos pos, Str prog, Str cwd, Str p, Str shown, bool written) {
    Arena *a = r->ctx.values;
    PathRun *pr = &builtin_files_of(r)->path;
    Str abs = S("");
    if (!paths_resolve_posix(a, cwd, p, &abs)) return run_values_oom(r, pos);
    char *c = arena_cstr(a, os_str_own(abs));
    char *real = ARENA_ARRAY(a, char, PATH_MAX);
    if (c == NULL || real == NULL) return run_values_oom(r, pos);
    Str resolved = S("");
    if (realpath(c, real) != NULL) {
        resolved = (Str){real, strlen(real)};
    } else {
        Str dir = S("");
        Str name = path_basename(abs);
        if (!paths_resolve_posix(a, abs, S(".."), &dir)) return run_values_oom(r, pos);
        char *dc = arena_cstr(a, os_str_own(dir));
        if (dc == NULL) return run_values_oom(r, pos);
        if (realpath(dc, real) == NULL) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s}: its local path {s} is not there, nor its folder"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(shown)));
        TRY(paths_cat(pr, pos, (Str){real, strlen(real)}, S("/"), name, &resolved));
    }
    if (mounts_in_folder(resolved, paths_root_of(&h->paths, pr))) return OK;
    for (size_t k = 0; k < h->paths.mounts.len; k++) {
        const Mount *m = &at(h->paths.mounts, k);
        if (m->secrets || !mounts_in_folder(resolved, paths_run_dir_of(&h->paths, pr, m))) continue; // skip: not this mount's
        if (written && m->access == MOUNT_READ_ONLY) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} would write {s}, in the read-only mount `{s}`"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(shown), MESSAGE_FROM_HOST(m->name)));
        return OK;
    }
    return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s}: its local path {s} is under neither the root nor a mount"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(shown)));
}

// A copy's operands, words from `first`: two at least, each remote one to
// an allowed host, each local one under the mounts, the last written.
static STATUS_MUST_USE Status host_unconfined_operands(Host *h, Run *r, Pos pos, Str prog, Str cwd, const HostUnconfinedArgv *a, size_t first, Str hosts) {
    if (a->filled.len < first + 2) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes a source and a destination after its options"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog)));
    for (size_t i = first; i < a->filled.len; i++) {
        Str s = host_unconfined_word(a, i);
        Str host = S("");
        if (str_starts_with(s, S("-"))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes its options before its operands, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(host_unconfined_shown(a, i))));
        if (host_unconfined_remote(s, &host)) {
            if (str_starts_with(str_sub(s, host.len, s.len), S("::"))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes no rsync daemon's module (`host::module`), not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(host_unconfined_shown(a, i))));
            if (!host_unconfined_host(hosts, host)) return host_unconfined_no_host_at(r, pos, prog, a, i, host);
        } else {
            TRY(host_unconfined_local(h, r, pos, prog, cwd, s, host_unconfined_shown(a, i), i + 1 == a->filled.len));
        }
    }
    return OK;
}

// `ssh [-o SETTING]... <host> [command...]`: HOST_UNCONFINED_SSH_SETTINGS'
// `-o`s alone, then the host.
static STATUS_MUST_USE Status host_unconfined_ssh(Run *r, Pos pos, const HostUnconfinedArgv *a, Str hosts) {
    size_t i = 1;
    while (i < a->filled.len && str_starts_with(host_unconfined_word(a, i), S("-"))) {
        bool taken = false;
        TRY(host_unconfined_ssh_o(r, pos, S("ssh"), a, &i, &taken));
        if (!taken) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ssh takes no option but `-o`, not {s}: an option could name a program, a config or a hop"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(host_unconfined_shown(a, i))));
    }
    if (i >= a->filled.len) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ssh names its host"));
    if (!host_unconfined_host(hosts, host_unconfined_word(a, i))) return host_unconfined_no_host_at(r, pos, S("ssh"), a, i, host_unconfined_word(a, i));
    return OK;
}

// `scp [-q -r -p -o SETTING]... operands...`.
static STATUS_MUST_USE Status host_unconfined_scp(Host *h, Run *r, Pos pos, Str cwd, const HostUnconfinedArgv *a, Str hosts) {
    HostConfineArgs flags = {HOST_UNCONFINED_SCP_FLAGS, sizeof HOST_UNCONFINED_SCP_FLAGS / sizeof *HOST_UNCONFINED_SCP_FLAGS};
    size_t i = 1;
    while (i < a->filled.len && str_starts_with(host_unconfined_word(a, i), S("-"))) {
        bool taken = false;
        TRY(host_unconfined_ssh_o(r, pos, S("scp"), a, &i, &taken));
        if (taken) continue;
        if (!host_unconfined_named(flags, host_unconfined_word(a, i))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` scp takes -q, -r, -p and `-o`, not {s}: another option could name a program, a config or a hop"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(host_unconfined_shown(a, i))));
        i++;
    }
    return host_unconfined_operands(h, r, pos, S("scp"), cwd, a, i, hosts);
}

// Whether `s`, a short option bundle after its `-`, holds only
// HOST_UNCONFINED_RSYNC_LETTERS.
static bool host_unconfined_rsync_letters(Str s) {
    Str allowed = {HOST_UNCONFINED_RSYNC_LETTERS, sizeof HOST_UNCONFINED_RSYNC_LETTERS - 1};
    if (s.len == 0) return false;
    for (size_t i = 0; i < s.len; i++) {
        if (mem_find_byte(allowed.ptr, (char)str_byte(s, i), allowed.len) == NULL) return false;
    }
    return true;
}

static STATUS_MUST_USE Status host_unconfined_bad_rsh(Run *r, Pos pos) {
    return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` rsync takes `-e ssh` alone of a remote shell, with `-o BatchMode=yes` and `-o LogLevel=QUIET` after it: any other is a program it would start"));
}

// `rsync [options] operands...`: HOST_UNCONFINED_RSYNC_*'s options, and
// `-e`/`--rsh` alone as host_unconfined_rsh_ok's `ssh`.
static STATUS_MUST_USE Status host_unconfined_rsync(Host *h, Run *r, Pos pos, Str cwd, const HostUnconfinedArgv *a, Str hosts) {
    HostConfineArgs longs = {HOST_UNCONFINED_RSYNC_LONG, sizeof HOST_UNCONFINED_RSYNC_LONG / sizeof *HOST_UNCONFINED_RSYNC_LONG};
    size_t n = a->filled.len;
    size_t i = 1;
    while (i < n && str_starts_with(host_unconfined_word(a, i), S("-"))) {
        Str o = host_unconfined_word(a, i);
        if (str_eq(o, S("-e")) || str_eq(o, S("--rsh"))) {
            if (i + 1 >= n || !host_unconfined_rsh_ok(host_unconfined_word(a, i + 1))) return host_unconfined_bad_rsh(r, pos);
            i += 2;
            continue;
        }
        if (str_starts_with(o, S("--rsh=")) || str_starts_with(o, S("-e"))) {
            Str v = str_starts_with(o, S("--rsh=")) ? str_sub(o, 6, o.len) : str_sub(o, 2, o.len);
            if (!host_unconfined_rsh_ok(v)) return host_unconfined_bad_rsh(r, pos);
            i++;
            continue;
        }
        bool short_ok = !str_starts_with(o, S("--")) && host_unconfined_rsync_letters(str_sub(o, 1, o.len));
        if (!short_ok && !host_unconfined_named(longs, o)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` rsync does not take {s}: its options are HOST_UNCONFINED_RSYNC_LETTERS' and HOST_UNCONFINED_RSYNC_LONG's (c/host/host_unconfined.c)"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(host_unconfined_shown(a, i))));
        i++;
    }
    for (size_t k = i; k < n; k++) {
        if (str_starts_with(host_unconfined_word(a, k), S("rsync://"))) return host_unconfined_no_host(r, pos, S("rsync"), host_unconfined_shown(a, k));
    }
    return host_unconfined_operands(h, r, pos, S("rsync"), cwd, a, i, hosts);
}

// `git ... push ...`: no `-c` but `color.ui=never` (the git prelude's), no
// `--receive-pack` or `--exec`.
static STATUS_MUST_USE Status host_unconfined_git(Run *r, Pos pos, const HostUnconfinedArgv *a, Str why) {
    bool push_word = false;
    for (size_t i = 1; i < a->filled.len; i++) {
        Str w = host_unconfined_word(a, i);
        push_word = push_word || str_eq(w, S("push"));
        if (str_starts_with(w, S("--receive-pack")) || str_starts_with(w, S("--exec"))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push takes no `--receive-pack` or `--exec`: each names a program it would start outside the confinement"));
        if (str_eq(w, S("-c")) && (i + 1 >= a->filled.len || !str_eq(host_unconfined_word(a, i + 1), S("color.ui=never")))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push takes no `-c` but `color.ui=never`: a setting could name a program git starts outside the confinement"));
    }
    if (!push_word) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ({s}) is for a `git push`, or `ssh`, `scp` or `rsync` to a host of the hosts file; every other program runs confined to the mounts"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(why)));
    return OK;
}

// `unconfined = "tag: why"`, held to its shapes, into `*out`: the argv to
// run, `filled` (the program's, `secret_args`' values in place; `argv` when
// there are none), git's with HOST_UNCONFINED_GIT after `git`. The shapes
// are checked on `filled`, and a refusal names `argv`'s word
// (HostUnconfinedArgv).
// DRAGON: the file system's boundaries. An unconfined program runs every
// program its settings name with all the user's file powers: a git hook,
// an ssh ProxyCommand, an rsync remote shell, a credential helper; and its
// environment could name one too.
// REMEDY: the program is `git`, `ssh`, `scp` or `rsync` by its name and the
// file it reaches; each is held to its shape above; no `env` but git's
// GIT_TERMINAL_PROMPT; no secrets folder inside the root or a mount. What
// is left: a git repository's `url.*.insteadOf` and a remote's
// `receivepack`, read from its config, and the user's own ~/.ssh/config,
// which no mount should make writable. test_run_guards'
// an_unconfined_run_is_a_git_push_or_a_deploy_to_the_prod_host_alone plants
// each refusal.
static STATUS_MUST_USE Status host_run_unconfined(Host *h, Run *r, Pos pos, SpawnStrs argv, SpawnStrs filled, const HostEnvOpt *env, Str why, Str cwd, SpawnStrs *out) {
    Str reached = S("");
    bool found = host_resolved_name(h, r->ctx.values, at(argv, 0), cwd, &reached);
    Str base = host_base_name(at(argv, 0).s);
    bool named = found && str_eq(base, reached);
    bool git = named && str_eq(base, S("git"));
    bool ssh_family = named && (str_eq(base, S("ssh")) || str_eq(base, S("scp")) || str_eq(base, S("rsync")));
    if (!git && !ssh_family) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ({s}) is for a `git push`, or `ssh`, `scp` or `rsync` to a host of the hosts file; every other program runs confined to the mounts"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(why)));
    for (size_t j = 0; j < env->names.len; j++) {
        Str k = at(env->names, j);
        if (!(git && str_eq(k, S("GIT_TERMINAL_PROMPT")))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes no `env` (git: but GIT_TERMINAL_PROMPT), not {s}: a variable could name a program it starts"), MESSAGE_ARGS(MESSAGE_FROM_HOST(base), MESSAGE_FROM_SCRIPT(k)));
    }
    TRY(host_confine_secrets_apart(h, r, pos, S("run")));
    // host_secret_args_fill fills word for word, the program's own first.
    if (filled.len != argv.len) return host_fail_plain(r, pos, CLASS_HOST, S("`run`'s `unconfined`: the filled argv is not the script's, word for word"));
    HostUnconfinedArgv a = {filled, argv};
    *out = filled;
    Str hosts = S("");
    if (ssh_family) TRY(host_unconfined_hosts(r, pos, &hosts));
    if (str_eq(base, S("ssh"))) return host_unconfined_ssh(r, pos, &a, hosts);
    if (str_eq(base, S("scp"))) return host_unconfined_scp(h, r, pos, cwd, &a, hosts);
    if (str_eq(base, S("rsync"))) return host_unconfined_rsync(h, r, pos, cwd, &a, hosts);
    TRY(host_unconfined_git(r, pos, &a, why));
    HostConfineArgs fixed = {HOST_UNCONFINED_GIT, sizeof HOST_UNCONFINED_GIT / sizeof *HOST_UNCONFINED_GIT};
    size_t n = filled.len + fixed.len;
    OsStr *all = ARENA_ARRAY(r->ctx.values, OsStr, n);
    if (all == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(ArgvRoom, OsStr);
    ArgvRoom room = {all, n};
    at(room, 0) = at(filled, 0);
    for (size_t i = 0; i < fixed.len; i++) at(room, 1 + i) = os_str_own((Str){at(fixed, i), strlen(at(fixed, i))});
    for (size_t i = 1; i < filled.len; i++) at(room, fixed.len + i) = at(filled, i);
    out->ptr = all;
    out->len = n;
    return OK;
}
