// host_confine.c: the Landlock ruleset every program the host starts runs
// under (`run`'s, `job_start`'s reaper and job, and the git that says whether
// a program is tracked), built from the run's root and mounts before each
// fork (landlock.c's calls; spawn_child.c restricts the child to it).
//
// It is an allow-list, of what the box's programs are known to need:
// - **written**: the run's root and every mount without ` ro`, the files
//   and folders under them made, changed, moved and removed;
// - **read and run**: the read-only mounts (a ` tools` mount among them),
//   the system's own folders (HOST_CONFINE_SYSTEM) and the few files a
//   program of ours reads from the user's home (HOST_CONFINE_HOME_FILES),
//   the mounts file and the user prelude a child `shale` reads, and the
//   reaper, this program;
// - **devices**: /dev/null, /dev/zero, /dev/full and the pseudo-terminals
//   (/dev/ptmx, /dev/pts) read and written,
//   /dev/random and /dev/urandom read.
// - **the run's own temporary folder**, made below TMPDIR (else /tmp) at
//   its first program and handed to every program as TMPDIR, written; it
//   is removed when the run ends, unless the run started a job, which may
//   still use it. The caches go to the `build` mount's cache folder
//   (host_confine_sets).
// Nothing else: a secrets mount's folder is in no rule, `/tmp` itself is
// in none, and a `want` widens none of it. A program refused by it gets EACCES
// ("Permission denied") from the call it made, which it says in its own
// output; such a refusal is a gap to add here on purpose, never a reason to
// widen a rule.
//
// Include host.c and landlock.c first.

// The system's folders a program reads and runs from: its programs and
// libraries, its configuration (passwd, the resolver, certificates,
// /etc/gitconfig), the process and system tables a runtime asks (/proc, the
// CPUs in /sys) and the resolver's own file where /etc/resolv.conf links. A
// folder not there (a merged /usr has no /lib32) is no rule.
static const char *const HOST_CONFINE_SYSTEM[] = {
    "/usr", "/bin", "/sbin", "/lib", "/lib32", "/lib64", "/libx32", "/etc", "/proc", "/sys", "/run/systemd/resolve",
};

// The devices a program reads and writes, and those it only reads. A
// pseudo-terminal's (/dev/ptmx, /dev/pts) are among the first: a test that
// opens one (test_secrets' openpty) or a program that runs another on one.
// The cost: a program may write to any terminal of the user's, which shows
// text there; it can type into none, as the kernel refuses TIOCSTI since
// 6.2.
static const char *const HOST_CONFINE_DEVICES[] = {"/dev/null", "/dev/zero", "/dev/full", "/dev/ptmx", "/dev/pts"};
static const char *const HOST_CONFINE_RANDOM[] = {"/dev/random", "/dev/urandom"};

// The files below the user's home a program of ours reads: git's own
// configuration, its default excludes and attributes, which git refuses to
// run without reading when they are there (a file it cannot read is a fatal
// error to it, not a missing one: `git add` stopped on
// ~/.config/git/ignore in the box's census); and ssh's known hosts and its
// configuration, so a push over ssh can check the host and authenticate
// through the agent (SSH_AUTH_SOCK, a socket, which Landlock's file rules
// do not reach). No key file is in it: a key reaches ssh only through the
// agent, or through an unconfined push (`run`'s `unconfined`, host_run.c).
static const char *const HOST_CONFINE_HOME_FILES[] = {".gitconfig", ".config/git/config", ".config/git/ignore", ".config/git/attributes", ".ssh/known_hosts", ".ssh/config"};

// git's own files below XDG_CONFIG_HOME, where git reads them in place of
// ~/.config/git when it is set.
static const char *const HOST_CONFINE_XDG_GIT_FILES[] = {"git/config", "git/ignore", "git/attributes"};

SLICE_TYPE(HostConfineNames, const char *const);

