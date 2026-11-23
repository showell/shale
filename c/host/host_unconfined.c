// host_unconfined.c: `run`'s `unconfined = "tag: why"`, the programs `run`
// starts outside the Landlock confinement (host_confine.c), because what
// they need no rule allows: ssh's key files. There are six, each held to a
// narrow shape: a `git push`, a `git fetch` and a `git ls-remote --symref
// <remote> HEAD` (which branch the remote's HEAD names), each with a remote
// of the remotes file, and `ssh`, `scp` and `rsync` to a host of the hosts file
// alone (a deploy's). Every other program, and every other shape of these
// (every other git command), is refused; a `want` is beside the point, and
// the run's record says `(unconfined: ...)`.
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
// argv: no hook, no fsmonitor, no credential helper, no submodule pushed and
// no signing (each a program git would start, outside the confinement), and
// then ssh itself as ssh, its HostName the remotes file's host
// (host_run_unconfined adds `-c core.sshCommand=/usr/bin/ssh -o
// HostName=<host>`).
// They come before every `-c` of the script's, which host_unconfined_git
// refuses, so none is undone; and the push runs with GIT_ALLOW_PROTOCOL=ssh,
// which no configuration overrides.
static const char *const HOST_UNCONFINED_GIT[] = {"-c", "core.hooksPath=/dev/null", "-c", "core.fsmonitor=false", "-c", "credential.helper=", "-c", "push.recurseSubmodules=no", "-c", "push.gpgSign=false"};

