// host_unconfined.c: `run`'s `unconfined = "tag: why"`, the programs `run`
// starts outside the Landlock confinement (host_confine.c), because what
// they need no rule allows: ssh's key files. There are four, each held to a
// narrow shape: a `git push`, and `ssh`, `scp` and `rsync` to a host of
// HOST_UNCONFINED_HOSTS alone (a deploy's). Every other program, and every
// other shape of these, is refused; a `want` is beside the point, and the
// run's record says `(unconfined: ...)`.
//
// Include host.c, host_confine.c and host_refusals.c first.

// The one home of the hosts an unconfined `ssh`, `scp` or `rsync` may reach,
// as the deploy scripts write them (roc-chat's tools/deploy.shale, roc-apps'
// site/deploy.sh, angry-gopher's ops/deploy): `user@host`, and in a copy's
// operand `user@host:path`. A host by another spelling (no user, a name for
// the address, an `ssh://` URL) is another host, and refused.
static const char *const HOST_UNCONFINED_HOSTS[] = {"steve@162.243.1.123"};

// git's own settings an unconfined push runs under, ahead of the script's
// argv: no hook, no fsmonitor and no credential helper of the repository's
// (each a program git would start, outside the confinement), and ssh itself
// as ssh. They come before every `-c` of the script's, which
// host_unconfined_git refuses, so none is undone.
static const char *const HOST_UNCONFINED_GIT[] = {"-c", "core.hooksPath=/dev/null", "-c", "core.fsmonitor=false", "-c", "credential.helper=", "-c", "core.sshCommand=ssh"};

// The options an unconfined `scp` takes: quiet, recursive, times kept. Any
// other (`-o`, `-F`, `-S`, `-J`, `-i`) could name a program, a config or a
// hop.
static const char *const HOST_UNCONFINED_SCP_FLAGS[] = {"-q", "-r", "-p"};

// The letters an unconfined `rsync`'s short options may bundle (`-avz`,
// `-rlt`), and its long options, each whole or, ending in `=`, by its prefix.
// `-e`/`--rsh` is taken only as plain `ssh`.
static const char HOST_UNCONFINED_RSYNC_LETTERS[] = "avzrltpgoDqcnhP";
static const char *const HOST_UNCONFINED_RSYNC_LONG[] = {"--delete", "--mkpath", "--checksum", "--dry-run", "--itemize-changes", "--chmod=", "--exclude="};

SLICE_TYPE(HostConfineArgs, const char *const);

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

// Whether `host` is one of HOST_UNCONFINED_HOSTS.
static bool host_unconfined_host(Str host) {
    HostConfineArgs hosts = {HOST_UNCONFINED_HOSTS, sizeof HOST_UNCONFINED_HOSTS / sizeof *HOST_UNCONFINED_HOSTS};
    for (size_t i = 0; i < hosts.len; i++) {
        if (str_eq(host, ((Str){at(hosts, i), strlen(at(hosts, i))}))) return true;
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
    return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} goes only to a host of HOST_UNCONFINED_HOSTS (c/host/host_unconfined.c), not `{s}`"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(host)));
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
static STATUS_MUST_USE Status host_unconfined_local(Host *h, Run *r, Pos pos, Str prog, Str cwd, Str p, bool written) {
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
        if (realpath(dc, real) == NULL) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s}: its local path {s} is not there, nor its folder"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(p)));
        TRY(paths_cat(pr, pos, (Str){real, strlen(real)}, S("/"), name, &resolved));
    }
    if (mounts_in_folder(resolved, paths_root_of(&h->paths, pr))) return OK;
    for (size_t k = 0; k < h->paths.mounts.len; k++) {
        const Mount *m = &at(h->paths.mounts, k);
        if (m->secrets || !mounts_in_folder(resolved, paths_run_dir_of(&h->paths, pr, m))) continue; // skip: not this mount's
        if (written && m->access == MOUNT_READ_ONLY) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} would write {s}, in the read-only mount `{s}`"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(p), MESSAGE_FROM_HOST(m->name)));
        return OK;
    }
    return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s}: its local path {s} is under neither the root nor a mount"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(p)));
}