// An environment variable's value in `env` (its last `K=` entry), or false.
static bool host_confine_env(SpawnStrs env, Str key, Str *out) {
    bool found = false;
    for (size_t i = 0; i < env.len; i++) {
        Str e = at(env, i).s;
        if (e.len > key.len && str_starts_with(e, key) && str_byte(e, key.len) == '=') {
            *out = str_sub(e, key.len + 1, e.len);
            found = true;
        }
    }
    return found;
}

// The stop for a rule the kernel refused at `path` (named only when
// `shown`, a path the host chose; a mount's folder is not named), `e` its
// errno.
static STATUS_MUST_USE Status host_confine_refused(Run *r, Pos pos, Str who, Str what, int e) {
    return host_fail(r, pos, CLASS_HOST, S("`{s}`: Landlock refused a rule for {s}: {s}; no program runs unconfined"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_HOST(what), MESSAGE_FROM_HOST(file_system_err_name(file_system_err_of(e)))));
}

// Allows `rights` beneath the OS path `path` in the ruleset `fd`; a path not
// there is no rule. Any other refusal stops the run, the path said as
// `what`.
static STATUS_MUST_USE Status host_confine_allow(Run *r, Pos pos, Str who, int fd, Str path, uint64_t rights, uint64_t handled, Str what) {
    char *c = arena_cstr(r->ctx.values, os_str_own(path));
    if (c == NULL) return run_values_oom(r, pos);
    int e = landlock_allow(fd, c, rights, handled);
    if (e == 0 || e == ENOENT || e == ENOTDIR) return OK;
    return host_confine_refused(r, pos, who, what, e);
}

// Allows `rights` beneath each of `names`, the host's own literals.
static STATUS_MUST_USE Status host_confine_allow_all(Run *r, Pos pos, Str who, int fd, HostConfineNames names, uint64_t rights, uint64_t handled) {
    for (size_t i = 0; i < names.len; i++) {
        Str p = {at(names, i), strlen(at(names, i))};
        TRY(host_confine_allow(r, pos, who, fd, p, rights, handled, p));
    }
    return OK;
}

// Whether a secrets mount's folder lies under the folder `dir`, which a rule
// allows.
static bool host_confine_holds_secrets(PathsSecretDirs secrets, Str dir) {
    for (size_t k = 0; k < secrets.len; k++) {
        if (mounts_in_folder(at(secrets, k).run_dir, dir)) return true;
    }
    return false;
}

// The rules for the run's root and mounts into the ruleset `fd`.
// DRAGON: the secrets rule. A Landlock rule allows a whole folder, and no
// rule below it can take a right back, so a secrets mount whose folder is
// inside the root or another mount would be readable to every program.
// REMEDY: such mounts run no program: the stop names neither folder.
// test_run_guards'
// a_program_by_a_secret_s_path_is_the_secrets_guard_s_before_git_is_asked
// plants it.
static STATUS_MUST_USE Status host_confine_mounts(Host *h, Run *r, Pos pos, Str who, int fd, uint64_t handled) {
    PathRun *pr = &builtin_files_of(r)->path;
    PathsSecretDirs secrets = paths_secret_dirs(&h->paths, pr);
    Str root = paths_root_of(&h->paths, pr);
    bool leaks = host_confine_holds_secrets(secrets, root);
    for (size_t k = 0; k < h->paths.mounts.len && !leaks; k++) {
        const Mount *m = &at(h->paths.mounts, k);
        if (!m->secrets) leaks = host_confine_holds_secrets(secrets, paths_run_dir_of(&h->paths, pr, m));
    }
    if (leaks) {
        return host_fail(r, pos, CLASS_REFUSED, S("`{s}`: a secrets mount's folder is inside the root or another mount, which a program may read, and Landlock cannot hide a folder below one it allows: move the secrets folder out, and then programs run"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who)));
    }
    TRY(host_confine_allow(r, pos, who, fd, root, handled, handled, S("the root")));
    for (size_t k = 0; k < h->paths.mounts.len; k++) {
        const Mount *m = &at(h->paths.mounts, k);
        if (m->secrets) continue; // skip: no program reads a secrets folder
        Str what = S("");
        TRY(paths_cat(pr, pos, S("the mount `"), m->name, S("`"), &what));
        uint64_t rights = m->access == MOUNT_READ_ONLY ? LANDLOCK_READ_RIGHTS : handled;
        TRY(host_confine_allow(r, pos, who, fd, paths_run_dir_of(&h->paths, pr, m), rights, handled, what));
    }
    return OK;
}