// The same for an unconfined fetch: no hook (a fetch's reference-transaction
// hook), no fsmonitor, no credential helper; no submodule fetched (whose URL
// is another repository's); no automatic gc or maintenance afterwards (each a
// git it starts, with hooks of its own); a command for an alternate's refs
// that is `true` itself, never one the repository's config names (git runs
// it to negotiate); no bundle a server offers; and no commit graph written.
// An unconfined `ls-remote` runs under them too. host_run_unconfined adds
// `--upload-pack=git-upload-pack` after `fetch` (or `ls-remote`), so
// the command asked for on the host is git's own (a `-c
// remote.<name>.uploadpack` would not do: git takes the first of the
// remote's `uploadpack`s, the repository's own), and the ssh as a push's.
static const char *const HOST_UNCONFINED_GIT_FETCH[] = {"-c", "core.hooksPath=/dev/null", "-c", "core.fsmonitor=false", "-c", "credential.helper=", "-c", "fetch.recurseSubmodules=false", "-c", "submodule.recurse=false", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.alternateRefsCommand=true", "-c", "transfer.bundleURI=false", "-c", "fetch.writeCommitGraph=false"};

// The inherited variables an unconfined push runs without: each could name
// the program git runs for ssh, or settings past HOST_UNCONFINED_GIT's.
static const char *const HOST_UNCONFINED_GIT_ENV_DROPPED[] = {"GIT_SSH", "GIT_SSH_COMMAND", "GIT_SSH_VARIANT", "GIT_ALLOW_PROTOCOL", "GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT"};

// The options an unconfined `scp` takes: quiet, times kept, the original
// protocol (`-O`, under which the host runs `scp -t <path>`, a command a
// forced command on the host can hold to its paths, where scp's default
// SFTP is a subsystem that reads and writes any file), and
// HOST_UNCONFINED_SSH_SETTINGS' `-o`s. Any other (`-F`, `-S`, `-J`, `-i`)
// could name a program, a config or a hop.
// DRAGON: the file system's boundaries. `scp -r` follows every symbolic
// link below the folder it copies, and host_unconfined_local resolves the
// operand alone, so a link in a deploy's folder to ~/.ssh's key sent the
// key to the host, read with the user's own powers.
// REMEDY: no `-r`: an unconfined scp copies files named one by one, each
// resolved; a folder goes by rsync, whose options here follow no link
// (HOST_UNCONFINED_RSYNC_LETTERS holds no `L` or `k`). test_run_guards'
// an_unconfined_run_is_a_git_push_or_a_deploy_to_the_prod_host_alone plants
// an `-r`.
static const char *const HOST_UNCONFINED_SCP_FLAGS[] = {"-q", "-p", "-O"};

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

// A list file of the unconfined shapes', `kind` (the hosts file, the
// remotes file): its text into `*out`, each line held to `line_ok` (`shape`
// says a line's in a stop), the file the one `pinned` names (an environment
// variable, a test's) or else `~/.config/shale/<kind>`; "" for none (no
// `pinned` and no such file). A `pinned` that names no file, a file that
// cannot be read and a line that is not `line_ok` stop the run, naming the
// file and the line.
static STATUS_MUST_USE Status host_unconfined_list(Run *r, Pos pos, const char *pinned_name, Str kind, bool (*line_ok)(Str), Str shape, Str *out) { // cstr: a literal
    Arena *a = r->ctx.values;
    PathRun *pr = &builtin_files_of(r)->path;
    *out = S("");
    const char *pinned = getenv(pinned_name); // cstr: the environment's
    const char *home = getenv("HOME"); // cstr: the environment's
    Str path = S("");
    if (pinned != NULL) {
        path = (Str){pinned, strlen(pinned)};
    } else if (home != NULL && str_starts_with((Str){home, strlen(home)}, S("/"))) {
        TRY(paths_cat(pr, pos, (Str){home, strlen(home)}, S("/.config/shale/"), kind, &path));
    } else {
        return OK;
    }
    Str text = S("");
    FileSystemErr e = file_system_read_file(a, os_str_own(path), HOST_UNCONFINED_HOSTS_MAX, &text);
    if (e == FILE_SYSTEM_OUT_OF_MEMORY) return run_values_oom(r, pos);
    if (e == FILE_SYSTEM_FILE_NOT_FOUND && pinned == NULL) return OK; // skip: no file is nothing allowed
    if (e != FILE_SYSTEM_OK) return host_fail(r, pos, CLASS_HOST, S("`run`'s `unconfined`: the {s} file {s} cannot be read: {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(kind), MESSAGE_FROM_HOST(path), MESSAGE_FROM_HOST(file_system_err_name(e))));
    size_t from = 0;
    size_t n = 0;
    while (from <= text.len) {
        size_t to = path_part_end_at(text, from, '\n');
        Str line = mounts_trim(str_sub(text, from, to), true);
        from = to + 1;
        n++;
        if (line.len == 0 || str_byte(line, 0) == '#') continue;
        if (!line_ok(line)) return host_fail(r, pos, CLASS_HOST, S("`run`'s `unconfined`: the {s} file {s}, line {s}, is not {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(kind), MESSAGE_FROM_HOST(path), MESSAGE_FROM_HOST(run_unum(r, n)), MESSAGE_FROM_HOST(shape)));
    }
    *out = text;
    return OK;
}

// The hosts file's text into `*out` (host_unconfined_list): SHALE_HOSTS's,
// else ~/.config/shale/hosts.
static STATUS_MUST_USE Status host_unconfined_hosts(Run *r, Pos pos, Str *out) {
    return host_unconfined_list(r, pos, "SHALE_HOSTS", S("hosts"), host_unconfined_host_line, S("a host's: one `user@host` a line"), out);
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
        if (!host_unconfined_named(flags, host_unconfined_word(a, i))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` scp takes -q, -p, -O and `-o`, not {s}: `-r` follows the links in a folder, and another option could name a program, a config or a hop"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(host_unconfined_shown(a, i))));
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

// The options an unconfined push takes, each whole (`--force-with-lease=`
// by its prefix too): quiet or verbose, the upstream set, a dry run, no
// hooks (there are none: HOST_UNCONFINED_GIT), a deletion, and the one
// rewrite, `--force-with-lease`, held to a branch that is not main or
// master (host_unconfined_push_refspec).
static const char *const HOST_UNCONFINED_PUSH_FLAGS[] = {"-q", "--quiet", "-v", "--verbose", "-u", "--set-upstream", "-n", "--dry-run", "--porcelain", "--no-verify", "-d", "--delete", "--force-with-lease", "--force-with-lease="};

// The options that force a push or delete what it does not name, each
// refused by name: any of these, and a bundle of short options with an `f`.
static const char *const HOST_UNCONFINED_PUSH_FORCES[] = {"-f", "--force", "--force-if-includes", "--mirror", "--prune", "--all"};

// Whether `line`, trimmed, is a remotes file's line: `user@host:owner/`, the
// user, the owner and the host of letters, digits, `.`, `_` and `-` (the host
// without `_`), since the host goes into ssh's command (HOST_UNCONFINED_GIT).
static bool host_unconfined_remote_line(Str line) {
    size_t i = 0;
    size_t start = 0;
    int part = 0;
    for (; i < line.len; i++) {
        uint8_t c = str_byte(line, i);
        bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || (c == '_' && part != 1);
        if (word) continue;
        char want = part == 0 ? '@' : part == 1 ? ':' : '/';
        if (c != want || i == start) return false;
        part++;
        start = i + 1;
        if (part == 3) break;
    }
    return part == 3 && i + 1 == line.len;
}

// The host of a remotes file's line, between its `@` and its `:`.
static Str host_unconfined_remote_host(Str line) {
    const char *at_sign = str_find_byte(line, '@');
    const char *colon = str_find_byte(line, ':');
    return str_sub(line, (size_t)(at_sign - line.ptr) + 1, (size_t)(colon - line.ptr));
}

// The remotes file's text into `*out` (host_unconfined_list): SHALE_REMOTES's,
// else ~/.config/shale/remotes.
static STATUS_MUST_USE Status host_unconfined_remotes(Run *r, Pos pos, Str *out) {
    return host_unconfined_list(r, pos, "SHALE_REMOTES", S("remotes"), host_unconfined_remote_line, S("a remote owner's: one `user@host:owner/` a line"), out);
}

// The owner and host of a remotes file's line, `user@host:owner/` (held to
// that shape by host_unconfined_remote_line).
static void host_remote_owner_of(Str line, Str *host, Str *owner) {
    *host = host_unconfined_remote_host(line);
    const char *colon = str_find_byte(line, ':');
    *owner = str_sub(line, (size_t)(colon - line.ptr) + 1, line.len - 1);
}

// Whether `owner` is the owner of a github.com line of the remotes file's
// text `remotes`: a fenced pull request's head (host_run_secrets.c).
static bool host_remote_owner_listed(Str remotes, Str owner) {
    size_t from = 0;
    while (from <= remotes.len) {
        size_t to = path_part_end_at(remotes, from, '\n');
        Str line = mounts_trim(str_sub(remotes, from, to), true);
        from = to + 1;
        if (line.len == 0 || str_byte(line, 0) == '#') continue;
        Str host = S("");
        Str o = S("");
        host_remote_owner_of(line, &host, &o);
        if (str_eq(host, S("github.com")) && str_eq(o, owner)) return true;
    }
    return false;
}

// `host("remote_owners")`: the remotes file's lines as `{host, owner}`, in
// its order, `{}` with no file; a script's view of the one list a push, a
// fetch and a fenced pull request's head are held to.
static STATUS_MUST_USE Status host_remote_owners(Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`remote_owners` takes nothing, given {d} arguments"), MESSAGE_ARGS(MESSAGE_FROM_HOST(run_unum(r, args.len))));
    Str remotes = S("");
    TRY(host_unconfined_remotes(r, pos, &remotes));
    Table *list = NULL;
    TRY(run_new_table(r, pos, &list));
    list->kind = TABLE_LIST;
    size_t from = 0;
    while (from <= remotes.len) {
        size_t to = path_part_end_at(remotes, from, '\n');
        Str line = mounts_trim(str_sub(remotes, from, to), true);
        from = to + 1;
        if (line.len == 0 || str_byte(line, 0) == '#') continue;
        Str host = S("");
        Str owner = S("");
        host_remote_owner_of(line, &host, &owner);
        Table *m = NULL;
        TRY(run_new_table(r, pos, &m));
        m->kind = TABLE_MAP;
        TRY(run_map_put_str(r, pos, m, S("host"), host));
        TRY(run_map_put_str(r, pos, m, S("owner"), owner));
        Value mv;
        value_table(&mv, m);
        TRY(run_list_push(r, pos, list, &mv));
    }
    value_table(out, list);
    return OK;
}

// Whether `url` is `<line><repo>.git` for a line of the remotes file's text
// `remotes`, the repo a name of letters, digits, `.`, `_` and `-` that does
// not start with `.` or `-`; that line's host into `*host`.
static bool host_unconfined_remote_ok(Str remotes, Str url, Str *host) {
    size_t from = 0;
    while (from <= remotes.len) {
        size_t to = path_part_end_at(remotes, from, '\n');
        Str line = mounts_trim(str_sub(remotes, from, to), true);
        from = to + 1;
        if (line.len == 0 || str_byte(line, 0) == '#' || !str_starts_with(url, line)) continue;
        Str rest = str_sub(url, line.len, url.len);
        if (rest.len < 5 || !str_eq(str_sub(rest, rest.len - 4, rest.len), S(".git"))) continue;
        Str repo = str_sub(rest, 0, rest.len - 4);
        bool ok = str_byte(repo, 0) != '.' && str_byte(repo, 0) != '-';
        for (size_t i = 0; i < repo.len && ok; i++) {
            uint8_t c = str_byte(repo, i);
            ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        }
        if (!ok) continue;
        *host = host_unconfined_remote_host(line);
        return true;
    }
    return false;
}

// What a confined `git remote get-url` printed, at most 4 KiB of it.
typedef struct {
    Buf got;
    bool over;
} HostUnconfinedKept;

static STATUS_MUST_USE Status host_unconfined_keep(void *ctx, Str bytes) {
    HostUnconfinedKept *k = ctx;
    if (k->got.len + bytes.len > 4096) {
        k->over = true;
        return OK;
    }
    return buf_str(&k->got, bytes);
}

// The URLs of `remote` a push (`push`) or a fetch goes to, every one, as git
// reads them (a push's `pushurl` before the `url`, a fetch's `url` alone,
// each `insteadOf` and `pushInsteadOf` applied): `git [-C dir] remote
// get-url [--push] --all <remote>`, run confined, into `*out`; a git that
// fails stops the run, naming its first line.
static STATUS_MUST_USE Status host_unconfined_urls(Host *h, Run *r, Pos pos, Str cwd, Str dir, Str remote, bool push, Str *out) {
    Str in = dir.len > 0 ? dir : S(".");
    OsStr push_all[8] = {os_str_own(S("git")), os_str_own(S("-C")), os_str_own(in), os_str_own(S("remote")), os_str_own(S("get-url")), os_str_own(S("--push")), os_str_own(S("--all")), os_str_own(remote)};
    OsStr fetch_all[7] = {os_str_own(S("git")), os_str_own(S("-C")), os_str_own(in), os_str_own(S("remote")), os_str_own(S("get-url")), os_str_own(S("--all")), os_str_own(remote)};
    SpawnSpec spec;
    spawn_spec_default(&spec);
    spec.argv.ptr = push ? push_all : fetch_all;
    spec.argv.len = push ? 8 : 7;
    spec.cwd = os_str_own(cwd);
    spec.env = h->env;
    spec.timeout_ms = 10000;
    TRY(host_confine_open(h, r, pos, S("run"), &spec.confine_fd));
    HostUnconfinedKept kept = {buf_make(r->ctx.values), false};
    SpawnSink sink = {&kept, host_unconfined_keep};
    SpawnOutcome o;
    SpawnResult got = spawn_run(r->ctx.values, &spec, sink, &o);
    host_confine_close(&spec.confine_fd);
    if (got == SPAWN_OUT_OF_MEMORY || got == SPAWN_SINK_STOPPED) return run_values_oom(r, pos);
    Str said = buf_view(&kept.got);
    Str first = str_sub(said, 0, path_part_end_at(said, 0, '\n'));
    if (got != SPAWN_OK || o.ended != SPAWN_EXIT || o.code != 0 || kept.over) {
        if (push) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push: `git remote get-url --push --all {s}` failed, so its URL is not known, and it is not pushed: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(remote), MESSAGE_FROM_HOST(first)));
        return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` fetch: `git remote get-url --all {s}` failed, so its URL is not known, and it is not fetched: {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(remote), MESSAGE_FROM_HOST(first)));
    }
    *out = said;
    return OK;
}

// The host every URL of `remote` a push (`push`) or a fetch goes to, into
// `*host`: each URL must be a remote of the remotes file, and all of one
// host.
static STATUS_MUST_USE Status host_unconfined_git_host(Host *h, Run *r, Pos pos, Str cwd, Str dir, Str remote, bool push, Str *host) {
    Str remotes = S("");
    TRY(host_unconfined_remotes(r, pos, &remotes));
    Str urls = S("");
    TRY(host_unconfined_urls(h, r, pos, cwd, dir, remote, push, &urls));
    *host = S("");
    size_t from = 0;
    while (from <= urls.len) {
        size_t to = path_part_end_at(urls, from, '\n');
        Str url = mounts_trim(str_sub(urls, from, to), true);
        from = to + 1;
        if (url.len == 0) continue;
        Str this_host = S("");
        if (!host_unconfined_remote_ok(remotes, url, &this_host) || (host->len > 0 && !str_eq(*host, this_host))) {
            if (!push) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` fetch comes only from a remote of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), `<user@host:owner/><repo>.git`: `{s}` fetches from {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(remote), MESSAGE_FROM_HOST(url)));
            return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push goes only to a remote of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), `<user@host:owner/><repo>.git`: `{s}` pushes to {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(remote), MESSAGE_FROM_HOST(url)));
        }
        *host = this_host;
    }
    if (host->len == 0) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s}: `{s}` has no {s}"), MESSAGE_ARGS(MESSAGE_FROM_HOST(push ? S("push") : S("fetch")), MESSAGE_FROM_SCRIPT(remote), MESSAGE_FROM_HOST(push ? S("push URL") : S("URL"))));
    return OK;
}