// A copy's operands, `argv` from `first`: two at least, each remote one to
// an allowed host, each local one under the mounts, the last written.
static STATUS_MUST_USE Status host_unconfined_operands(Host *h, Run *r, Pos pos, Str prog, Str cwd, SpawnStrs argv, size_t first) {
    if (argv.len < first + 2) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes a source and a destination after its options"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog)));
    for (size_t i = first; i < argv.len; i++) {
        Str s = at(argv, i).s;
        Str host = S("");
        if (str_starts_with(s, S("-"))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes its options before its operands, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(s)));
        if (host_unconfined_remote(s, &host)) {
            if (str_starts_with(str_sub(s, host.len, s.len), S("::"))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes no rsync daemon's module (`host::module`), not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(prog), MESSAGE_FROM_SCRIPT(s)));
            if (!host_unconfined_host(host)) return host_unconfined_no_host(r, pos, prog, host);
        } else {
            TRY(host_unconfined_local(h, r, pos, prog, cwd, s, i + 1 == argv.len));
        }
    }
    return OK;
}

// `ssh <host> [command...]`: the host first, no option.
static STATUS_MUST_USE Status host_unconfined_ssh(Run *r, Pos pos, SpawnStrs argv) {
    if (argv.len < 2) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ssh names its host"));
    Str first = at(argv, 1).s;
    if (str_starts_with(first, S("-"))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ssh takes no option, not {s}: an option could name a program, a config or a hop"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(first)));
    if (!host_unconfined_host(first)) return host_unconfined_no_host(r, pos, S("ssh"), first);
    return OK;
}

// `scp [-q -r -p] operands...`.
static STATUS_MUST_USE Status host_unconfined_scp(Host *h, Run *r, Pos pos, Str cwd, SpawnStrs argv) {
    HostConfineArgs flags = {HOST_UNCONFINED_SCP_FLAGS, sizeof HOST_UNCONFINED_SCP_FLAGS / sizeof *HOST_UNCONFINED_SCP_FLAGS};
    size_t i = 1;
    while (i < argv.len && str_starts_with(at(argv, i).s, S("-"))) {
        if (!host_unconfined_named(flags, at(argv, i).s)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` scp takes -q, -r and -p, not {s}: another option could name a program, a config or a hop"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(at(argv, i).s)));
        i++;
    }
    return host_unconfined_operands(h, r, pos, S("scp"), cwd, argv, i);
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

// `rsync [options] operands...`: HOST_UNCONFINED_RSYNC_*'s options, and
// `-e ssh` or `--rsh=ssh` alone of a shell.
static STATUS_MUST_USE Status host_unconfined_rsync(Host *h, Run *r, Pos pos, Str cwd, SpawnStrs argv) {
    HostConfineArgs longs = {HOST_UNCONFINED_RSYNC_LONG, sizeof HOST_UNCONFINED_RSYNC_LONG / sizeof *HOST_UNCONFINED_RSYNC_LONG};
    size_t i = 1;
    while (i < argv.len && str_starts_with(at(argv, i).s, S("-"))) {
        Str o = at(argv, i).s;
        if (str_eq(o, S("-e")) || str_eq(o, S("--rsh"))) {
            if (i + 1 >= argv.len || !str_eq(at(argv, i + 1).s, S("ssh"))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` rsync takes `-e ssh` alone of a remote shell: any other is a program it would start"));
            i += 2;
            continue;
        }
        if (str_starts_with(o, S("--rsh=")) || str_starts_with(o, S("-e"))) {
            if (!str_eq(o, S("--rsh=ssh")) && !str_eq(o, S("-essh"))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` rsync takes `-e ssh` alone of a remote shell: any other is a program it would start"));
            i++;
            continue;
        }
        bool short_ok = !str_starts_with(o, S("--")) && host_unconfined_rsync_letters(str_sub(o, 1, o.len));
        if (!short_ok && !host_unconfined_named(longs, o)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` rsync does not take {s}: its options are HOST_UNCONFINED_RSYNC_LETTERS' and HOST_UNCONFINED_RSYNC_LONG's (c/host/host_unconfined.c)"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(o)));
        i++;
    }
    for (size_t k = i; k < argv.len; k++) {
        if (str_starts_with(at(argv, k).s, S("rsync://"))) return host_unconfined_no_host(r, pos, S("rsync"), at(argv, k).s);
    }
    return host_unconfined_operands(h, r, pos, S("rsync"), cwd, argv, i);
}

// `git ... push ...`: no `-c` but `color.ui=never` (the git prelude's), no
// `--receive-pack` or `--exec`.
static STATUS_MUST_USE Status host_unconfined_git(Run *r, Pos pos, SpawnStrs argv, Str why) {
    bool push_word = false;
    for (size_t i = 1; i < argv.len; i++) {
        Str w = at(argv, i).s;
        push_word = push_word || str_eq(w, S("push"));
        if (str_starts_with(w, S("--receive-pack")) || str_starts_with(w, S("--exec"))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push takes no `--receive-pack` or `--exec`: each names a program it would start outside the confinement"));
        if (str_eq(w, S("-c")) && (i + 1 >= argv.len || !str_eq(at(argv, i + 1).s, S("color.ui=never")))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push takes no `-c` but `color.ui=never`: a setting could name a program git starts outside the confinement"));
    }
    if (!push_word) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ({s}) is for a `git push`, or `ssh`, `scp` or `rsync` to the hosts of HOST_UNCONFINED_HOSTS; every other program runs confined to the mounts"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(why)));
    return OK;
}

// `unconfined = "tag: why"`, held to its shapes, into `*out`: the argv to
// run, git's with HOST_UNCONFINED_GIT after `git`.
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
static STATUS_MUST_USE Status host_run_unconfined(Host *h, Run *r, Pos pos, SpawnStrs argv, const HostEnvOpt *env, Str why, Str cwd, SpawnStrs *out) {
    Str reached = S("");
    bool found = host_resolved_name(h, r->ctx.values, at(argv, 0), cwd, &reached);
    Str base = host_base_name(at(argv, 0).s);
    bool named = found && str_eq(base, reached);
    bool git = named && str_eq(base, S("git"));
    bool ssh_family = named && (str_eq(base, S("ssh")) || str_eq(base, S("scp")) || str_eq(base, S("rsync")));
    if (!git && !ssh_family) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ({s}) is for a `git push`, or `ssh`, `scp` or `rsync` to the hosts of HOST_UNCONFINED_HOSTS; every other program runs confined to the mounts"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(why)));
    for (size_t j = 0; j < env->names.len; j++) {
        Str k = at(env->names, j);
        if (!(git && str_eq(k, S("GIT_TERMINAL_PROMPT")))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes no `env` (git: but GIT_TERMINAL_PROMPT), not {s}: a variable could name a program it starts"), MESSAGE_ARGS(MESSAGE_FROM_HOST(base), MESSAGE_FROM_SCRIPT(k)));
    }
    TRY(host_confine_secrets_apart(h, r, pos, S("run")));
    *out = argv;
    if (str_eq(base, S("ssh"))) return host_unconfined_ssh(r, pos, argv);
    if (str_eq(base, S("scp"))) return host_unconfined_scp(h, r, pos, cwd, argv);
    if (str_eq(base, S("rsync"))) return host_unconfined_rsync(h, r, pos, cwd, argv);
    TRY(host_unconfined_git(r, pos, argv, why));
    HostConfineArgs fixed = {HOST_UNCONFINED_GIT, sizeof HOST_UNCONFINED_GIT / sizeof *HOST_UNCONFINED_GIT};
    size_t n = argv.len + fixed.len;
    OsStr *all = ARENA_ARRAY(r->ctx.values, OsStr, n);
    if (all == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(ArgvRoom, OsStr);
    ArgvRoom room = {all, n};
    at(room, 0) = at(argv, 0);
    for (size_t i = 0; i < fixed.len; i++) at(room, 1 + i) = os_str_own((Str){at(fixed, i), strlen(at(fixed, i))});
    for (size_t i = 1; i < argv.len; i++) at(room, fixed.len + i) = at(argv, i);
    out->ptr = all;
    out->len = n;
    return OK;
}
