// host_refusals.c: the programs `run` and `job_start` refuse: a shell, a
// text tool or a wrapper without a `want`, `sed` and `grep` even with one,
// and a program by a path git does not track (`host_refuse`).
//
// Include host.c and host_confine.c first.

typedef struct {
    const char *names; // cstr: the names, each ended by a space
    const char *why; // cstr: a literal
} HostRefusedGroup;

SLICE_TYPE(HostRefusedGroups, const HostRefusedGroup);

// Programs `run` refuses without a `want`.
static const HostRefusedGroup HOST_REFUSED[] = {
    {"bash sh dash zsh busybox python python3 perl node rbash ash ksh mksh csh tcsh fish yash nodejs tclsh wish expect ruby irb php lua luajit pypy pypy3 deno bun guile Rscript ", "a shell or an interpreter: Shale is the script"},
    {"vim vi ex view ed gdb ", "an editor or a debugger, which runs any command it is given: `edit(p, old, new)` changes a file"},
    {"head tail awk gawk mawk nawk cut sort uniq wc cat tr tac nl rev fold paste ", "a text tool: Shale's builtins and run's own filter and keep do this"},
    {"env xargs timeout nohup setsid stdbuf time flock script chroot unshare nsenter setpriv prlimit taskset chrt ionice watch strace ltrace sudo su runuser systemd-run numactl tmux screen sg dbus-run-session run-parts start-stop-daemon capsh doas pkexec fakeroot faketime unbuffer parallel firejail ", "a wrapper that runs another program: run that program, with run's own options"},
    {"nice ", "a wrapper that runs another program: run that program, with run's own `nice = N` (0 to 19)"},
};