// Allows `rights` on each of `names` below the folder `dir`.
static STATUS_MUST_USE Status host_confine_allow_below(Run *r, Pos pos, Str who, int fd, Str dir, HostConfineNames names, uint64_t rights, uint64_t handled) {
    PathRun *pr = &builtin_files_of(r)->path;
    for (size_t i = 0; i < names.len; i++) {
        Str rel = {at(names, i), strlen(at(names, i))};
        Str p = S("");
        TRY(paths_cat(pr, pos, dir, S("/"), rel, &p));
        TRY(host_confine_allow(r, pos, who, fd, p, rights, handled, rel));
    }
    return OK;
}

// The files a child `shale` and git read from the user's home: the mounts
// file this run read, the user prelude (SHALE_PRELUDE's, else the home's),
// git's global configuration where GIT_CONFIG_GLOBAL or XDG_CONFIG_HOME
// moves it, and HOST_CONFINE_HOME_FILES. None is a folder, so none allows
// a listing.
static STATUS_MUST_USE Status host_confine_home(Host *h, Run *r, Pos pos, Str who, int fd, uint64_t handled) {
    PathRun *pr = &builtin_files_of(r)->path;
    uint64_t file_read = LANDLOCK_READ_FILE;
    if (h->paths.mounts_from.len > 0) TRY(host_confine_allow(r, pos, who, fd, h->paths.mounts_from, file_read, handled, S("the mounts file")));
    Str home = S("");
    bool has_home = host_confine_env(h->env, S("HOME"), &home) && home.len > 0 && str_byte(home, 0) == '/';
    Str prelude = S("");
    if (host_confine_env(h->env, S("SHALE_PRELUDE"), &prelude)) {
        if (prelude.len > 0) TRY(host_confine_allow(r, pos, who, fd, prelude, file_read, handled, S("the user prelude")));
    } else if (has_home) {
        TRY(paths_cat(pr, pos, home, S("/.config/shale/prelude.shale"), S(""), &prelude));
        TRY(host_confine_allow(r, pos, who, fd, prelude, file_read, handled, S("the user prelude")));
    }
    // git's global configuration where GIT_CONFIG_GLOBAL moves it (the
    // golden harness's pinned identity does), as ~/.gitconfig is read.
    Str global = S("");
    if (host_confine_env(h->env, S("GIT_CONFIG_GLOBAL"), &global) && global.len > 0 && str_byte(global, 0) == '/') TRY(host_confine_allow(r, pos, who, fd, global, file_read, handled, S("GIT_CONFIG_GLOBAL's file")));
    Str xdg = S("");
    if (host_confine_env(h->env, S("XDG_CONFIG_HOME"), &xdg) && xdg.len > 0 && str_byte(xdg, 0) == '/') {
        HostConfineNames git = {HOST_CONFINE_XDG_GIT_FILES, sizeof HOST_CONFINE_XDG_GIT_FILES / sizeof *HOST_CONFINE_XDG_GIT_FILES};
        TRY(host_confine_allow_below(r, pos, who, fd, xdg, git, file_read, handled));
    }
    if (!has_home) return OK;
    HostConfineNames files = {HOST_CONFINE_HOME_FILES, sizeof HOST_CONFINE_HOME_FILES / sizeof *HOST_CONFINE_HOME_FILES};
    return host_confine_allow_below(r, pos, who, fd, home, files, file_read, handled);
}