// Whether a ref a push forces or deletes, `dst` as written, is one it never
// may: main or master (`main`, `refs/heads/main`, any ref ending `/main`),
// HEAD (the remote's, which names its default branch), or `@`.
static bool host_unconfined_kept_ref(Str dst) {
    if (str_eq(dst, S("HEAD")) || str_eq(dst, S("@")) || dst.len == 0) return true;
    size_t start = dst.len;
    while (start > 0 && str_byte(dst, start - 1) != '/') start--;
    Str last = str_sub(dst, start, dst.len);
    return str_eq(last, S("main")) || str_eq(last, S("master"));
}

static STATUS_MUST_USE Status host_unconfined_forced(Run *r, Pos pos, Str shown) {
    return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push does not force, not {s}: `--force-with-lease` to a branch other than main and master is the one rewrite it takes"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown)));
}

// A push's refspec, word `i`: no `+` (a force), no pattern or negation, and,
// when it deletes (`:dst`, or any under `--delete`) or the push leases
// (`--force-with-lease`), not to main or master (host_unconfined_kept_ref).
static STATUS_MUST_USE Status host_unconfined_push_refspec(Run *r, Pos pos, const HostUnconfinedArgv *a, size_t i, bool deleting, bool lease) {
    Str w = host_unconfined_word(a, i);
    Str shown = host_unconfined_shown(a, i);
    if (str_starts_with(w, S("+"))) return host_unconfined_forced(r, pos, shown);
    if (str_find_byte(w, '*') != NULL || str_starts_with(w, S("^"))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push names each ref whole, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown)));
    const char *colon = str_find_byte(w, ':');
    Str dst = w;
    if (colon != NULL) {
        size_t c = (size_t)(colon - w.ptr);
        deleting = deleting || c == 0;
        dst = str_sub(w, c + 1, w.len);
    }
    if (deleting && host_unconfined_kept_ref(dst)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push does not delete main or master, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown)));
    if (lease && host_unconfined_kept_ref(dst)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push forces (`--force-with-lease`) only a branch other than main and master, named whole, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown)));
    return OK;
}