// Programs `run` refuses even with a `want`.
static const HostRefusedGroup HOST_NEVER[] = {
    {"sed gsed ", "`edit(p, old, new)` changes a file and stops the run when `old` isn't there; `re_replace(s, pattern, new)` then `replace(p, s)` for a pattern; `lines(read(p))`, or `shale cookbook:look.shale at=f:N-M`, for a file's lines"},
    {"grep egrep fgrep zgrep zegrep zfgrep rgrep xzgrep xzegrep xzfgrep bzgrep lzgrep rg ", "`grep_tree(place, needle, {regex, nocase, ext, like, context})` searches a tree; `grep(p, lit)`, `re_lines(read(p), pattern)` or `grep_all(p, lit)` one file; `report_run(argv, {grep = {...}})` or `run`'s `re` a program's output"},
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

// The last part of the file `argv0` names once every link is followed, found
// as `execve` finds it (spawn_candidates: its path, from `cwd` when
// relative, else along the inherited PATH), with `host_base_name`'s suffix
// dropped, into `*out`; false when no candidate is there to run (the spawn
// says so) or the arena refuses.
// DRAGON: a refused program by another name. The guards read argv[0]'s
// name, so `rbash` (a link to bash on Debian), or a link a script made with
// `ln -s` to a shell, would pass them by a name the lists don't hold.
// REMEDY: the name is read again from the file it reaches, and a refused
// program is refused by either name. test_run_guards'
// a_refused_program_by_another_name_is_refused_by_the_one_it_reaches plants
// a link.
static bool host_resolved_name(Host *h, Arena *a, OsStr argv0_os, Str cwd, Str *out) {
    SpawnCStrs paths;
    if (!spawn_candidates(a, argv0_os, h->env, &paths)) return false;
    char *real = ARENA_ARRAY(a, char, PATH_MAX);
    if (real == NULL) return false;
    for (size_t i = 0; i < paths.len; i++) {
        const char *c = at(paths, i); // cstr: spawn_candidates' path
        Str cand = {c, strlen(c)};
        Buf full = buf_make(a);
        if (str_byte(cand, 0) != '/' && (buf_str(&full, cwd) != OK || buf_lit(&full, "/") != OK)) return false;
        if (buf_str(&full, cand) != OK) return false;
        const char *f = arena_cstr(a, os_str_own(buf_view(&full)));
        if (f == NULL) return false;
        if (access(f, X_OK) != 0 || realpath(f, real) == NULL) continue; // skip: not there to run, as execve passes it
        *out = host_base_name((Str){real, strlen(real)});
        return true;
    }
    return false;
}

// The group of `groups` holding `name`, or its name without trailing digits
// (`python3` as `python`), or NULL.
static const HostRefusedGroup *host_group_of(HostRefusedGroups groups, Str name) {
    Str bare = name;
    while (bare.len > 0 && str_byte(bare, bare.len - 1) >= '0' && str_byte(bare, bare.len - 1) <= '9') bare = str_sub(bare, 0, bare.len - 1);
    for (size_t g = 0; g < groups.len; g++) {
        const HostRefusedGroup *grp = &at(groups, g);
        if (host_group_has(grp, name) || host_group_has(grp, bare)) return grp;
    }
    return NULL;
}

// Refuses, without a want, an `env` name that changes which program runs or
// what it loads before its own code: PATH, LD_*, BASH_ENV and ENV.
// DRAGON: a refused program by its environment. The guards read argv[0],
// so `env = {PATH = "d"}` with `d/git` a link to a shell ran the shell as
// `git`, and LD_PRELOAD ran any library's code in any allowed program.
// REMEDY: those names need a want, as the programs they would reach do.
// test_run_guards' an_env_that_picks_the_program_needs_a_want plants each.
static STATUS_MUST_USE Status host_refuse_env(Run *r, Pos pos, Str who, PathsStrs names) {
    for (size_t i = 0; i < names.len; i++) {
        Str k = at(names, i);
        if (str_eq(k, S("PATH")) || str_eq(k, S("BASH_ENV")) || str_eq(k, S("ENV")) || str_starts_with(k, S("LD_"))) {
            return host_fail(r, pos, CLASS_REFUSED, S("`{s}`'s `env` {s} changes which program runs, or what it loads before its own code; {{want = \"tag: why\"}} to set it anyway"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(k)));
        }
    }
    return OK;
}

// The `env` names gh reads a token or its configuration from, which a
// script's `env` never sets, want or not: GH_CONFIG_DIR is the host's (an
// empty folder of the run's own, host_confine_sets), and a token goes in by
// `secrets` alone.
static const char *const HOST_GH_ENV[] = {"GH_CONFIG_DIR", "GH_TOKEN", "GITHUB_TOKEN", "GH_ENTERPRISE_TOKEN", "GITHUB_ENTERPRISE_TOKEN"};

// Refuses an `env` that sets one of HOST_GH_ENV.
// DRAGON: the secrets rule. A token in `env` is the script's own string,
// printed and logged as any other (the record shows `env`), and a
// GH_CONFIG_DIR of the script's could point gh at a copy of the home's
// configuration, token and all, inside a mount.
// REMEDY: those names are refused whatever the want; gh's token comes from
// `secrets = {GH_TOKEN = "keys:github-token"}`, scrubbed, and its
// configuration folder is the host's. test_run_guards'
// gh_reads_no_configuration_and_never_says_its_token plants it.
static STATUS_MUST_USE Status host_refuse_gh_env(Run *r, Pos pos, Str who, PathsStrs names) {
    HostConfineNames gh = {HOST_GH_ENV, sizeof HOST_GH_ENV / sizeof *HOST_GH_ENV};
    for (size_t i = 0; i < names.len; i++) {
        Str k = at(names, i);
        for (size_t j = 0; j < gh.len; j++) {
            if (!str_eq(k, (Str){at(gh, j), strlen(at(gh, j))})) continue; // skip: another name
            if (j == 0) return host_fail(r, pos, CLASS_REFUSED, S("`{s}`'s `env` GH_CONFIG_DIR is the host's, even with a want: gh reads its configuration from an empty folder of the run's own, and its token from `secrets = {{GH_TOKEN = \"keys:github-token\"}}`"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who)));
            return host_fail(r, pos, CLASS_REFUSED, S("`{s}`'s `env` {s} would put a token in the script's hands, even with a want: `secrets = {{{s} = \"keys:github-token\"}}` hands it to gh, scrubbed"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(k), MESSAGE_FROM_SCRIPT(k)));
        }
    }
    return OK;
}

