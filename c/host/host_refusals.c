// host_refusals.c: the programs `run` and `job_start` refuse: a shell, a
// text tool or a wrapper without a `want`, `sed` and `grep` even with one,
// and a program by a path git does not track (`host_refuse`).
//
// Include host.c first.

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
    Str bare = base;
    while (bare.len > 0 && str_byte(bare, bare.len - 1) >= '0' && str_byte(bare, bare.len - 1) <= '9') bare = str_sub(bare, 0, bare.len - 1);
    HostRefusedGroups never = {HOST_NEVER, sizeof HOST_NEVER / sizeof *HOST_NEVER};
    for (size_t g = 0; g < never.len; g++) {
        const HostRefusedGroup *grp = &at(never, g);
        if (host_group_has(grp, base) || host_group_has(grp, bare)) {
            return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`, even with a want: {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(base), MESSAGE_FROM_HOST(((Str){grp->why, strlen(grp->why)}))));
        }
    }
    if (has_want) return OK;
    HostRefusedGroups refused = {HOST_REFUSED, sizeof HOST_REFUSED / sizeof *HOST_REFUSED};
    for (size_t g = 0; g < refused.len; g++) {
        const HostRefusedGroup *grp = &at(refused, g);
        if (host_group_has(grp, base) || host_group_has(grp, bare)) {
            return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`: {s}; {{want = \"tag: why\"}} to run it anyway"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(base), MESSAGE_FROM_HOST(((Str){grp->why, strlen(grp->why)}))));
        }
    }
    if (str_find_byte(argv0, '/') == NULL || host_under_tools_mount(h, argv0)) return OK;
    bool known = false;
    TRY(host_tracked(h, r, pos, who, argv0_os, cwd, &known));
    if (known) return OK;
    return host_fail(r, pos, CLASS_REFUSED, S("`{s}` of `{s}`: a program by path that git does not track (a script just written?); {{want = \"tag: why\"}} to run it anyway"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_SCRIPT(argv0)));
}