// The run's own temporary folder, made once, at the first call: below this
// process's TMPDIR when it is an absolute path, else below /tmp. A `shale`
// run by a confined program has its parent's temporary folder as TMPDIR, the
// one place below /tmp its confinement lets it write.
static STATUS_MUST_USE Status host_confine_tmp(Host *h, Run *r, Pos pos, Str who) {
    if (h->tmp.len > 0) return OK;
    Str base = S("/tmp");
    const char *given = getenv("TMPDIR"); // cstr: the environment's
    Str given_s = given != NULL ? (Str){given, strlen(given)} : S("");
    if (given_s.len > 0 && str_byte(given_s, 0) == '/') base = given_s;
    Buf b = buf_make(r->ctx.values);
    if (buf_str(&b, base) != OK || buf_lit(&b, "/shale-run.XXXXXX") != OK || buf_byte(&b, 0) != OK) return run_values_oom(r, pos);
    char *made = (char *)buf_view(&b).ptr;
    if (mkdtemp(made) == NULL) return host_fail(r, pos, CLASS_HOST, S("`{s}`: the run's temporary folder could not be made below {s}: {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_HOST(base), MESSAGE_FROM_HOST(file_system_err_name(file_system_err_of(errno)))));
    if (!arena_dup(h->paths.arena, (Str){made, strlen(made)}, &h->tmp)) return run_values_oom(r, pos);
    return OK;
}

// Removes the run's temporary folder, if it made one and started no job,
// which may still be using it; the folder's path is forgotten either way.
static void host_confine_tmp_remove(Host *h, Arena *a) {
    if (h->tmp.len > 0 && h->started.len == 0) (void)file_system_delete_tree(a, os_str_own(h->tmp)); // ignored: a folder below the temporary root, cleaned by the system's own sweep if left
    h->tmp = S("");
}

// What the host sets in every program's environment, unless the script's
// `env` sets it: TMPDIR, the run's own temporary folder, and the caches a
// toolchain would put below the home, which no rule allows: XDG_CACHE_HOME
// (roc's, Go's, any XDG program's) and ZIG_GLOBAL_CACHE_DIR (Zig's), both
// `<the build mount>/cache`, or `<the run's temporary folder>/cache` when no
// writable mount is named `build`, a cache that goes with the run.
typedef struct {
    Str name;
    Str value;
} HostConfineSet;

enum { HOST_CONFINE_SETS = 3 };

SLICE_TYPE(HostConfineSets, HostConfineSet);

// The cache folder, made if it is not there, into `*out`.
static STATUS_MUST_USE Status host_confine_cache(Host *h, Run *r, Pos pos, Str who, Str *out) {
    PathRun *pr = &builtin_files_of(r)->path;
    Str base = h->tmp;
    for (size_t k = 0; k < h->paths.mounts.len; k++) {
        const Mount *m = &at(h->paths.mounts, k);
        if (str_eq(m->name, S("build")) && m->access == MOUNT_READ_WRITE) base = paths_run_dir_of(&h->paths, pr, m);
    }
    TRY(paths_cat(pr, pos, base, S("/cache"), S(""), out));
    char *c = arena_cstr(r->ctx.values, os_str_own(*out));
    if (c == NULL) return run_values_oom(r, pos);
    if (mkdir(c, 0777) != 0 && errno != EEXIST) return host_fail(r, pos, CLASS_HOST, S("`{s}`: the programs' cache folder (the `build` mount's cache, or the run's temporary folder's) could not be made: {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_HOST(file_system_err_name(file_system_err_of(errno)))));
    return OK;
}

// The names and values the host sets, into `sets` (HOST_CONFINE_SETS of
// them), the run's temporary folder made first.
static STATUS_MUST_USE Status host_confine_sets(Host *h, Run *r, Pos pos, Str who, HostConfineSets sets) {
    TRY(host_confine_tmp(h, r, pos, who));
    Str cache = S("");
    TRY(host_confine_cache(h, r, pos, who, &cache));
    at(sets, 0) = (HostConfineSet){S("TMPDIR"), h->tmp};
    at(sets, 1) = (HostConfineSet){S("XDG_CACHE_HOME"), cache};
    at(sets, 2) = (HostConfineSet){S("ZIG_GLOBAL_CACHE_DIR"), cache};
    return OK;
}

// The rules into the ruleset `fd`, made for ABI `abi`.
static STATUS_MUST_USE Status host_confine_rules(Host *h, Run *r, Pos pos, Str who, int fd, int abi) {
    uint64_t handled = landlock_handled(abi);
    TRY(host_confine_mounts(h, r, pos, who, fd, handled));
    TRY(host_confine_tmp(h, r, pos, who));
    TRY(host_confine_allow(r, pos, who, fd, h->tmp, handled, handled, S("the run's temporary folder")));
    HostConfineNames system = {HOST_CONFINE_SYSTEM, sizeof HOST_CONFINE_SYSTEM / sizeof *HOST_CONFINE_SYSTEM};
    TRY(host_confine_allow_all(r, pos, who, fd, system, LANDLOCK_READ_RIGHTS, handled));
    HostConfineNames devices = {HOST_CONFINE_DEVICES, sizeof HOST_CONFINE_DEVICES / sizeof *HOST_CONFINE_DEVICES};
    TRY(host_confine_allow_all(r, pos, who, fd, devices, LANDLOCK_DEVICE_RIGHTS, handled));
    HostConfineNames random = {HOST_CONFINE_RANDOM, sizeof HOST_CONFINE_RANDOM / sizeof *HOST_CONFINE_RANDOM};
    TRY(host_confine_allow_all(r, pos, who, fd, random, LANDLOCK_READ_FILE, handled));
    if (h->reaper.len > 0) TRY(host_confine_allow(r, pos, who, fd, h->reaper, LANDLOCK_EXECUTE | LANDLOCK_READ_FILE, handled, S("the reaper")));
    return host_confine_home(h, r, pos, who, fd, handled);
}

// A new Landlock ruleset for a program `who` starts, into `*out`, which the
// caller closes once the child is forked. A kernel without Landlock, or
// older than LANDLOCK_ABI_NEEDED, stops the run: no program runs
// unconfined.
static STATUS_MUST_USE Status host_confine_open(Host *h, Run *r, Pos pos, Str who, int *out) {
    *out = -1;
    int abi = landlock_abi();
    if (abi < 0) return host_fail(r, pos, CLASS_REFUSED, S("`{s}`: this kernel has no Landlock (built without it, or booted with it off), which confines every program Shale starts to the root and the mounts; no program runs unconfined"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who)));
    if (abi < LANDLOCK_ABI_NEEDED) return host_fail(r, pos, CLASS_REFUSED, S("`{s}`: this kernel's Landlock is ABI {d}, and a program is confined only from ABI 3 (Linux 6.2), which holds a rename and a truncate to the mounts too; no program runs unconfined"), MESSAGE_ARGS(MESSAGE_FROM_HOST(who), MESSAGE_FROM_HOST(run_num(r, abi))));
    int fd = -1;
    int e = landlock_ruleset(landlock_handled(abi), &fd);
    if (e != 0) return host_confine_refused(r, pos, who, S("a new ruleset"), e);
    Status s = host_confine_rules(h, r, pos, who, fd, abi);
    if (s != OK) {
        close(fd);
        return s;
    }
    *out = fd;
    return OK;
}

// Closes a ruleset host_confine_open made, if it made one.
static void host_confine_close(int *fd) {
    if (*fd >= 0) close(*fd);
    *fd = -1;
}