// The inherited variables no program Shale starts gets, and no script's
// `env` sets, want or not: each names a socket to a service outside the
// confinement (the session's D-Bus and the user's runtime folder, where
// systemd's user manager listens; ssh's agent; Claude Code's own messaging,
// with its token), and Landlock's file rules do not hold a socket a program
// connects to. A name ending in `_` is a prefix. An unconfined ssh, scp,
// rsync or git push gets the agent back (host_unconfined_env), as the user's
// own ssh would have it.
static const char *const HOST_ENV_DROPPED[] = {"DBUS_SESSION_BUS_ADDRESS", "XDG_RUNTIME_DIR", "SSH_AUTH_SOCK", "CLAUDE_CODE_MESSAGING_"};

// Whether the variable `name` is one of HOST_ENV_DROPPED.
static bool host_env_dropped(Str name) {
    HostConfineNames dropped = {HOST_ENV_DROPPED, sizeof HOST_ENV_DROPPED / sizeof *HOST_ENV_DROPPED};
    for (size_t i = 0; i < dropped.len; i++) {
        Str d = {at(dropped, i), strlen(at(dropped, i))};
        bool prefix = str_byte(d, d.len - 1) == '_';
        if (prefix ? str_starts_with(name, d) : str_eq(name, d)) return true;
    }
    return false;
}

SLICE_TYPE(HostEnvNames, Str);

// The environment a program inherits, `K=V` each, from `from` (`environ`'s
// shape, NULL-ended), in its order, into `*out`: without `ZIG_PROGRESS`, or
// a `zig build` that `run` runs would write its progress to the pipe of the
// build that ran `shale`, and without HOST_ENV_DROPPED's names, each into
// `*gone` (room for every entry, its `len` set), so the caller can take them
// out of its own environment too. The inherited `SSH_AUTH_SOCK=...` goes to
// `*agent` (empty for none), for an unconfined program alone. False when
// the arena refuses.
static bool host_env_inherit(Arena *a, char **from, SpawnStrs *out, Str *agent, HostEnvNames *gone) {
    size_t n = 0;
    for (char **p = from; *p != NULL; p++) n++; // cstr: the environment's, NULL-ended
    OsStr *env = ARENA_ARRAY(a, OsStr, n);
    Str *names = ARENA_ARRAY(a, Str, n);
    if ((env == NULL || names == NULL) && n > 0) return false;
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {env, n};
    HostEnvNames gone_room = {names, n};
    size_t k = 0;
    size_t g = 0;
    *agent = S("");
    for (char **p = from; *p != NULL; p++) {
        Str e = {*p, strlen(*p)}; // cstr: the environment's, NULL-ended
        if (str_starts_with(e, S("ZIG_PROGRESS="))) continue;
        const char *eq = str_find_byte(e, '=');
        Str name = eq == NULL ? e : str_sub(e, 0, (size_t)(eq - e.ptr));
        if (host_env_dropped(name)) {
            if (str_eq(name, S("SSH_AUTH_SOCK")) && !arena_dup(a, e, agent)) return false;
            if (!arena_dup(a, name, &at(gone_room, g))) return false;
            g++;
            continue;
        }
        at(room, k++) = os_str_own(e);
    }
    out->ptr = env;
    out->len = k;
    gone->ptr = names;
    gone->len = g;
    return true;
}

// Refuses an `env` that sets one of HOST_ENV_DROPPED, want or not.
// DRAGON: the file system's boundaries. Landlock holds a program to the
// mounts' files, and a socket it connects to is no file to it: with the
// session's D-Bus a confined program could ask systemd's user manager to
// start any program, unconfined, and with Claude Code's messaging socket
// and token it could speak as the session.
// REMEDY: those variables never reach a program (host_env_inherit drops
// them, and this refuses a script's own); test_run_guards'
// a_program_gets_no_session_socket_and_no_env_sets_one plants each.
static STATUS_MUST_USE Status host_refuse_session_env(Run *r, Pos pos, Str who, PathsStrs names) {
    for (size_t i = 0; i < names.len; i++) {
        Str k = at(names, i);
        if (host_env_dropped(k)) return host_fail(r, pos, CLASS_REFUSED, S("`{s}`'s `env` {s} names a socket outside the confinement, even with a want: no program Shale starts gets the session's D-Bus, its runtime folder, ssh's agent or Claude Code's messaging"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(k)));
    }
    return OK;
}