// The options an unconfined fetch takes, each whole: quiet or verbose, a
// prune of the remote's gone branches, its tags or none, a dry run, and the
// work after it (submodules', gc's, FETCH_HEAD's) left out. No
// `--upload-pack` (a program to ask for on the host), no `--all` or
// `--multiple` (remotes nobody named), no `--recurse-submodules` (another
// repository's URL).
static const char *const HOST_UNCONFINED_FETCH_FLAGS[] = {"-q", "--quiet", "-v", "--verbose", "-p", "--prune", "-t", "--tags", "-n", "--no-tags", "--dry-run", "--no-recurse-submodules", "--no-auto-gc", "--no-auto-maintenance", "--no-write-fetch-head"};

static STATUS_MUST_USE Status host_unconfined_not_git(Run *r, Pos pos, Str why) {
    return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ({s}) is for a `git push`, a `git fetch` or a `git ls-remote --symref`, or `ssh`, `scp` or `rsync` to a host of the hosts file; every other program runs confined to the mounts"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(why)));
}

// Whether `s` is a remote's name a fetch takes: letters, digits, `.`, `_`
// and `-`, not starting with `.` or `-`, since it goes into a setting's key
// (`remote.<name>.uploadpack`, host_run_unconfined).
static bool host_unconfined_remote_name(Str s) {
    if (s.len == 0 || str_byte(s, 0) == '.' || str_byte(s, 0) == '-') return false;
    for (size_t i = 0; i < s.len; i++) {
        uint8_t c = str_byte(s, i);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}

// `fetch [options] <remote> [<refspec>...]`, `fetch` at word `i`, its remote
// into `*remote`: HOST_UNCONFINED_FETCH_FLAGS' options alone, a remote by
// its name (host_unconfined_remote_name, since a URL would not be held to
// the remotes file), and refspecs that name each ref whole and force none
// (no `+`, `*` or `^`, no empty source).
static STATUS_MUST_USE Status host_unconfined_fetch(Run *r, Pos pos, const HostUnconfinedArgv *a, size_t i, Str *remote) {
    HostConfineArgs fetch_flags = {HOST_UNCONFINED_FETCH_FLAGS, sizeof HOST_UNCONFINED_FETCH_FLAGS / sizeof *HOST_UNCONFINED_FETCH_FLAGS};
    size_t n = a->filled.len;
    size_t first = 0;
    size_t positional = 0;
    for (size_t k = i + 1; k < n; k++) {
        Str w = host_unconfined_word(a, k);
        Str shown = host_unconfined_shown(a, k);
        if (!str_starts_with(w, S("-"))) {
            bool whole = !str_starts_with(w, S("+")) && !str_starts_with(w, S("^")) && !str_starts_with(w, S(":")) && str_find_byte(w, '*') == NULL;
            if (positional > 0 && !whole) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` fetch names each ref whole and forces none, not {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown)));
            if (positional == 0) first = k;
            positional++;
            continue;
        }
        if (str_starts_with(w, S("--upload-pack"))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` fetch takes no `--upload-pack`: it names the program the fetch starts"));
        if (!host_unconfined_named(fetch_flags, w)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` fetch does not take {s}: its options are HOST_UNCONFINED_FETCH_FLAGS' (c/host/host_unconfined.c)"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown)));
    }
    if (positional == 0) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` fetch names its remote (`git fetch origin`): a fetch by the branch's own settings could come from a remote nobody checked"));
    *remote = host_unconfined_word(a, first);
    if (!host_unconfined_remote_name(*remote)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` fetch names a remote, not {s}: its URL is held to the remotes file"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(host_unconfined_shown(a, first))));
    return OK;
}

// `ls-remote --symref <remote> HEAD`, `ls-remote` at word `i`, its remote
// into `*remote`: the one form, which asks the remote which branch its HEAD
// names (the git prelude's git_remote_head), by a remote's name.
static STATUS_MUST_USE Status host_unconfined_ls_remote(Run *r, Pos pos, const HostUnconfinedArgv *a, size_t i, Str *remote) {
    size_t n = a->filled.len;
    bool shaped = n == i + 4 && str_eq(host_unconfined_word(a, i + 1), S("--symref")) && str_eq(host_unconfined_word(a, i + 3), S("HEAD"));
    if (!shaped) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ls-remote is `git ls-remote --symref <remote> HEAD` alone: it asks which branch the remote's HEAD names, and nothing else"));
    if (!host_unconfined_remote_name(host_unconfined_word(a, i + 2))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` ls-remote names a remote, not {s}: its URL is held to the remotes file"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(host_unconfined_shown(a, i + 2))));
    *remote = host_unconfined_word(a, i + 2);
    return OK;
}

// `git [-C dir] [-c color.ui=never]... push [options] <remote> <refspec>...`,
// or the same with `fetch` (host_unconfined_fetch) or `ls-remote`
// (host_unconfined_ls_remote), each of which sets `*fetch` (it reads the
// remote by its fetch URLs, with a fetch's settings), into
// `*dir`, `*remote` and `*command` (the command's word): before
// the command, `-C` (a folder under the root or a writable mount) and the
// git prelude's `-c color.ui=never` alone; after a push,
// HOST_UNCONFINED_PUSH_FLAGS' options alone, a remote by its name and at
// least one refspec, each held to host_unconfined_push_refspec.
static STATUS_MUST_USE Status host_unconfined_git(Host *h, Run *r, Pos pos, Str cwd, const HostUnconfinedArgv *a, Str why, Str *dir, Str *remote, bool *fetch, size_t *command) {
    HostConfineArgs flags = {HOST_UNCONFINED_PUSH_FLAGS, sizeof HOST_UNCONFINED_PUSH_FLAGS / sizeof *HOST_UNCONFINED_PUSH_FLAGS};
    HostConfineArgs forces = {HOST_UNCONFINED_PUSH_FORCES, sizeof HOST_UNCONFINED_PUSH_FORCES / sizeof *HOST_UNCONFINED_PUSH_FORCES};
    size_t n = a->filled.len;
    size_t i = 1;
    *dir = S("");
    *fetch = false;
    while (i < n && !str_eq(host_unconfined_word(a, i), S("push")) && !str_eq(host_unconfined_word(a, i), S("fetch")) && !str_eq(host_unconfined_word(a, i), S("ls-remote"))) {
        Str w = host_unconfined_word(a, i);
        if (str_eq(w, S("-C")) && i + 1 < n) {
            TRY(host_unconfined_local(h, r, pos, S("git"), cwd, host_unconfined_word(a, i + 1), host_unconfined_shown(a, i + 1), true));
            *dir = host_unconfined_word(a, i + 1);
            i += 2;
        } else if (str_eq(w, S("-c")) && i + 1 < n && str_eq(host_unconfined_word(a, i + 1), S("color.ui=never"))) {
            i += 2;
        } else if (str_starts_with(w, S("-c"))) {
            return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` git takes no `-c` but `color.ui=never`: a setting could name a program git starts outside the confinement"));
        } else {
            return host_unconfined_not_git(r, pos, why);
        }
    }
    if (i >= n) return host_unconfined_not_git(r, pos, why);
    *command = i;
    if (str_eq(host_unconfined_word(a, i), S("fetch"))) {
        *fetch = true;
        return host_unconfined_fetch(r, pos, a, i, remote);
    }
    if (str_eq(host_unconfined_word(a, i), S("ls-remote"))) {
        *fetch = true;
        return host_unconfined_ls_remote(r, pos, a, i, remote);
    }
    bool deleting = false;
    bool lease = false;
    size_t first = 0;
    size_t positional = 0;
    for (size_t k = i + 1; k < n; k++) {
        Str w = host_unconfined_word(a, k);
        Str shown = host_unconfined_shown(a, k);
        if (!str_starts_with(w, S("-"))) {
            if (positional == 0) first = k;
            positional++;
            continue;
        }
        if (str_starts_with(w, S("--receive-pack")) || str_starts_with(w, S("--exec"))) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push takes no `--receive-pack` or `--exec`: each names a program it would start outside the confinement"));
        bool bundle_forces = !str_starts_with(w, S("--")) && str_find_byte(w, 'f') != NULL;
        if (host_unconfined_named(forces, w) || bundle_forces) return host_unconfined_forced(r, pos, shown);
        if (!host_unconfined_named(flags, w)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push does not take {s}: its options are HOST_UNCONFINED_PUSH_FLAGS' (c/host/host_unconfined.c)"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(shown)));
        deleting = deleting || str_eq(w, S("-d")) || str_eq(w, S("--delete"));
        lease = lease || str_starts_with(w, S("--force-with-lease"));
    }
    if (positional < 2) return host_fail_plain(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push names its remote and what it pushes (`git push origin HEAD:main`): a push by the remote's own settings could force, or go to another branch"));
    *remote = host_unconfined_word(a, first);
    if (str_find_byte(*remote, ':') != NULL || str_find_byte(*remote, '/') != NULL) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` push names a remote, not {s}: its push URL is held to the remotes file"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(host_unconfined_shown(a, first))));
    for (size_t k = first + 1; k < n; k++) {
        if (str_starts_with(host_unconfined_word(a, k), S("-"))) continue; // skip: an option, held above
        TRY(host_unconfined_push_refspec(r, pos, a, k, deleting, lease));
    }
    return OK;
}

// An unconfined program's environment into `*env`: the program's `*env`
// less its PATH (and, for git, HOST_UNCONFINED_GIT_ENV_DROPPED), with
// HOST_UNCONFINED_PATH, the inherited ssh agent when there is one (`agent`,
// Host's), and for git GIT_ALLOW_PROTOCOL=ssh.
static STATUS_MUST_USE Status host_unconfined_env(Run *r, Pos pos, SpawnStrs *env, Str agent, bool git) {
    HostConfineArgs dropped = {HOST_UNCONFINED_GIT_ENV_DROPPED, sizeof HOST_UNCONFINED_GIT_ENV_DROPPED / sizeof *HOST_UNCONFINED_GIT_ENV_DROPPED};
    OsStr *all = ARENA_ARRAY(r->ctx.values, OsStr, env->len + 3);
    if (all == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {all, env->len + 3};
    size_t n = 0;
    for (size_t i = 0; i < env->len; i++) {
        Str e = at(*env, i).s;
        bool drop = str_starts_with(e, S("PATH="));
        for (size_t k = 0; git && k < dropped.len; k++) {
            Str name = {at(dropped, k), strlen(at(dropped, k))};
            drop = drop || (e.len > name.len && str_starts_with(e, name) && str_byte(e, name.len) == '=');
        }
        if (!drop) at(room, n++) = at(*env, i);
    }
    at(room, n++) = os_str_own(S(HOST_UNCONFINED_PATH));
    if (agent.len > 0) at(room, n++) = os_str_own(agent);
    if (git) at(room, n++) = os_str_own(S("GIT_ALLOW_PROTOCOL=ssh"));
    env->ptr = all;
    env->len = n;
    return OK;
}

// The program an unconfined run starts, by its absolute path, into
// `*path`: `name` (git, ssh, scp or rsync) in Host's `unconfined_bin`
// (HOST_UNCONFINED_BIN), which must be there to run and reach a file of
// that name once every link is followed. `argv0` must be the bare name.
// DRAGON: a refused program by another name. The inherited PATH starts with
// ~/.local/bin, which a script may write through the `localbin` mount, so a
// `git` or an `ssh` planted there ran unconfined, with the user's keys, as
// the git or ssh it named; and git ran `core.sshCommand=ssh` along that PATH
// too.
// REMEDY: the program is the system's own, by its absolute path, never one
// PATH finds, and it runs with HOST_UNCONFINED_PATH; git's ssh is named by
// its path too. test_run_guards'
// an_unconfined_program_is_the_system_s_never_one_planted_on_path plants a
// `git` and an `ssh` first on PATH.
static STATUS_MUST_USE Status host_unconfined_program(Host *h, Run *r, Pos pos, Str argv0, Str name, Str *path) {
    if (!str_eq(argv0, name)) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` names its program bare (`git`, `ssh`, `scp` or `rsync`), not {s}: it runs the system's own, by its path"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(argv0)));
    TRY(paths_cat(&builtin_files_of(r)->path, pos, h->unconfined_bin, S("/"), name, path));
    char *c = arena_cstr(r->ctx.values, os_str_own(*path));
    char *real = ARENA_ARRAY(r->ctx.values, char, PATH_MAX);
    if (c == NULL || real == NULL) return run_values_oom(r, pos);
    bool ok = access(c, X_OK) == 0 && realpath(c, real) != NULL && str_eq(host_base_name((Str){real, strlen(real)}), name);
    if (!ok) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s}: the system's own, {s}, is not there to run, or is another program"), MESSAGE_ARGS(MESSAGE_FROM_HOST(name), MESSAGE_FROM_HOST(*path)));
    return OK;
}

// `unconfined = "tag: why"`, held to its shapes, into `*out`: the argv to
// run, `filled` (the program's, `secret_args`' values in place; `argv` when
// there are none), git's with HOST_UNCONFINED_GIT (a fetch's
// HOST_UNCONFINED_GIT_FETCH) and its ssh after `git` and a fetch's
// `--upload-pack=git-upload-pack` after `fetch`, and git's `*env`
// (host_unconfined_git_env). The shapes are checked on
// `filled`, and a refusal names `argv`'s word (HostUnconfinedArgv).
// DRAGON: the file system's boundaries. An unconfined program runs every
// program its settings name with all the user's file powers: a git hook,
// an ssh ProxyCommand, an rsync remote shell, a credential helper; and its
// environment could name one too. And a push with the user's ssh keys is
// the user's: a forced one rewrites a branch, and one to another remote
// sends the code and the key's use elsewhere. A fetch with them reads what
// the key may read, and its settings name more programs: an alternate's
// refs command, an automatic gc, a submodule's fetch.
// REMEDY: the program is the system's own `git`, `ssh`, `scp` or `rsync`,
// by its path (host_unconfined_program), run with HOST_UNCONFINED_PATH;
// each is held to its shape above; no `env` but git's
// GIT_TERMINAL_PROMPT; no secrets folder inside the root or a mount. A push
// forces only by `--force-with-lease`, and never main or master, and
// deletes neither; every push URL of its remote, as git reads them (a
// confined `git remote get-url --push --all`), is one of the remotes file's;
// it runs over ssh alone (GIT_ALLOW_PROTOCOL), and ssh connects to that
// file's host whatever the URL says then (`-o HostName`). A fetch is held
// the same way, by every fetch URL of its remote (`git remote get-url
// --all`), with HOST_UNCONFINED_GIT_FETCH's settings and git's own
// `git-upload-pack` asked for on the host. What is left: a
// repository's config changed between that check and the push could send
// it to another repository on that host, one the key may write; a remote's
// `receivepack`, which names the command run on that host; and the user's
// own ~/.ssh/config, which no mount should make writable. test_run_guards'
// an_unconfined_run_is_a_git_push_or_a_deploy_to_the_prod_host_alone and
// an_unconfined_push_reaches_the_remotes_file_s_remotes_alone and
// an_unconfined_fetch_comes_from_the_remotes_file_s_remotes_alone plant each
// refusal.
static STATUS_MUST_USE Status host_run_unconfined(Host *h, Run *r, Pos pos, SpawnStrs argv, SpawnStrs filled, const HostEnvOpt *env, Str why, Str cwd, SpawnStrs *out, SpawnStrs *spawn_env) {
    Str base = host_base_name(at(argv, 0).s);
    bool git = str_eq(base, S("git"));
    bool ssh_family = str_eq(base, S("ssh")) || str_eq(base, S("scp")) || str_eq(base, S("rsync"));
    if (!git && !ssh_family) return host_unconfined_not_git(r, pos, why);
    Str program = S("");
    TRY(host_unconfined_program(h, r, pos, at(argv, 0).s, base, &program));
    for (size_t j = 0; j < env->names.len; j++) {
        Str k = at(env->names, j);
        if (!(git && str_eq(k, S("GIT_TERMINAL_PROMPT")))) return host_fail(r, pos, CLASS_REFUSED, S("`run`'s `unconfined` {s} takes no `env` (git: but GIT_TERMINAL_PROMPT), not {s}: a variable could name a program it starts"), MESSAGE_ARGS(MESSAGE_FROM_HOST(base), MESSAGE_FROM_SCRIPT(k)));
    }
    TRY(host_confine_secrets_apart(h, r, pos, S("run")));
    // host_secret_args_fill fills word for word, the program's own first.
    if (filled.len != argv.len) return host_fail_plain(r, pos, CLASS_HOST, S("`run`'s `unconfined`: the filled argv is not the script's, word for word"));
    HostUnconfinedArgv a = {filled, argv};
    Str hosts = S("");
    if (ssh_family) {
        TRY(host_unconfined_hosts(r, pos, &hosts));
        if (str_eq(base, S("ssh"))) TRY(host_unconfined_ssh(r, pos, &a, hosts));
        if (str_eq(base, S("scp"))) TRY(host_unconfined_scp(h, r, pos, cwd, &a, hosts));
        if (str_eq(base, S("rsync"))) TRY(host_unconfined_rsync(h, r, pos, cwd, &a, hosts));
        // The filled argv, the program named by its path.
        OsStr *copy = ARENA_ARRAY(r->ctx.values, OsStr, filled.len);
        if (copy == NULL) return run_values_oom(r, pos);
        SLICE_TYPE(CopyRoom, OsStr);
        CopyRoom words = {copy, filled.len};
        at(words, 0) = os_str_own(program);
        for (size_t i = 1; i < filled.len; i++) at(words, i) = at(filled, i);
        out->ptr = copy;
        out->len = filled.len;
        return host_unconfined_env(r, pos, spawn_env, h->agent, false);
    }
    Str dir = S("");
    Str remote = S("");
    bool fetch = false;
    size_t command = 0;
    TRY(host_unconfined_git(h, r, pos, cwd, &a, why, &dir, &remote, &fetch, &command));
    Str host = S("");
    TRY(host_unconfined_git_host(h, r, pos, cwd, dir, remote, !fetch, &host));
    // git's ssh by its path too: git runs core.sshCommand by a shell, along
    // PATH.
    Str ssh = S("");
    TRY(paths_cat(&builtin_files_of(r)->path, pos, S("core.sshCommand="), h->unconfined_bin, S("/ssh -o HostName="), &ssh));
    TRY(paths_cat(&builtin_files_of(r)->path, pos, ssh, host, S(""), &ssh));
    HostConfineArgs fixed = {HOST_UNCONFINED_GIT, sizeof HOST_UNCONFINED_GIT / sizeof *HOST_UNCONFINED_GIT};
    if (fetch) fixed = (HostConfineArgs){HOST_UNCONFINED_GIT_FETCH, sizeof HOST_UNCONFINED_GIT_FETCH / sizeof *HOST_UNCONFINED_GIT_FETCH};
    size_t added = fixed.len + 2;
    size_t n = filled.len + added + (fetch ? 1 : 0);
    OsStr *all = ARENA_ARRAY(r->ctx.values, OsStr, n);
    if (all == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(ArgvRoom, OsStr);
    ArgvRoom room = {all, n};
    at(room, 0) = os_str_own(program);
    for (size_t i = 0; i < fixed.len; i++) at(room, 1 + i) = os_str_own((Str){at(fixed, i), strlen(at(fixed, i))});
    at(room, 1 + fixed.len) = os_str_own(S("-c"));
    at(room, 2 + fixed.len) = os_str_own(ssh);
    size_t to = added + 1;
    for (size_t i = 1; i < filled.len; i++) {
        at(room, to++) = at(filled, i);
        if (fetch && i == command) at(room, to++) = os_str_own(S("--upload-pack=git-upload-pack"));
    }
    out->ptr = all;
    out->len = n;
    return host_unconfined_env(r, pos, spawn_env, h->agent, true);
}