// Refuses `gh auth ...` (gh's first word that is no option `auth`) and `gh
// alias ...`, want or not, when the program is gh by its name or the file it
// reaches.
// DRAGON: the secrets rule. `gh auth status` prints the token's first
// characters, `gh auth token` all of it and `gh auth git-credential` it as a
// password; the scrubber knows a token by its value, and a part of it only
// from 12 bytes on (host_scrub_scan). An alias (`gh alias set t "auth
// token"`, kept in the run's GH_CONFIG_DIR) runs one by another name.
// REMEDY: both are refused before the program starts; gh's token reaches gh
// alone, by `secrets`. test_run_guards'
// gh_reads_no_configuration_and_never_says_its_token plants it.
static STATUS_MUST_USE Status host_refuse_gh(Host *h, Run *r, Pos pos, SpawnStrs argv, Str cwd) {
    Str base = host_base_name(at(argv, 0).s);
    Str reached = base;
    if (!host_resolved_name(h, r->ctx.values, at(argv, 0), cwd, &reached)) reached = base;
    if (!str_eq(base, S("gh")) && !str_eq(reached, S("gh"))) return OK;
    for (size_t i = 1; i < argv.len; i++) {
        Str w = at(argv, i).s;
        if (str_starts_with(w, S("-"))) continue; // skip: an option before gh's command
        if (str_eq(w, S("auth"))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`gh auth` is refused, even with a want: `gh auth status` prints the token's first characters, `gh auth token` and `gh auth git-credential` all of it, and gh needs no login here: its token comes from `secrets = {{GH_TOKEN = \"keys:github-token\"}}`"));
        if (str_eq(w, S("alias"))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`gh alias` is refused, even with a want: an alias could run `gh auth token` by another name"));
        return OK;
    }
    return OK;
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
    TRY(host_confine_open(h, r, pos, who, &spec.confine_fd));
    SpawnSink sink = {NULL, host_discard};
    SpawnOutcome o;
    SpawnResult got = spawn_run(r->ctx.values, &spec, sink, &o);
    host_confine_close(&spec.confine_fd);
    if (got == SPAWN_OUT_OF_MEMORY) return run_values_oom(r, pos);
    if (got != SPAWN_OK) {
        Str why = got == SPAWN_NOT_FOUND ? S("there is no git") : got == SPAWN_NOT_RUNNABLE ? S("git is not runnable") : got == SPAWN_NO_CWD ? S("its folder cannot be entered") : got == SPAWN_NOT_CONFINED ? S("the kernel refused its Landlock confinement") : S("the kernel refused a pipe or a fork");
        return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`: a program by path is run only when git tracks it, and git could not start to say: {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(path.s), MESSAGE_FROM_HOST(why)));
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
    // The name of the file it reaches, every link followed; its own when
    // none is found.
    Str reached = base;
    if (!host_resolved_name(h, r->ctx.values, argv0_os, cwd, &reached)) reached = base;
    HostRefusedGroups never = {HOST_NEVER, sizeof HOST_NEVER / sizeof *HOST_NEVER};
    HostRefusedGroups refused = {HOST_REFUSED, sizeof HOST_REFUSED / sizeof *HOST_REFUSED};
    for (int pass = 0; pass < 2; pass++) {
        Str name = pass == 0 ? base : reached;
        if (pass == 1 && str_eq(reached, base)) break;
        // A program by another name is said as the one it reaches.
        Str shown = base;
        if (pass == 1) TRY(paths_cat(&builtin_files_of(r)->path, pos, base, S("`, which is `"), reached, &shown));
        const HostRefusedGroup *grp = host_group_of(never, name);
        if (grp != NULL) {
            return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`, even with a want: {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(shown), MESSAGE_FROM_HOST(((Str){grp->why, strlen(grp->why)}))));
        }
        if (has_want) continue;
        grp = host_group_of(refused, name);
        if (grp != NULL) {
            return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`: {s}; {{want = \"tag: why\"}} to run it anyway"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(shown), MESSAGE_FROM_HOST(((Str){grp->why, strlen(grp->why)}))));
        }
    }
    if (has_want) return OK;
    if (str_find_byte(argv0, '/') == NULL || host_under_tools_mount(h, argv0)) return OK;
    bool known = false;
    TRY(host_tracked(h, r, pos, who, argv0_os, cwd, &known));
    if (known) return OK;
    return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`: a program by path that git does not track (a script just written?); {{want = \"tag: why\"}} to run it anyway"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(argv0)));
}
