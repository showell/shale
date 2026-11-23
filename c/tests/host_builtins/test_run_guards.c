// test_run_guards.c: the guards `run` and the jobs hold before and while a
// program runs (c/host/host_refusals.c, host_write_gate.c,
// host_run_secrets.c, host_jobs.c, spawn.c, zig_ast.c), each
// tried by the attack it guards against: a NUL inside an argument, a
// secret's path shown to git, a log written through a read-only mount or
// past the undo journal, a copy's mode given through a read-only mount, a
// FIFO where a file should be, a secret's file
// that grows without bound, an OS path in a message, and a zig-ast that
// hangs or floods its stderr. Every message is C Shale's own.

// A part of ran_tests.c (`TEST_PART`, test.h), which defines _GNU_SOURCE and
// includes test.h and ran.c before it.

// A run on a fresh root with the mounts `mounts` (each `D` the folder).
static void guards_ran_with(Ran *x, const char *mounts, bool undo) { // cstr: a test's literal
    RanOpts o = {mounts, false, undo};
    ran_init(x, o);
}

// What a decoy file under the test's folder holds, or "absent".
static Str guards_decoy(Ran *x, const char *rel) { // cstr: a test's literal
    Str got = S("");
    return ran_file(x, rel, &got) ? got : S("absent");
}

// Whether `s` holds the literal `lit`.
static bool guards_holds(Str s, const char *lit) { // cstr: a test's literal
    Str l = ran_cs(lit);
    return mem_find(s.ptr, s.len, l.ptr, l.len) != SIZE_MAX;
}

static const char *const GUARDS_SECRETS_MOUNT = "k = D/root/keys secrets\n";

// A secrets mount outside the root, as one must be for a program to run
// beside it (host_confine.c).
static const char *const GUARDS_SECRETS_APART = "k = D/keys secrets\n";

TEST(a_nul_in_an_argument_an_env_a_secrets_name_or_a_secret_is_refused_before_any_guard) {
    static Ran x;
    guards_ran_with(&x, GUARDS_SECRETS_MOUNT, false);
    ran_put(&x, "root/keys/tok", "s3cret\n");
    ran_write(&x, "root/keys/nul", ((Str){"s3\0cret\n", 8}));
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\nfunction j(a, o) return host(\"job_start\", a, o) end\n"
                  "print(attempt(f, {\"echo\", \"keys\\0x\"}, {timeout_ms = 5000, tail = 1}).error)\n"
                  "print(attempt(f, {\"echo\"}, {timeout_ms = 5000, tail = 1, env = {F = \"keys\\0x\"}}).error)\n"
                  "print(attempt(f, {\"echo\"}, {timeout_ms = 5000, tail = 1, env = {[\"F\\0G\"] = \"x\"}}).error)\n"
                  "print(attempt(f, {\"echo\"}, {timeout_ms = 5000, tail = 1, secrets = {[\"T\\0K\"] = \"k:tok\"}}).error)\n"
                  "print(attempt(j, {\"echo\", \"keys\\0x\"}, {log = \"j.log\"}).error)\n"
                  "print(attempt(j, {\"echo\"}, {log = \"j.log\", env = {F = \"keys\\0x\"}}).error)\n"
                  "print(attempt(f, {\"echo\"}, {timeout_ms = 5000, tail = 1, secrets = {T = \"k:nul\"}}).error)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`run`'s argument 2 holds a NUL byte, where the kernel would end it: the program would be handed less than the guards saw\n"
                                     "`run`'s `env` F holds a NUL byte, where the kernel would end it: the program would be handed less than the guards saw\n"
                                     "`run`'s `env` name holds a NUL byte, where the kernel would end it: the program would be handed less than the guards saw\n"
                                     "`run`'s `secrets` name holds a NUL byte, where the kernel would end it: the program would be handed less than the guards saw\n"
                                     "`job_start`'s argument 2 holds a NUL byte, where the kernel would end it: the program would be handed less than the guards saw\n"
                                     "`job_start`'s `env` F holds a NUL byte, where the kernel would end it: the program would be handed less than the guards saw\n"
                                     "`run`: the secret `T`'s file k:nul holds a NUL byte, which no environment can carry\n"));
    // Nothing ran, and nothing was recorded.
    EXPECT_EQ(x.r.host_runs, 0);
    EXPECT_EQ(x.lh.records.len, 0);
    ran_free(&x);
}

TEST(a_git_that_cannot_start_is_said_not_taken_for_a_program_it_does_not_track) {
    static Ran x;
    guards_ran_with(&x, "", false);
    ran_put(&x, "root/x", "x");
    // A cwd that is not there: git, asked whether `./x` is tracked, cannot
    // be started in it, and the stop says so.
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\nprint(attempt(f, {\"./x\"}, {timeout_ms = 5000, tail = 1, cwd = \"nope\"}).error)\n"), ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`run` of `./x`: a program by path is run only when git tracks it, and git could not start to say: its folder cannot be entered\n"));
    EXPECT_EQ(x.r.host_runs, 0);
    ran_free(&x);
}

TEST(a_program_by_a_secret_s_path_is_the_secrets_guard_s_before_git_is_asked) {
    static Ran x;
    guards_ran_with(&x, GUARDS_SECRETS_MOUNT, false);
    ran_put(&x, "root/keys/tok", "s3cret\n");
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\nfunction j(a, o) return host(\"job_start\", a, o) end\n"
                  "print(attempt(f, {\"keys/tok\"}, {timeout_ms = 5000, tail = 1}).error)\n"
                  "print(attempt(j, {\"keys/tok\"}, {log = \"j.log\"}).error)\n"
                  "print(attempt(f, {\"./nope.sh\"}, {timeout_ms = 5000, tail = 1}).error)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`run`'s argument 1 names the mount `k`'s folder, which holds secrets: hand one over as `secrets`\n"
                                     "`job_start`'s argument 1 names the mount `k`'s folder, which holds secrets: hand one over as `secrets`\n"
                                     "`run`: a secrets mount's folder is inside the root or another mount, which a program may read, and Landlock cannot hide a folder below one it allows: move the secrets folder out, and then programs run\n"));
    EXPECT_EQ(x.r.host_runs, 0);
    ran_free(&x);
}

TEST(a_log_is_a_change_a_read_only_mount_refuses_it_and_an_undo_puts_it_back) {
    static Ran x;
    guards_ran_with(&x, "ro = D/root/frozen ro\n", true);
    ran_put(&x, "root/frozen/a.txt", "a\n");
    ran_put(&x, "root/old.log", "was\n");
    // The job is refused before its reaper is ever started.
    x.lh.reaper = S("/nonexistent/shale");
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\nfunction j(a, o) return host(\"job_start\", a, o) end\n"
                  "print(attempt(f, {\"echo\", \"x\"}, {timeout_ms = 5000, tail = 1, log = \"ro:a.txt\"}).error)\n"
                  "print(attempt(j, {\"echo\", \"x\"}, {log = \"ro:a.txt\"}).error)\n"
                  "print(attempt(j, {\"echo\", \"x\"}, {log = \"frozen/a.txt\"}).error)\n"
                  "host(\"run\", {\"echo\", \"new\"}, {timeout_ms = 5000, tail = 1, log = \"old.log\"})\n"
                  "print(read(\"old.log\"))\nfail(\"stop\")\n"),
           ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_STR(buf_view(&x.r.out), S("ro:a.txt: mount `ro` is read-only\nro:a.txt: mount `ro` is read-only\nfrozen/a.txt: mount `ro` is read-only\nnew\n\n"));
    // The mount's file was never opened; the truncated log is put back.
    Str got = S("");
    EXPECT(ran_file(&x, "root/frozen/a.txt", &got));
    EXPECT_STR(got, S("a\n"));
    EXPECT(ran_file(&x, "root/old.log", &got));
    EXPECT_STR(got, S("was\n"));
    EXPECT(x.undone.len > 0);
    EXPECT_EQ(x.r.host_runs, 1);
    ran_free(&x);
}

TEST(a_log_or_a_secret_at_a_fifo_is_refused_not_waited_on) {
    static Ran x;
    guards_ran_with(&x, GUARDS_SECRETS_MOUNT, false);
    ran_put(&x, "root/keys/tok", "s3cret\n");
    FileSystemCPath pipe_cp;
    FileSystemCPath secret_cp;
    const char *pipe_c = NULL;
    const char *secret_c = NULL;
    EXPECT_EQ(file_system_cstr(&pipe_cp, os_str_own(ran_at(&x, "root/pipe")), &pipe_c), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_cstr(&secret_cp, os_str_own(ran_at(&x, "root/keys/pipe")), &secret_c), FILE_SYSTEM_OK);
    EXPECT_EQ(mkfifo(pipe_c, 0600), 0);
    EXPECT_EQ(mkfifo(secret_c, 0600), 0);
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\n"
                  "print(attempt(f, {\"echo\"}, {timeout_ms = 5000, tail = 1, log = \"pipe\"}).error)\n"
                  "print(attempt(f, {\"echo\"}, {timeout_ms = 5000, tail = 1, secrets = {X = \"k:pipe\"}}).error)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`run`: its log pipe is there and is not a file\n`run`: the secret `X`'s file k:pipe is there and is not a file\n"));
    EXPECT_EQ(x.r.host_runs, 0);
    ran_free(&x);
}

TEST(stdout_secret_s_file_is_held_to_one_byte_past_its_cap_while_the_program_runs) {
    static Ran x;
    guards_ran_with(&x, GUARDS_SECRETS_APART, false);
    ran_put(&x, "keys/tok", "s3cret\n");
    // About 20 MB to stdout: the kernel stops the file at 65537 bytes.
    ran_run(&x, S("host(\"run\", {\"seq\", \"1\", \"3000000\"}, {timeout_ms = 20000, stdout_secret = \"k:big\"})\n"), ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_ARGUMENT);
    EXPECT_STR(x.r.ctx.stop.message, S("`run`: `seq` printed 65537 bytes, past the 65536 `secrets` reads: nothing is stored in k:big"));
    // Nothing stored, and no temporary file left beside it.
    Str got = S("");
    EXPECT(!ran_file(&x, "keys/big", &got));
    char tmp[64];
    int n = snprintf(tmp, sizeof tmp, "keys/.big.%d.shale-tmp", (int)getpid());
    EXPECT(n > 0);
    EXPECT(!ran_file(&x, tmp, &got));
    ran_free(&x);
}

TEST(job_wait_names_the_script_s_path_never_the_os_path) {
    static Ran x;
    guards_ran_with(&x, NULL, false);
    char exe[PATH_MAX];
    EXPECT(realpath("c/out/tcc/shale", exe) != NULL);
    x.lh.reaper = ran_cs(exe);
    ran_put(&x, "root/shut/x.log", "x\n");
    FileSystemCPath shut_cp;
    const char *shut = NULL;
    EXPECT_EQ(file_system_cstr(&shut_cp, os_str_own(ran_at(&x, "root/shut")), &shut), FILE_SYSTEM_OK);
    EXPECT_EQ(chmod(shut, 0), 0);
    ran_run(&x, S("function w(j, o) return host(\"job_wait\", j, o) end\n"
                  "local j = host(\"job_start\", {\"true\"}, {log = \"w.log\"})\n"
                  "print(attempt(w, j, {timeout_ms = 10000, file = \"shut/x.log\", contains = \"x\"}).error)\n"),
           ran_budgets());
    EXPECT_EQ(chmod(shut, 0700), 0);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`job_wait`: shut/x.log cannot be read: AccessDenied\n"));
    ran_free(&x);
}

// A fake zig-ast at the folder's `rel`: a shell script, made runnable.
static Str guards_zig_fake(Ran *x, const char *rel, const char *script) { // cstr: test literals
    ran_put(x, rel, script);
    Str p = ran_at(x, rel);
    FileSystemCPath cp;
    const char *c = NULL;
    if (file_system_cstr(&cp, os_str_own(p), &c) != FILE_SYSTEM_OK || chmod(c, 0755) != 0) abort();
    return p;
}

TEST(zig_ast_s_stderr_is_drained_while_it_answers_and_a_hung_one_stops_at_the_deadline) {
    static Ran x;
    guards_ran_with(&x, NULL, false);
    // The deadline is the run's own, so a shorter one here is this run's
    // alone.
    x.zig.answer_ms = 2000;
    ran_put(&x, "root/a.zig", "const a = 1;\n");
    // 200,000 bytes on stderr before the answer: a host that did not drain
    // stderr would never get it.
    Str flood = guards_zig_fake(&x, "flood.sh", "#!/bin/sh\necho 'zig-ast serve 1 fake'\nread n\nhead -c \"$n\" >/dev/null\nhead -c 200000 /dev/zero | tr '\\0' x >&2\nprintf '12\\nbad\\tDrained\\n'\n");
    FileSystemCPath flood_cp;
    const char *flood_c = NULL;
    EXPECT_EQ(file_system_cstr(&flood_cp, os_str_own(flood), &flood_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_ZIG_AST", flood_c, 1), 0);
    ran_run(&x, S("host(\"zig_decls\", \"a.zig\")\n"), ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_STR(x.r.ctx.stop.message, S("`zig_decls`: zig-ast refused the request: Drained"));
    ran_free(&x);

    static Ran y;
    guards_ran_with(&y, NULL, false);
    EXPECT_EQ(y.zig.answer_ms, 60000);
    y.zig.answer_ms = 2000;
    ran_put(&y, "root/a.zig", "const a = 1;\n");
    Str hang = guards_zig_fake(&y, "hang.sh", "#!/bin/sh\necho 'zig-ast serve 1 fake'\nsleep 30\n");
    FileSystemCPath hang_cp;
    const char *hang_c = NULL;
    EXPECT_EQ(file_system_cstr(&hang_cp, os_str_own(hang), &hang_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_ZIG_AST", hang_c, 1), 0);
    ran_run(&y, S("host(\"zig_tokens\", \"a.zig\")\n"), ran_budgets());
    EXPECT_EQ(y.ran, STOP);
    EXPECT_STR(y.r.ctx.stop.message, S("`zig_tokens`: zig-ast did not answer within 2000 ms"));
    Str got = S("");
    EXPECT(ran_file(&y, "root/a.zig", &got));
    EXPECT_STR(got, S("const a = 1;\n"));
    ran_free(&y);
    EXPECT_EQ(unsetenv("SHALE_ZIG_AST"), 0);
}

// zig-ast runs confined as `run`'s programs do: a fake one, outside every
// mount, writes a decoy beside it before it serves, and the decoy is never
// made.
TEST(zig_ast_runs_confined_to_the_mounts) {
    static Ran x;
    guards_ran_with(&x, NULL, false);
    ran_put(&x, "root/a.zig", "const a = 1;\n");
    Str script = ran_d(&x, "#!/bin/sh\necho w > D/decoy.txt 2>/dev/null\necho 'zig-ast serve 1 fake'\nread n\nhead -c \"$n\" >/dev/null\nprintf '9\\nbad\\tdone\\n'\n");
    Str fake = guards_zig_fake(&x, "fake.sh", "");
    ran_write(&x, "fake.sh", script);
    FileSystemCPath fake_cp;
    const char *fake_c = NULL;
    EXPECT_EQ(file_system_cstr(&fake_cp, os_str_own(fake), &fake_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_ZIG_AST", fake_c, 1), 0);
    ran_run(&x, S("host(\"zig_decls\", \"a.zig\")\n"), ran_budgets());
    EXPECT_EQ(unsetenv("SHALE_ZIG_AST"), 0);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_STR(x.r.ctx.stop.message, S("`zig_decls`: zig-ast refused the request: done"));
    EXPECT_STR(guards_decoy(&x, "decoy.txt"), S("absent"));
    ran_free(&x);
}

// A bare repository at the OS path `path`, made by git run unconfined, as no
// confined program could make it outside every mount.
static void guards_git_bare(Ran *x, Str path) {
    OsStr argv[5] = {os_str_own(S("git")), os_str_own(S("init")), os_str_own(S("-q")), os_str_own(S("--bare")), os_str_own(path)};
    SpawnSpec s;
    spawn_spec_default(&s);
    s.argv = (SpawnStrs){argv, 5};
    s.timeout_ms = 10000;
    SpawnSink sink = {NULL, host_discard};
    SpawnOutcome o;
    if (spawn_run(&x->host, &s, sink, &o) != SPAWN_OK || o.ended != SPAWN_EXIT || o.code != 0) abort();
}

// The host's unconfined programs (Host's `unconfined_bin`) are the fakes in
// the folder `rel`, with the system's own git linked in beside them when
// they hold none: a test's stand-in for /usr/bin.
static void guards_unconfined_bin(Ran *x, const char *rel) { // cstr: a test's literal
    Str dir = ran_at(x, rel);
    Buf g = buf_make(&x->host);
    if (buf_str(&g, dir) != OK || buf_lit(&g, "/git") != OK) abort();
    const char *git = arena_cstr(&x->host, os_str_own(buf_view(&g)));
    if (git == NULL) abort();
    if (access(git, F_OK) != 0 && symlink("/usr/bin/git", git) != 0) abort();
    x->lh.unconfined_bin = dir;
}

// ssh reads its known hosts and its configuration from a decoy home, and no
// key file there; gh's configuration stays closed; every program gets its
// caches in the `build` mount's cache folder, Playwright's browsers there
// too, unless the script's `env` says otherwise.
TEST(ssh_reads_its_known_hosts_and_no_key_and_the_caches_are_the_build_mount_s) {
    static Ran x;
    guards_ran_with(&x, "build = D/b\n", false);
    ran_put(&x, "home/.ssh/known_hosts", "known\n");
    ran_put(&x, "home/.ssh/config", "config\n");
    ran_put(&x, "home/.ssh/id_ed25519", "key\n");
    ran_put(&x, "home/.config/gh/hosts.yml", "token\n");
    ran_put(&x, "b/.keep", "");
    static OsStr home_env[1];
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {home_env, 1};
    at(room, 0) = os_str_own(ran_d(&x, "HOME=D/home"));
    x.lh.env = (SpawnStrs){home_env, 1};
    // The home's path comes from a file: the source holds capital Ds.
    ran_write(&x, "root/home.txt", ran_at(&x, "home"));
    ran_run(&x, S("function c(a) return host(\"run\", a, {timeout_ms = 5000, tail = 3}).code end\n"
                  "local h = read(\"home.txt\")\n"
                  "print(c({\"cp\", h .. \"/.ssh/known_hosts\", \"kh\"}))\n"
                  "print(c({\"cp\", h .. \"/.ssh/config\", \"cf\"}))\n"
                  "print(c({\"cp\", h .. \"/.ssh/id_ed25519\", \"key\"}))\n"
                  "print(c({\"cp\", h .. \"/.config/gh/hosts.yml\", \"gh\"}))\n"
                  "print(host(\"run\", {\"printenv\", \"XDG_CACHE_HOME\", \"ZIG_GLOBAL_CACHE_DIR\", \"PLAYWRIGHT_BROWSERS_PATH\"}, {timeout_ms = 5000, tail = 3}).out)\n"
                  "print(host(\"run\", {\"printenv\", \"XDG_CACHE_HOME\"}, {timeout_ms = 5000, tail = 3, env = {XDG_CACHE_HOME = \"mine\"}}).out)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), ran_d(&x, "0\n0\n1\n1\nD/b/cache\nD/b/cache\nD/b/cache/ms-playwright\n\nmine\n\n"));
    EXPECT_STR(guards_decoy(&x, "root/kh"), S("known\n"));
    EXPECT_STR(guards_decoy(&x, "root/key"), S("absent"));
    EXPECT_STR(guards_decoy(&x, "root/gh"), S("absent"));
    ran_free(&x);
}

// `unconfined` runs a deploy's `ssh`, `scp` and `rsync` to a host of a
// planted hosts file (SHALE_HOSTS), and a git push (the next test's), and
// nothing else: another program (one with a `push` among its words too), a
// `-c`, a `--receive-pack`, another host, an ssh option, an scp option, a
// local path outside the mounts, an rsync shell, option or daemon, and an
// `env` are each refused; a confined push cannot reach a bare repository no
// mount names. No test reaches any host: `ssh`, `scp` and `rsync` are fakes
// first on PATH that say how they were called, so a planted fault that let
// one through would print, never connect.
TEST(an_unconfined_run_is_a_git_push_or_a_deploy_to_the_prod_host_alone) {
    static Ran x;
    guards_ran_with(&x, "", false);
    guards_zig_fake(&x, "fakebin/ssh", "#!/bin/sh\necho \"fake $(basename \"$0\") $*\"\n");
    guards_zig_fake(&x, "fakebin/scp", "#!/bin/sh\necho \"fake $(basename \"$0\") $*\"\n");
    guards_zig_fake(&x, "fakebin/rsync", "#!/bin/sh\necho \"fake $(basename \"$0\") $*\"\n");
    static OsStr path_env[1];
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {path_env, 1};
    Buf pb = buf_make(&x.host);
    EXPECT_EQ(buf_lit(&pb, "PATH="), OK);
    EXPECT_EQ(buf_str(&pb, ran_at(&x, "fakebin")), OK);
    EXPECT_EQ(buf_lit(&pb, ":/usr/bin:/bin"), OK);
    at(room, 0) = os_str_own(buf_view(&pb));
    x.lh.env = (SpawnStrs){path_env, 1};
    guards_unconfined_bin(&x, "fakebin");
    ran_put(&x, "hosts", "# the deploys' hosts\n\ndeploy@example.com\n");
    FileSystemCPath hosts_cp;
    const char *hosts_c = NULL;
    EXPECT_EQ(file_system_cstr(&hosts_cp, os_str_own(ran_at(&x, "hosts")), &hosts_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_HOSTS", hosts_c, 1), 0);
    guards_git_bare(&x, ran_at(&x, "outside.git"));
    ran_write(&x, "root/where.txt", ran_at(&x, "outside.git"));
    ran_run(&x, S("function c(a, o) return host(\"run\", a, o) end\n"
                  "function g(args)\n  local argv = {\"git\", \"-c\", \"user.name=t\", \"-c\", \"user.email=t@t\"}\n  for a in args do push(argv, a) end\n  return c(argv, {timeout_ms = 10000, tail = 3}).code\nend\n"
                  "local out = read(\"where.txt\")\n"
                  "print(g({\"init\", \"-q\", \"-b\", \"master\"}), g({\"add\", \"where.txt\"}), g({\"commit\", \"-q\", \"-m\", \"m\"}))\n"
                  "local o = {timeout_ms = 10000, tail = 3}\n"
                  "print(c({\"git\", \"push\", \"-q\", out, \"master\"}, o).code ~= 0)\n"
                  "o.unconfined = \"t: past the mounts\"\n"
                  "print(attempt(c, {\"cp\", \"where.txt\", \"x\"}, o).error)\n"
                  "print(attempt(c, {\"cp\", \"push\", \"x\"}, o).error)\n"
                  "print(attempt(c, {\"ssh\", \"steve@example.org\", \"true\"}, o).error)\n"
                  "print(attempt(c, {\"ssh\", \"-o\", \"ProxyCommand=x\", \"deploy@example.com\"}, o).error)\n"
                  "print(attempt(c, {\"ssh\", \"-F\", \"f\", \"deploy@example.com\"}, o).error)\n"
                  "print(attempt(c, {\"scp\", \"-S\", \"x\", \"where.txt\", \"deploy@example.com:/tmp/x\"}, o).error)\n"
                  "print(attempt(c, {\"scp\", \"-q\", \"-r\", \".\", \"deploy@example.com:/tmp/x\"}, o).error)\n"
                  "print(attempt(c, {\"scp\", \"where.txt\", \"other@example.com:/tmp/x\"}, o).error)\n"
                  "print(attempt(c, {\"scp\", \"deploy@example.com:/etc/hosts\", \"../x.txt\"}, o).error)\n"
                  "print(attempt(c, {\"rsync\", \"-e\", \"ssh -o ProxyCommand=x\", \"where.txt\", \"deploy@example.com:/tmp/\"}, o).error)\n"
                  "print(attempt(c, {\"rsync\", \"--rsync-path=x\", \"where.txt\", \"deploy@example.com:/tmp/\"}, o).error)\n"
                  "print(attempt(c, {\"rsync\", \"-avz\", \"where.txt\", \"deploy@example.com::mod\"}, o).error)\n"
                  "print(attempt(c, {\"ssh\", \"deploy@example.com\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\", env = {SSH_AUTH_SOCK = \"x\"}}).error)\n"
                  "print(c({\"ssh\", \"deploy@example.com\", \"true\"}, o).out)\n"
                  "print(c({\"scp\", \"-q\", \"-O\", \"where.txt\", \"deploy@example.com:/tmp/x\"}, o).out)\n"
                  "print(c({\"rsync\", \"-rlt\", \"--delete\", \"--chmod=D755,F644\", \"-e\", \"ssh\", \"where.txt\", \"deploy@example.com:/tmp/\"}, o).out)\n"
                  "print(attempt(c, {\"git\", \"-c\", \"core.sshCommand=x\", \"push\", out, \"master\"}, o).error)\n"
                  "print(attempt(c, {\"git\", \"push\", \"--receive-pack=x\", out, \"master\"}, o).error)\n"
                  "print(attempt(c, {\"git\", \"push\", out, \"master\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\", env = {GIT_SSH_COMMAND = \"x\"}}).error)\n"),
           ran_budgets());
    EXPECT_EQ(unsetenv("SHALE_HOSTS"), 0);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("0\t0\t0\ntrue\n"
                                     "`run`'s `unconfined` (t: past the mounts) is for a `git push`, a `git fetch` or a `git ls-remote --symref`, or `ssh`, `scp` or `rsync` to a host of the hosts file; every other program runs confined to the mounts\n"
                                     "`run`'s `unconfined` (t: past the mounts) is for a `git push`, a `git fetch` or a `git ls-remote --symref`, or `ssh`, `scp` or `rsync` to a host of the hosts file; every other program runs confined to the mounts\n"
                                     "`run`'s `unconfined` ssh goes only to a host of the hosts file (~/.config/shale/hosts, or SHALE_HOSTS's), not `steve@example.org`\n"
                                     "`run`'s `unconfined` ssh takes `-o` for BatchMode=yes and LogLevel=QUIET alone, not `ProxyCommand=x`: another setting could name a program, a file or a hop\n"
                                     "`run`'s `unconfined` ssh takes no option but `-o`, not -F: an option could name a program, a config or a hop\n"
                                     "`run`'s `unconfined` scp takes -q, -p, -O and `-o`, not -S: `-r` follows the links in a folder, and another option could name a program, a config or a hop\n"
                                     "`run`'s `unconfined` scp takes -q, -p, -O and `-o`, not -r: `-r` follows the links in a folder, and another option could name a program, a config or a hop\n"
                                     "`run`'s `unconfined` scp goes only to a host of the hosts file (~/.config/shale/hosts, or SHALE_HOSTS's), not `other@example.com`\n"
                                     "`run`'s `unconfined` scp: its local path ../x.txt is under neither the root nor a mount\n"
                                     "`run`'s `unconfined` rsync takes `-e ssh` alone of a remote shell, with `-o BatchMode=yes` and `-o LogLevel=QUIET` after it: any other is a program it would start\n"
                                     "`run`'s `unconfined` rsync does not take --rsync-path=x: its options are HOST_UNCONFINED_RSYNC_LETTERS' and HOST_UNCONFINED_RSYNC_LONG's (c/host/host_unconfined.c)\n"
                                     "`run`'s `unconfined` rsync takes no rsync daemon's module (`host::module`), not deploy@example.com::mod\n"
                                     "`run`'s `env` SSH_AUTH_SOCK names a socket outside the confinement, even with a want: no program Shale starts gets the session's D-Bus, its runtime folder, ssh's agent or Claude Code's messaging\n"
                                     "fake ssh deploy@example.com true\n\n"
                                     "fake scp -q -O where.txt deploy@example.com:/tmp/x\n\n"
                                     "fake rsync -rlt --delete --chmod=D755,F644 -e ssh where.txt deploy@example.com:/tmp/\n\n"
                                     "`run`'s `unconfined` git takes no `-c` but `color.ui=never`: a setting could name a program git starts outside the confinement\n"
                                     "`run`'s `unconfined` push takes no `--receive-pack` or `--exec`: each names a program it would start outside the confinement\n"
                                     "`run`'s `unconfined` git takes no `env` (git: but GIT_TERMINAL_PROMPT), not GIT_SSH_COMMAND: a variable could name a program it starts\n"));
    ran_free(&x);
}

// An unconfined push goes to a remote of a planted remotes file
// (SHALE_REMOTES) alone, by every push URL git reads for it (an
// `insteadOf` and a `pushurl` among them), over ssh alone, and to the
// file's host whatever the URL says: the forms the box pushes with run (HEAD
// to main, `main`, a lane's branch and a lease to it after a rewind, a
// deletion of a lane), and a force (an option, a bundle, a `+`, a mirror), a
// lease or a deletion of main or master, another option, a pattern, a push
// with no refspec, a URL for a remote, another remote and a `-C` outside the
// mounts are each refused. `ssh` is a fake first on PATH that says how git
// called it and hands the push to a bare repository outside every mount,
// so nothing is reached; the record says the push was unconfined.
TEST(an_unconfined_push_reaches_the_remotes_file_s_remotes_alone) {
    static Ran x;
    guards_ran_with(&x, "", false);
    guards_zig_fake(&x, "fakebin/ssh", "");
    ran_write(&x, "fakebin/ssh", ran_d(&x, "#!/bin/sh\necho \"$* allow=$GIT_ALLOW_PROTOCOL\" >> D/ssh.log\nexec git receive-pack D/github.git\n"));
    // Decoys first on PATH, as a script could plant them in ~/.local/bin:
    // each leaves a mark outside every mount, which only an unconfined one
    // could write, and the git runs the system's after, so the confined
    // calls work as before.
    guards_zig_fake(&x, "decoy/git", "");
    ran_write(&x, "decoy/git", ran_d(&x, "#!/bin/sh\necho planted > D/planted-git 2>/dev/null\nexec /usr/bin/git \"$@\"\n"));
    guards_zig_fake(&x, "decoy/ssh", "");
    ran_write(&x, "decoy/ssh", ran_d(&x, "#!/bin/sh\necho planted > D/planted-ssh 2>/dev/null\nexit 1\n"));
    static OsStr path_env[1];
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {path_env, 1};
    Buf pb = buf_make(&x.host);
    EXPECT_EQ(buf_lit(&pb, "PATH="), OK);
    EXPECT_EQ(buf_str(&pb, ran_at(&x, "decoy")), OK);
    EXPECT_EQ(buf_lit(&pb, ":"), OK);
    EXPECT_EQ(buf_str(&pb, ran_at(&x, "fakebin")), OK);
    EXPECT_EQ(buf_lit(&pb, ":/usr/bin:/bin"), OK);
    at(room, 0) = os_str_own(buf_view(&pb));
    x.lh.env = (SpawnStrs){path_env, 1};
    guards_unconfined_bin(&x, "fakebin");
    ran_put(&x, "remotes", "# the box's\n\ngit@github.com:showell/\n");
    FileSystemCPath remotes_cp;
    const char *remotes_c = NULL;
    EXPECT_EQ(file_system_cstr(&remotes_cp, os_str_own(ran_at(&x, "remotes")), &remotes_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_REMOTES", remotes_c, 1), 0);
    guards_git_bare(&x, ran_at(&x, "github.git"));
    ran_put(&x, "root/where.txt", "w\n");
    ran_run(&x, S("function c(a, o) return host(\"run\", a, o) end\n"
                  "function g(args)\n  local argv = {\"git\", \"-c\", \"user.name=t\", \"-c\", \"user.email=t@t\"}\n  for a in args do push(argv, a) end\n  return c(argv, {timeout_ms = 10000, tail = 3}).code\nend\n"
                  "function p(args)\n  local argv = {\"git\"}\n  for a in args do push(argv, a) end\n  return c(argv, {timeout_ms = 10000, tail = 3, unconfined = \"t: push\"}).code\nend\n"
                  "function no(args)\n  local argv = {\"git\", \"push\", \"-q\"}\n  for a in args do push(argv, a) end\n  return attempt(c, argv, {timeout_ms = 10000, tail = 3, unconfined = \"t: push\"}).error\nend\n"
                  "print(g({\"init\", \"-q\", \"-b\", \"main\"}), g({\"add\", \"where.txt\"}), g({\"commit\", \"-q\", \"-m\", \"m1\"}), g({\"commit\", \"-q\", \"--allow-empty\", \"-m\", \"m2\"}))\n"
                  "print(g({\"remote\", \"add\", \"origin\", \"git@github.com:showell/t.git\"}), g({\"remote\", \"add\", \"other\", \"git@github.com:someone/t.git\"}), g({\"remote\", \"add\", \"short\", \"git@gh:showell/t.git\"}), g({\"remote\", \"add\", \"sneaky\", \"git@github.com:showell/s.git\"}), g({\"remote\", \"add\", \"twice\", \"git@github.com:showell/t.git\"}))\n"
                  "print(g({\"config\", \"url.git@github.com:showell/.insteadOf\", \"git@gh:showell/\"}), g({\"config\", \"url.git@evil.example:x/.insteadOf\", \"git@github.com:showell/s\"}), g({\"config\", \"--add\", \"remote.twice.pushurl\", \"git@github.com:showell/t.git\"}), g({\"config\", \"--add\", \"remote.twice.pushurl\", \"git@github.com:someone/t.git\"}))\n"
                  "print(no({\"--force\", \"origin\", \"lane\"}))\n"
                  "print(no({\"-qf\", \"origin\", \"lane\"}))\n"
                  "print(no({\"origin\", \"+lane\"}))\n"
                  "print(no({\"--mirror\", \"origin\"}))\n"
                  "print(no({\"--force-with-lease\", \"origin\", \"HEAD:main\"}))\n"
                  "print(no({\"--force-with-lease\", \"origin\", \"master\"}))\n"
                  "print(no({\"origin\", \"--delete\", \"main\"}))\n"
                  "print(no({\"origin\", \":refs/heads/master\"}))\n"
                  "print(no({\"--repo=x\", \"origin\", \"HEAD:main\"}))\n"
                  "print(no({\"origin\", \"lane*\"}))\n"
                  "print(no({\"origin\"}))\n"
                  "print(no({\"git@github.com:showell/t.git\", \"HEAD:main\"}))\n"
                  "print(no({\"other\", \"HEAD:main\"}))\n"
                  "print(no({\"sneaky\", \"HEAD:main\"}))\n"
                  "print(no({\"twice\", \"HEAD:main\"}))\n"
                  "print(attempt(c, {\"git\", \"-C\", \"..\", \"push\", \"origin\", \"HEAD:main\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: push\"}).error)\n"
                  "print(p({\"-C\", \".\", \"-c\", \"color.ui=never\", \"push\", \"-q\", \"origin\", \"HEAD:refs/heads/main\"}), p({\"push\", \"origin\", \"main\"}))\n"
                  "print(g({\"branch\", \"lane\"}), p({\"push\", \"-q\", \"-u\", \"origin\", \"lane\"}), g({\"branch\", \"-f\", \"lane\", \"HEAD~1\"}))\n"
                  "print(p({\"push\", \"-q\", \"origin\", \"lane\"}) ~= 0, p({\"push\", \"-q\", \"--force-with-lease\", \"origin\", \"lane\"}), p({\"push\", \"-q\", \"short\", \"HEAD:refs/heads/short\"}))\n"
                  "print(p({\"push\", \"-q\", \"origin\", \"--delete\", \"lane\"}))\n"),
           ran_budgets());
    EXPECT_EQ(unsetenv("SHALE_REMOTES"), 0);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("0\t0\t0\t0\n0\t0\t0\t0\t0\n0\t0\t0\t0\n"
                                     "`run`'s `unconfined` push does not force, not --force: `--force-with-lease` to a branch other than main and master is the one rewrite it takes\n"
                                     "`run`'s `unconfined` push does not force, not -qf: `--force-with-lease` to a branch other than main and master is the one rewrite it takes\n"
                                     "`run`'s `unconfined` push does not force, not +lane: `--force-with-lease` to a branch other than main and master is the one rewrite it takes\n"
                                     "`run`'s `unconfined` push does not force, not --mirror: `--force-with-lease` to a branch other than main and master is the one rewrite it takes\n"
                                     "`run`'s `unconfined` push forces (`--force-with-lease`) only a branch other than main and master, named whole, not HEAD:main\n"
                                     "`run`'s `unconfined` push forces (`--force-with-lease`) only a branch other than main and master, named whole, not master\n"
                                     "`run`'s `unconfined` push does not delete main or master, not main\n"
                                     "`run`'s `unconfined` push does not delete main or master, not :refs/heads/master\n"
                                     "`run`'s `unconfined` push does not take --repo=x: its options are HOST_UNCONFINED_PUSH_FLAGS' (c/host/host_unconfined.c)\n"
                                     "`run`'s `unconfined` push names each ref whole, not lane*\n"
                                     "`run`'s `unconfined` push names its remote and what it pushes (`git push origin HEAD:main`): a push by the remote's own settings could force, or go to another branch\n"
                                     "`run`'s `unconfined` push names a remote, not git@github.com:showell/t.git: its push URL is held to the remotes file\n"
                                     "`run`'s `unconfined` push goes only to a remote of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), `<user@host:owner/><repo>.git`: `other` pushes to git@github.com:someone/t.git\n"
                                     "`run`'s `unconfined` push goes only to a remote of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), `<user@host:owner/><repo>.git`: `sneaky` pushes to git@evil.example:x/.git\n"
                                     "`run`'s `unconfined` push goes only to a remote of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), `<user@host:owner/><repo>.git`: `twice` pushes to git@github.com:someone/t.git\n"
                                     "`run`'s `unconfined` git: its local path .. is under neither the root nor a mount\n"
                                     "0\t0\n"
                                     "0\t0\t0\n"
                                     "true\t0\t0\n"
                                     "0\n"));
    Str ref = S("");
    EXPECT(ran_file(&x, "github.git/refs/heads/main", &ref));
    EXPECT(ran_file(&x, "github.git/refs/heads/short", &ref));
    EXPECT(!ran_file(&x, "github.git/refs/heads/lane", &ref));
    Str said = S("");
    EXPECT(ran_file(&x, "ssh.log", &said));
    EXPECT(guards_holds(said, "-o HostName=github.com "));
    EXPECT(guards_holds(said, "git@github.com git-receive-pack 'showell/t.git' allow=ssh\n"));
    EXPECT(x.lh.records.len > 0);
    EXPECT(str_starts_with(at(x.lh.records, x.lh.records.len - 1), S("run git push -q origin --delete lane (unconfined: t: push) -> exit 0")));
    EXPECT_STR(guards_decoy(&x, "planted-git"), S("absent"));
    EXPECT_STR(guards_decoy(&x, "planted-ssh"), S("absent"));
    ran_free(&x);
}
// An unconfined fetch comes from a remote of a planted remotes file
// (SHALE_REMOTES) alone, by every fetch URL git reads for it (an `insteadOf`
// among them, never a `pushurl`), over ssh alone to the file's host, asking
// the host for git's own upload-pack whatever the remote's `uploadpack`
// says, and with no hook of the repository's: a plain fetch of a remote runs,
// and an `--upload-pack`, a `-c`, `--all`, `--recurse-submodules`, no remote,
// a URL for a remote, a forced refspec, another remote, a remote whose
// `pushurl` alone is in the file, and another git command are each refused.
// `ssh` is a fake first on PATH that says how git called it and hands the
// push that fills the remote, then each fetch, to a bare repository outside
// every mount, so nothing is reached; the record says the fetch was
// unconfined.
TEST(an_unconfined_fetch_comes_from_the_remotes_file_s_remotes_alone) {
    static Ran x;
    guards_ran_with(&x, "", false);
    guards_zig_fake(&x, "fakebin/ssh", "");
    ran_write(&x, "fakebin/ssh", ran_d(&x, "#!/bin/sh\necho \"$* allow=$GIT_ALLOW_PROTOCOL\" >> D/ssh.log\ncase \"$*\" in *git-receive-pack*) exec git receive-pack D/github.git;; esac\nexec git upload-pack D/github.git\n"));
    guards_zig_fake(&x, "hooks/reference-transaction", "");
    ran_write(&x, "hooks/reference-transaction", ran_d(&x, "#!/bin/sh\necho hooked >> D/hooked.log\n"));
    static OsStr path_env[1];
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {path_env, 1};
    Buf pb = buf_make(&x.host);
    EXPECT_EQ(buf_lit(&pb, "PATH="), OK);
    EXPECT_EQ(buf_str(&pb, ran_at(&x, "fakebin")), OK);
    EXPECT_EQ(buf_lit(&pb, ":/usr/bin:/bin"), OK);
    at(room, 0) = os_str_own(buf_view(&pb));
    x.lh.env = (SpawnStrs){path_env, 1};
    guards_unconfined_bin(&x, "fakebin");
    ran_put(&x, "remotes", "git@github.com:showell/\n");
    FileSystemCPath remotes_cp;
    const char *remotes_c = NULL;
    EXPECT_EQ(file_system_cstr(&remotes_cp, os_str_own(ran_at(&x, "remotes")), &remotes_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_REMOTES", remotes_c, 1), 0);
    guards_git_bare(&x, ran_at(&x, "github.git"));
    ran_put(&x, "root/where.txt", "w\n");
    // The hooks' path comes from a file: the source holds capital Ds.
    ran_write(&x, "root/hooks.txt", ran_at(&x, "hooks"));
    ran_run(&x, S("function c(a, o) return host(\"run\", a, o) end\n"
                  "function g(args)\n  local argv = {\"git\", \"-c\", \"user.name=t\", \"-c\", \"user.email=t@t\"}\n  for a in args do push(argv, a) end\n  return c(argv, {timeout_ms = 10000, tail = 3}).code\nend\n"
                  "function f(args)\n  local argv = {\"git\"}\n  for a in args do push(argv, a) end\n  return c(argv, {timeout_ms = 10000, tail = 3, unconfined = \"t: fetch\"}).code\nend\n"
                  "function no(args)\n  local argv = {\"git\"}\n  for a in args do push(argv, a) end\n  return attempt(c, argv, {timeout_ms = 10000, tail = 3, unconfined = \"t: fetch\"}).error\nend\n"
                  "print(g({\"init\", \"-q\", \"-b\", \"main\"}), g({\"add\", \"where.txt\"}), g({\"commit\", \"-q\", \"-m\", \"m1\"}))\n"
                  "print(g({\"remote\", \"add\", \"origin\", \"git@github.com:showell/t.git\"}), g({\"remote\", \"add\", \"other\", \"git@github.com:someone/t.git\"}), g({\"remote\", \"add\", \"split\", \"git@github.com:someone/t.git\"}), g({\"config\", \"remote.split.pushurl\", \"git@github.com:showell/t.git\"}), g({\"remote\", \"add\", \"short\", \"git@gh:showell/t.git\"}), g({\"config\", \"url.git@github.com:showell/.insteadOf\", \"git@gh:showell/\"}))\n"
                  "print(f({\"push\", \"-q\", \"origin\", \"main\"}), g({\"update-ref\", \"-d\", \"refs/remotes/origin/main\"}), g({\"config\", \"remote.origin.uploadpack\", \"evil-pack\"}), g({\"config\", \"core.hooksPath\", read(\"hooks.txt\")}))\n"
                  "print(no({\"fetch\", \"--upload-pack=x\", \"origin\"}))\n"
                  "print(no({\"fetch\", \"--upload-pack\", \"x\", \"origin\"}))\n"
                  "print(no({\"-c\", \"core.sshCommand=x\", \"fetch\", \"origin\"}))\n"
                  "print(no({\"fetch\", \"--all\"}))\n"
                  "print(no({\"fetch\", \"--recurse-submodules\", \"origin\"}))\n"
                  "print(no({\"fetch\", \"-q\"}))\n"
                  "print(no({\"fetch\", \"git@github.com:showell/t.git\"}))\n"
                  "print(no({\"fetch\", \"origin\", \"+main:refs/heads/x\"}))\n"
                  "print(no({\"fetch\", \"other\"}))\n"
                  "print(no({\"fetch\", \"split\"}))\n"
                  "print(no({\"clone\", \"git@github.com:showell/t.git\", \"x\"}))\n"
                  "print(f({\"-C\", \".\", \"-c\", \"color.ui=never\", \"fetch\", \"-q\", \"--prune\", \"origin\"}), f({\"fetch\", \"-q\", \"short\"}))\n"),
           ran_budgets());
    EXPECT_EQ(unsetenv("SHALE_REMOTES"), 0);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("0\t0\t0\n0\t0\t0\t0\t0\t0\n0\t0\t0\t0\n"
                                     "`run`'s `unconfined` fetch takes no `--upload-pack`: it names the program the fetch starts\n"
                                     "`run`'s `unconfined` fetch takes no `--upload-pack`: it names the program the fetch starts\n"
                                     "`run`'s `unconfined` git takes no `-c` but `color.ui=never`: a setting could name a program git starts outside the confinement\n"
                                     "`run`'s `unconfined` fetch does not take --all: its options are HOST_UNCONFINED_FETCH_FLAGS' (c/host/host_unconfined.c)\n"
                                     "`run`'s `unconfined` fetch does not take --recurse-submodules: its options are HOST_UNCONFINED_FETCH_FLAGS' (c/host/host_unconfined.c)\n"
                                     "`run`'s `unconfined` fetch names its remote (`git fetch origin`): a fetch by the branch's own settings could come from a remote nobody checked\n"
                                     "`run`'s `unconfined` fetch names a remote, not git@github.com:showell/t.git: its URL is held to the remotes file\n"
                                     "`run`'s `unconfined` fetch names each ref whole and forces none, not +main:refs/heads/x\n"
                                     "`run`'s `unconfined` fetch comes only from a remote of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), `<user@host:owner/><repo>.git`: `other` fetches from git@github.com:someone/t.git\n"
                                     "`run`'s `unconfined` fetch comes only from a remote of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), `<user@host:owner/><repo>.git`: `split` fetches from git@github.com:someone/t.git\n"
                                     "`run`'s `unconfined` (t: fetch) is for a `git push`, a `git fetch` or a `git ls-remote --symref`, or `ssh`, `scp` or `rsync` to a host of the hosts file; every other program runs confined to the mounts\n"
                                     "0\t0\n"));
    Str ref = S("");
    EXPECT(ran_file(&x, "root/.git/refs/remotes/origin/main", &ref));
    EXPECT(ran_file(&x, "root/.git/refs/remotes/short/main", &ref));
    EXPECT_STR(guards_decoy(&x, "hooked.log"), S("absent"));
    Str said = S("");
    EXPECT(ran_file(&x, "ssh.log", &said));
    EXPECT(guards_holds(said, "-o HostName=github.com "));
    EXPECT(guards_holds(said, "git@github.com git-upload-pack 'showell/t.git' allow=ssh\n"));
    EXPECT(!guards_holds(said, "evil-pack"));
    EXPECT(x.lh.records.len > 0);
    EXPECT(str_starts_with(at(x.lh.records, x.lh.records.len - 1), S("run git fetch -q short (unconfined: t: fetch) -> exit 0")));
    ran_free(&x);
}
// An unconfined ls-remote is `git ls-remote --symref <remote> HEAD` alone, the
// question which branch the remote's HEAD names (git_remote_head), held as a
// fetch is: a remote of the remotes file by every fetch URL, over ssh to the
// file's host, git's own upload-pack asked for whatever the remote's
// `uploadpack` says. Another form (no `--symref`, no `HEAD`, a pattern, an
// `--upload-pack`), a URL for a remote, another remote and a remote whose
// `pushurl` alone is in the file are each refused. `ssh` is a fake first on
// PATH that hands the push that fills the remote, then the ls-remote, to a
// bare repository outside every mount, whose HEAD names main.
TEST(an_unconfined_ls_remote_asks_the_remotes_file_s_remotes_for_their_head_alone) {
    static Ran x;
    guards_ran_with(&x, "", false);
    guards_zig_fake(&x, "fakebin/ssh", "");
    ran_write(&x, "fakebin/ssh", ran_d(&x, "#!/bin/sh\necho \"$* allow=$GIT_ALLOW_PROTOCOL\" >> D/ssh.log\ncase \"$*\" in *git-receive-pack*) exec git receive-pack D/github.git;; esac\nexec git upload-pack D/github.git\n"));
    static OsStr path_env[1];
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {path_env, 1};
    Buf pb = buf_make(&x.host);
    EXPECT_EQ(buf_lit(&pb, "PATH="), OK);
    EXPECT_EQ(buf_str(&pb, ran_at(&x, "fakebin")), OK);
    EXPECT_EQ(buf_lit(&pb, ":/usr/bin:/bin"), OK);
    at(room, 0) = os_str_own(buf_view(&pb));
    x.lh.env = (SpawnStrs){path_env, 1};
    guards_unconfined_bin(&x, "fakebin");
    ran_put(&x, "remotes", "git@github.com:showell/\n");
    FileSystemCPath remotes_cp;
    const char *remotes_c = NULL;
    EXPECT_EQ(file_system_cstr(&remotes_cp, os_str_own(ran_at(&x, "remotes")), &remotes_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_REMOTES", remotes_c, 1), 0);
    guards_git_bare(&x, ran_at(&x, "github.git"));
    ran_write(&x, "github.git/HEAD", S("ref: refs/heads/main\n"));
    ran_put(&x, "root/where.txt", "w\n");
    ran_run(&x, S("function c(a, o) return host(\"run\", a, o) end\n"
                  "function g(args)\n  local argv = {\"git\", \"-c\", \"user.name=t\", \"-c\", \"user.email=t@t\"}\n  for a in args do push(argv, a) end\n  return c(argv, {timeout_ms = 10000, tail = 3}).code\nend\n"
                  "function u(args)\n  local argv = {\"git\"}\n  for a in args do push(argv, a) end\n  return c(argv, {timeout_ms = 10000, tail = 3, unconfined = \"t: head\"})\nend\n"
                  "function no(args)\n  local argv = {\"git\"}\n  for a in args do push(argv, a) end\n  return attempt(c, argv, {timeout_ms = 10000, tail = 3, unconfined = \"t: head\"}).error\nend\n"
                  "print(g({\"init\", \"-q\", \"-b\", \"main\"}), g({\"add\", \"where.txt\"}), g({\"commit\", \"-q\", \"-m\", \"m1\"}))\n"
                  "print(g({\"remote\", \"add\", \"origin\", \"git@github.com:showell/t.git\"}), g({\"remote\", \"add\", \"other\", \"git@github.com:someone/t.git\"}), g({\"remote\", \"add\", \"split\", \"git@github.com:someone/t.git\"}), g({\"config\", \"remote.split.pushurl\", \"git@github.com:showell/t.git\"}))\n"
                  "print(u({\"push\", \"-q\", \"origin\", \"main\"}).code, g({\"config\", \"remote.origin.uploadpack\", \"evil-pack\"}))\n"
                  "print(no({\"ls-remote\", \"origin\"}))\n"
                  "print(no({\"ls-remote\", \"--symref\", \"origin\"}))\n"
                  "print(no({\"ls-remote\", \"--symref\", \"origin\", \"main*\"}))\n"
                  "print(no({\"ls-remote\", \"--upload-pack=x\", \"--symref\", \"origin\", \"HEAD\"}))\n"
                  "print(no({\"ls-remote\", \"--symref\", \"git@github.com:showell/t.git\", \"HEAD\"}))\n"
                  "print(no({\"ls-remote\", \"--symref\", \"other\", \"HEAD\"}))\n"
                  "print(no({\"ls-remote\", \"--symref\", \"split\", \"HEAD\"}))\n"
                  "print(lines(u({\"-C\", \".\", \"-c\", \"color.ui=never\", \"ls-remote\", \"--symref\", \"origin\", \"HEAD\"}).out)[1])\n"),
           ran_budgets());
    EXPECT_EQ(unsetenv("SHALE_REMOTES"), 0);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("0\t0\t0\n0\t0\t0\t0\n0\t0\n"
                                     "`run`'s `unconfined` ls-remote is `git ls-remote --symref <remote> HEAD` alone: it asks which branch the remote's HEAD names, and nothing else\n"
                                     "`run`'s `unconfined` ls-remote is `git ls-remote --symref <remote> HEAD` alone: it asks which branch the remote's HEAD names, and nothing else\n"
                                     "`run`'s `unconfined` ls-remote is `git ls-remote --symref <remote> HEAD` alone: it asks which branch the remote's HEAD names, and nothing else\n"
                                     "`run`'s `unconfined` ls-remote is `git ls-remote --symref <remote> HEAD` alone: it asks which branch the remote's HEAD names, and nothing else\n"
                                     "`run`'s `unconfined` ls-remote names a remote, not git@github.com:showell/t.git: its URL is held to the remotes file\n"
                                     "`run`'s `unconfined` fetch comes only from a remote of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), `<user@host:owner/><repo>.git`: `other` fetches from git@github.com:someone/t.git\n"
                                     "`run`'s `unconfined` fetch comes only from a remote of the remotes file (~/.config/shale/remotes, or SHALE_REMOTES's), `<user@host:owner/><repo>.git`: `split` fetches from git@github.com:someone/t.git\n"
                                     "ref: refs/heads/main\tHEAD\n"));
    Str said = S("");
    EXPECT(ran_file(&x, "ssh.log", &said));
    EXPECT(guards_holds(said, "-o HostName=github.com "));
    EXPECT(guards_holds(said, "git@github.com git-upload-pack 'showell/t.git' allow=ssh\n"));
    EXPECT(!guards_holds(said, "evil-pack"));
    EXPECT(x.lh.records.len > 0);
    EXPECT(str_starts_with(at(x.lh.records, x.lh.records.len - 1), S("run git -C . -c color.ui=never ls-remote --symref origin HEAD (unconfined: t: head) -> exit 0")));
    ran_free(&x);
}

// gh runs with GH_CONFIG_DIR an empty folder of the run's own, its token from
// `secrets` alone, scrubbed whole and in part (its first 33 characters, as
// `gh auth status` prints them); `gh auth` (status, token, git-credential,
// past an option too) and `gh alias` are refused even with a want, and so is
// an `env` that sets GH_CONFIG_DIR or a token; `auth` as a later word is
// gh's to read. `gh` is a fake in a read-only mount first on PATH that says
// what it was handed, so nothing is reached.
TEST(gh_reads_no_configuration_and_never_says_its_token) {
    static Ran x;
    // The fake's folder is a mount: a confined program runs from the mounts
    // alone, and execvp would pass a refused fake by for the real gh.
    guards_ran_with(&x, "k = D/keys secrets\nfake = D/fakebin ro\n", false);
    guards_zig_fake(&x, "fakebin/gh", "#!/bin/sh\ncase \"$GH_CONFIG_DIR\" in */gh-config) echo \"config: the run's, $(ls -A \"$GH_CONFIG_DIR\" | wc -l) files\";; *) echo \"config: $GH_CONFIG_DIR\";; esac\necho \"token=$GH_TOKEN\"\nprintf 'prefix=%.33s\\n' \"$GH_TOKEN\"\n");
    static OsStr path_env[1];
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {path_env, 1};
    Buf pb = buf_make(&x.host);
    EXPECT_EQ(buf_lit(&pb, "PATH="), OK);
    EXPECT_EQ(buf_str(&pb, ran_at(&x, "fakebin")), OK);
    EXPECT_EQ(buf_lit(&pb, ":/usr/bin:/bin"), OK);
    at(room, 0) = os_str_own(buf_view(&pb));
    x.lh.env = (SpawnStrs){path_env, 1};
    ran_put(&x, "keys/github-token", "ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ab\n");
    ran_run(&x, S("function c(a, o) return host(\"run\", a, o) end\n"
                  "local s = {GH_TOKEN = \"k:github-token\"}\n"
                  "print(c({\"gh\", \"api\", \"user\"}, {timeout_ms = 10000, tail = 5, secrets = s}).out)\n"
                  "print(attempt(c, {\"gh\", \"auth\", \"status\"}, {timeout_ms = 10000, tail = 5, secrets = s}).error)\n"
                  "print(attempt(c, {\"gh\", \"auth\", \"token\"}, {timeout_ms = 10000, tail = 5, want = \"t: x\"}).error)\n"
                  "print(attempt(c, {\"gh\", \"--help\", \"auth\", \"git-credential\"}, {timeout_ms = 10000, tail = 5}).error)\n"
                  "print(attempt(c, {\"gh\", \"alias\", \"set\", \"t\", \"auth token\"}, {timeout_ms = 10000, tail = 5}).error)\n"
                  "print(attempt(c, {\"gh\", \"api\", \"user\"}, {timeout_ms = 10000, tail = 5, env = {GH_CONFIG_DIR = \"x\"}}).error)\n"
                  "print(attempt(c, {\"gh\", \"api\", \"user\"}, {timeout_ms = 10000, tail = 5, env = {GH_TOKEN = \"x\"}, want = \"t: x\"}).error)\n"
                  "print(c({\"gh\", \"pr\", \"list\", \"--search\", \"auth\"}, {timeout_ms = 10000, tail = 5}).code)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("config: the run's, 0 files\ntoken=<secret GH_TOKEN>\nprefix=<secret GH_TOKEN>\n\n"
                                     "`gh auth` is refused, even with a want: `gh auth status` prints the token's first characters, `gh auth token` and `gh auth git-credential` all of it, and gh needs no login here: its token comes from `secrets = {GH_TOKEN = \"keys:github-token\"}`\n"
                                     "`gh auth` is refused, even with a want: `gh auth status` prints the token's first characters, `gh auth token` and `gh auth git-credential` all of it, and gh needs no login here: its token comes from `secrets = {GH_TOKEN = \"keys:github-token\"}`\n"
                                     "`gh auth` is refused, even with a want: `gh auth status` prints the token's first characters, `gh auth token` and `gh auth git-credential` all of it, and gh needs no login here: its token comes from `secrets = {GH_TOKEN = \"keys:github-token\"}`\n"
                                     "`gh alias` is refused, even with a want: an alias could run `gh auth token` by another name\n"
                                     "`run`'s `env` GH_CONFIG_DIR is the host's, even with a want: gh reads its configuration from an empty folder of the run's own, and its token from `secrets = {GH_TOKEN = \"keys:github-token\"}`\n"
                                     "`run`'s `env` GH_TOKEN would put a token in the script's hands, even with a want: `secrets = {GH_TOKEN = \"keys:github-token\"}` hands it to gh, scrubbed\n"
                                     "0\n"));
    EXPECT(!guards_holds(buf_view(&x.r.out), "ABCDEFGHIJKL"));
    ran_free(&x);
}

// The hosts file is the one list of an unconfined copy's hosts, and Shale
// ships none: with no SHALE_HOSTS and no ~/.config/shale/hosts (a decoy home)
// no host is allowed; a SHALE_HOSTS that names no file, and a line that is
// not `user@host`, stop the run naming the file and the line. `ssh` is a fake
// first on PATH, so nothing is reached.
TEST(an_unconfined_copy_s_hosts_are_the_hosts_file_s_and_none_without_one) {
    static Ran x;
    guards_ran_with(&x, "", false);
    guards_zig_fake(&x, "fakebin/ssh", "#!/bin/sh\necho \"fake $*\"\n");
    static OsStr path_env[1];
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {path_env, 1};
    Buf pb = buf_make(&x.host);
    EXPECT_EQ(buf_lit(&pb, "PATH="), OK);
    EXPECT_EQ(buf_str(&pb, ran_at(&x, "fakebin")), OK);
    EXPECT_EQ(buf_lit(&pb, ":/usr/bin:/bin"), OK);
    at(room, 0) = os_str_own(buf_view(&pb));
    x.lh.env = (SpawnStrs){path_env, 1};
    guards_unconfined_bin(&x, "fakebin");
    ran_put(&x, "home/.keep", "");
    const char *old_home = getenv("HOME"); // cstr: the environment's
    char home_room[PATH_MAX];
    int kept = snprintf(home_room, sizeof home_room, "%s", old_home != NULL ? old_home : "");
    EXPECT(kept >= 0);
    FileSystemCPath home_cp;
    const char *home_c = NULL;
    EXPECT_EQ(file_system_cstr(&home_cp, os_str_own(ran_at(&x, "home")), &home_c), FILE_SYSTEM_OK);
    EXPECT_EQ(unsetenv("SHALE_HOSTS"), 0);
    EXPECT_EQ(setenv("HOME", home_c, 1), 0);
    ran_run(&x, S("function c(a, o) return host(\"run\", a, o) end\n"
                  "print(attempt(c, {\"ssh\", \"deploy@example.com\", \"true\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\"}).error)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`run`'s `unconfined` ssh goes only to a host of the hosts file (~/.config/shale/hosts, or SHALE_HOSTS's), not `deploy@example.com`\n"));
    EXPECT_EQ(setenv("HOME", home_room, 1), 0);
    ran_free(&x);

    static Ran y;
    guards_ran_with(&y, "", false);
    // y's own fake first on PATH: x's folder is gone.
    Buf pyb = buf_make(&y.host);
    EXPECT_EQ(buf_lit(&pyb, "PATH="), OK);
    EXPECT_EQ(buf_str(&pyb, ran_at(&y, "fakebin")), OK);
    EXPECT_EQ(buf_lit(&pyb, ":/usr/bin:/bin"), OK);
    at(room, 0) = os_str_own(buf_view(&pyb));
    y.lh.env = (SpawnStrs){path_env, 1};
    guards_zig_fake(&y, "fakebin/ssh", "#!/bin/sh\necho \"fake $*\"\n");
    guards_unconfined_bin(&y, "fakebin");
    ran_put(&y, "bad", "deploy@example.com\ndeploy at example.com\n");
    FileSystemCPath bad_cp;
    FileSystemCPath none_cp;
    const char *bad_c = NULL;
    const char *none_c = NULL;
    EXPECT_EQ(file_system_cstr(&bad_cp, os_str_own(ran_at(&y, "bad")), &bad_c), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_cstr(&none_cp, os_str_own(ran_at(&y, "none")), &none_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_HOSTS", bad_c, 1), 0);
    ran_run(&y, S("host(\"run\", {\"ssh\", \"deploy@example.com\", \"true\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\"})\n"), ran_budgets());
    EXPECT_EQ(y.ran, STOP);
    EXPECT(str_starts_with(y.r.ctx.stop.message, S("`run`'s `unconfined`: the hosts file ")));
    Str bad_end = S(", line 2, is not a host's: one `user@host` a line");
    Str said = y.r.ctx.stop.message;
    EXPECT(said.len > bad_end.len && str_eq(str_sub(said, said.len - bad_end.len, said.len), bad_end));
    ran_free(&y);

    // A SHALE_HOSTS that names no file: y's folder, gone, holds none.
    static Ran z;
    guards_ran_with(&z, "", false);
    EXPECT_EQ(setenv("SHALE_HOSTS", none_c, 1), 0);
    ran_run(&z, S("host(\"run\", {\"ssh\", \"deploy@example.com\", \"true\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\"})\n"), ran_budgets());
    EXPECT_EQ(z.ran, STOP);
    EXPECT(str_starts_with(z.r.ctx.stop.message, S("`run`'s `unconfined`: the hosts file ")));
    EXPECT_EQ(unsetenv("SHALE_HOSTS"), 0);
    ran_free(&z);
}

// A deploy's host may be a secret, `secret_args`' `<secret PROD>`, as
// roc-apps' and roc-chat's deploys hand it, with ssh's `-o BatchMode=yes -o
// LogLevel=QUIET`: the unconfined shapes are checked on the value the
// program is handed, against the hosts file, and every refusal names the
// marker, never the value. The prod host by its secret runs unconfined (its
// fakes read a decoy outside every mount, and are handed the value, by its
// length); another host by a secret, a secret that is an ssh setting, an ssh
// setting not taken, and any other program are refused; the same ssh without
// `unconfined`, and another program handed a secret, stay confined. No
// record, refusal or output holds a value. `ssh`, `scp` and `rsync` are fakes
// in a read-only mount first on PATH, so nothing is reached.
TEST(an_unconfined_deploy_s_host_may_be_a_secret_and_is_never_shown) {
    static Ran x;
    guards_ran_with(&x, "k = D/keys secrets\nfake = D/fakebin ro\n", false);
    const char *fake = "#!/bin/sh\necho \"fake $(basename \"$0\") $*\"\nprintf '%s' \"$*\" | wc -c\ncat ../decoy.txt 2>/dev/null || echo sealed\n";
    guards_zig_fake(&x, "fakebin/ssh", fake);
    guards_zig_fake(&x, "fakebin/scp", fake);
    guards_zig_fake(&x, "fakebin/rsync", fake);
    static OsStr path_env[1];
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {path_env, 1};
    Buf pb = buf_make(&x.host);
    EXPECT_EQ(buf_lit(&pb, "PATH="), OK);
    EXPECT_EQ(buf_str(&pb, ran_at(&x, "fakebin")), OK);
    EXPECT_EQ(buf_lit(&pb, ":/usr/bin:/bin"), OK);
    at(room, 0) = os_str_own(buf_view(&pb));
    x.lh.env = (SpawnStrs){path_env, 1};
    guards_unconfined_bin(&x, "fakebin");
    ran_put(&x, "keys/prod", "deploy@192.0.2.7\n");
    ran_put(&x, "keys/other", "other@192.0.2.8\n");
    ran_put(&x, "keys/opt", "-oProxyCommand=x\n");
    ran_put(&x, "decoy.txt", "outside\n");
    ran_write(&x, "keys/src", ran_at(&x, "decoy.txt"));
    ran_put(&x, "root/where.txt", "w\n");
    ran_put(&x, "hosts", "deploy@192.0.2.7\n");
    FileSystemCPath hosts_cp;
    const char *hosts_c = NULL;
    EXPECT_EQ(file_system_cstr(&hosts_cp, os_str_own(ran_at(&x, "hosts")), &hosts_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_HOSTS", hosts_c, 1), 0);
    ran_run(&x, S("function c(a, o) return host(\"run\", a, o) end\n"
                  "function u(name, file)\n  local o = {timeout_ms = 10000, tail = 5, unconfined = \"t: deploy\", secret_args = {}}\n  o.secret_args[name] = file\n  return o\nend\n"
                  "function confined(name, file)\n  local o = {timeout_ms = 10000, tail = 5, secret_args = {}}\n  o.secret_args[name] = file\n  return o\nend\n"
                  "print(c({\"ssh\", \"-o\", \"BatchMode=yes\", \"-o\", \"LogLevel=QUIET\", \"<secret PROD>\", \"true\"}, u(\"PROD\", \"k:prod\")).out)\n"
                  "print(c({\"scp\", \"-q\", \"-oBatchMode=yes\", \"-o\", \"LogLevel=QUIET\", \"where.txt\", \"<secret PROD>:/tmp/x\"}, u(\"PROD\", \"k:prod\")).out)\n"
                  "print(c({\"rsync\", \"-rlt\", \"--delete\", \"-e\", \"ssh -o BatchMode=yes -o LogLevel=QUIET\", \"where.txt\", \"<secret PROD>:/srv/x/\"}, u(\"PROD\", \"k:prod\")).out)\n"
                  "print(attempt(c, {\"ssh\", \"<secret OTHER>\", \"true\"}, u(\"OTHER\", \"k:other\")).error)\n"
                  "print(attempt(c, {\"scp\", \"where.txt\", \"<secret OTHER>:/tmp/x\"}, u(\"OTHER\", \"k:other\")).error)\n"
                  "print(attempt(c, {\"ssh\", \"<secret OPT>\", \"deploy@192.0.2.7\", \"true\"}, u(\"OPT\", \"k:opt\")).error)\n"
                  "print(attempt(c, {\"ssh\", \"-o\", \"LogLevel=DEBUG\", \"<secret PROD>\", \"true\"}, u(\"PROD\", \"k:prod\")).error)\n"
                  "print(attempt(c, {\"rsync\", \"-e\", \"ssh -o ProxyCommand=x\", \"where.txt\", \"<secret PROD>:/srv/x/\"}, u(\"PROD\", \"k:prod\")).error)\n"
                  "print(attempt(c, {\"expr\", \"<secret PROD>\", \":\", \"x\"}, u(\"PROD\", \"k:prod\")).error)\n"
                  "print(c({\"ssh\", \"<secret PROD>\", \"true\"}, confined(\"PROD\", \"k:prod\")).out)\n"
                  "print(c({\"cp\", \"<secret SRC>\", \"got\"}, confined(\"SRC\", \"k:src\")).code ~= 0, exists(\"got\"))\n"),
           ran_budgets());
    EXPECT_EQ(unsetenv("SHALE_HOSTS"), 0);
    EXPECT(x.parsed);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    // `deploy@192.0.2.7` is 16 bytes: the fakes' counts are of the value's
    // words, "<secret PROD>" being 13.
    EXPECT_STR(buf_view(&x.r.out), S("fake ssh -o BatchMode=yes -o LogLevel=QUIET <secret PROD> true\n56\noutside\n\n"
                                     "fake scp -q -oBatchMode=yes -o LogLevel=QUIET where.txt <secret PROD>:/tmp/x\n70\noutside\n\n"
                                     "fake rsync -rlt --delete -e ssh -o BatchMode=yes -o LogLevel=QUIET where.txt <secret PROD>:/srv/x/\n90\noutside\n\n"
                                     "`run`'s `unconfined` ssh goes only to a host of the hosts file (~/.config/shale/hosts, or SHALE_HOSTS's), not `<secret OTHER>`\n"
                                     "`run`'s `unconfined` scp goes only to a host of the hosts file (~/.config/shale/hosts, or SHALE_HOSTS's), not `<secret OTHER>`\n"
                                     "`run`'s `unconfined` ssh takes `-o` for BatchMode=yes and LogLevel=QUIET alone, not `<secret OPT>`: another setting could name a program, a file or a hop\n"
                                     "`run`'s `unconfined` ssh takes `-o` for BatchMode=yes and LogLevel=QUIET alone, not `LogLevel=DEBUG`: another setting could name a program, a file or a hop\n"
                                     "`run`'s `unconfined` rsync takes `-e ssh` alone of a remote shell, with `-o BatchMode=yes` and `-o LogLevel=QUIET` after it: any other is a program it would start\n"
                                     "`run`'s `unconfined` (t: deploy) is for a `git push`, a `git fetch` or a `git ls-remote --symref`, or `ssh`, `scp` or `rsync` to a host of the hosts file; every other program runs confined to the mounts\n"
                                     "fake ssh <secret PROD> true\n21\nsealed\n\n"
                                     "true\tfalse\n"));
    EXPECT(!guards_holds(buf_view(&x.r.out), "192.0.2"));
    EXPECT(x.lh.records.len >= 5);
    for (size_t i = 0; i < x.lh.records.len; i++) EXPECT(!guards_holds(at(x.lh.records, i), "192.0.2"));
    EXPECT(str_starts_with(at(x.lh.records, 0), S("run ssh -o BatchMode=yes -o LogLevel=QUIET <secret PROD> true argv PROD=<secret k:prod> (unconfined: t: deploy) -> exit 0")));
    EXPECT_STR(guards_decoy(&x, "root/got"), S("absent"));
    ran_free(&x);
}

// The permission bits of the file at `rel`.
static mode_t guards_mode(Ran *x, const char *rel) { // cstr: a test's literal
    FileSystemCPath cp;
    const char *c = NULL;
    struct stat st;
    if (file_system_cstr(&cp, os_str_own(ran_at(x, rel)), &c) != FILE_SYSTEM_OK || stat(c, &st) != 0) abort();
    return st.st_mode & 07777;
}

TEST(copy_mode_gives_a_copy_its_source_s_bits_and_a_read_only_mount_refuses_it) {
    static Ran x;
    guards_ran_with(&x, "ro = D/root/frozen ro\n", false);
    guards_zig_fake(&x, "root/run.sh", "#!/bin/sh\n");
    ran_put(&x, "root/copy.sh", "#!/bin/sh\n");
    ran_put(&x, "root/frozen/a.sh", "#!/bin/sh\n");
    FileSystemCPath cp;
    FileSystemCPath frozen_cp;
    const char *c = NULL;
    const char *frozen = NULL;
    EXPECT_EQ(file_system_cstr(&cp, os_str_own(ran_at(&x, "root/copy.sh")), &c), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_cstr(&frozen_cp, os_str_own(ran_at(&x, "root/frozen/a.sh")), &frozen), FILE_SYSTEM_OK);
    EXPECT_EQ(chmod(c, 0600), 0);
    EXPECT_EQ(chmod(frozen, 0600), 0);
    ran_run(&x, S("function m(a, b) return host(\"copy_mode\", a, b) end\n"
                  "print(host(\"copy_mode\", \"run.sh\", \"copy.sh\"))\n"
                  "print(attempt(m, \"run.sh\", \"ro:a.sh\").error)\n"
                  "print(attempt(m, \"gone.sh\", \"copy.sh\").error)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("true\nro:a.sh: mount `ro` is read-only\n`copy`: gone.sh's mode could not be given to copy.sh: FileNotFound\n"));
    // The copy took the source's bits; the read-only mount's file kept its own.
    EXPECT_EQ(guards_mode(&x, "root/copy.sh"), 0755);
    EXPECT_EQ(guards_mode(&x, "root/frozen/a.sh"), 0600);
    ran_free(&x);
}

// A link made in the root to a refused program, under an innocent name: the
// guards read the name of the file it reaches. The link is never run.
TEST(a_refused_program_by_another_name_is_refused_by_the_one_it_reaches) {
    static Ran x;
    guards_ran_with(&x, "", false);
    FileSystemCPath link_cp;
    const char *link_c = NULL;
    EXPECT_EQ(file_system_cstr(&link_cp, os_str_own(ran_at(&x, "root/tool")), &link_c), FILE_SYSTEM_OK);
    EXPECT_EQ(symlink("/bin/bash", link_c), 0);
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\nfunction j(a, o) return host(\"job_start\", a, o) end\n"
                  "print(attempt(f, {\"./tool\", \"-c\", \"exit 0\"}, {timeout_ms = 5000, tail = 1}).error)\n"
                  "print(attempt(j, {\"./tool\", \"-c\", \"exit 0\"}, {log = \"j.log\"}).error)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`run` of `tool`, which is `bash`: a shell or an interpreter: Shale is the script; {want = \"tag: why\"} to run it anyway\n"
                                     "`job_start` of `tool`, which is `bash`: a shell or an interpreter: Shale is the script; {want = \"tag: why\"} to run it anyway\n"));
    EXPECT_EQ(x.r.host_runs, 0);
    ran_free(&x);
}

// The lists' names past the first ones: another shell, an editor that runs
// commands, a text tool, a wrapper and a grep by another name.
TEST(each_kind_of_refused_program_is_refused_by_its_other_names) {
    static Ran x;
    guards_ran_with(&x, "", false);
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\n"
                  "for p in {\"rbash\", \"tclsh\", \"vim\", \"ed\", \"tac\", \"mawk\", \"flock\", \"unshare\", \"script\"} do print(attempt(f, {p}, {timeout_ms = 5000, tail = 1}).error) end\n"
                  "print(attempt(f, {\"zgrep\"}, {timeout_ms = 5000, tail = 1, want = \"t: x\"}).error)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`run` of `rbash`: a shell or an interpreter: Shale is the script; {want = \"tag: why\"} to run it anyway\n"
                                     "`run` of `tclsh`: a shell or an interpreter: Shale is the script; {want = \"tag: why\"} to run it anyway\n"
                                     "`run` of `vim`: an editor or a debugger, which runs any command it is given: `edit(p, old, new)` changes a file; {want = \"tag: why\"} to run it anyway\n"
                                     "`run` of `ed`: an editor or a debugger, which runs any command it is given: `edit(p, old, new)` changes a file; {want = \"tag: why\"} to run it anyway\n"
                                     "`run` of `tac`: a text tool: Shale's builtins and run's own filter and keep do this; {want = \"tag: why\"} to run it anyway\n"
                                     "`run` of `mawk`: a text tool: Shale's builtins and run's own filter and keep do this; {want = \"tag: why\"} to run it anyway\n"
                                     "`run` of `flock`: a wrapper that runs another program: run that program, with run's own options; {want = \"tag: why\"} to run it anyway\n"
                                     "`run` of `unshare`: a wrapper that runs another program: run that program, with run's own options; {want = \"tag: why\"} to run it anyway\n"
                                     "`run` of `script`: a wrapper that runs another program: run that program, with run's own options; {want = \"tag: why\"} to run it anyway\n"
                                     "`run` of `zgrep`, even with a want: `grep_tree(place, needle, {regex, nocase, ext, like, context})` searches a tree; `grep(p, lit)`, `re_lines(read(p), pattern)` or `grep_all(p, lit)` one file; `report_run(argv, {grep = {...}})` or `run`'s `re` a program's output\n"));
    EXPECT_EQ(x.r.host_runs, 0);
    ran_free(&x);
}

// `env = {PATH = ...}` would pick a shell named `git`; LD_PRELOAD loads code
// into any program. Each needs a want, for run and job_start alike.
TEST(an_env_that_picks_the_program_needs_a_want) {
    static Ran x;
    guards_ran_with(&x, "", false);
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\nfunction j(a, o) return host(\"job_start\", a, o) end\n"
                  "for k in {\"PATH\", \"LD_PRELOAD\", \"LD_LIBRARY_PATH\", \"BASH_ENV\", \"ENV\"} do\n"
                  "  local e = {}\n  e[k] = \"decoy\"\n"
                  "  print(attempt(f, {\"true\"}, {timeout_ms = 5000, tail = 1, env = e}).error)\nend\n"
                  "print(attempt(j, {\"true\"}, {log = \"j.log\", env = {PATH = \"decoy\"}}).error)\n"
                  "print(host(\"run\", {\"true\"}, {timeout_ms = 5000, tail = 1, env = {LANG = \"C\"}}).code)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`run`'s `env` PATH changes which program runs, or what it loads before its own code; {want = \"tag: why\"} to set it anyway\n"
                                     "`run`'s `env` LD_PRELOAD changes which program runs, or what it loads before its own code; {want = \"tag: why\"} to set it anyway\n"
                                     "`run`'s `env` LD_LIBRARY_PATH changes which program runs, or what it loads before its own code; {want = \"tag: why\"} to set it anyway\n"
                                     "`run`'s `env` BASH_ENV changes which program runs, or what it loads before its own code; {want = \"tag: why\"} to set it anyway\n"
                                     "`run`'s `env` ENV changes which program runs, or what it loads before its own code; {want = \"tag: why\"} to set it anyway\n"
                                     "`job_start`'s `env` PATH changes which program runs, or what it loads before its own code; {want = \"tag: why\"} to set it anyway\n"
                                     "0\n"));
    EXPECT_EQ(x.r.host_runs, 1);
    ran_free(&x);
}

// The session's sockets never reach a program: host_env_inherit drops the
// session's D-Bus, the runtime folder, ssh's agent and Claude Code's
// messaging from what is inherited (ZIG_PROGRESS too), keeping the agent
// for an unconfined program alone; a script's `env` setting any of them is
// refused, want or not, for run and job_start alike. The fake ssh, the
// host's unconfined one, says the agent it got and its PATH.
TEST(a_program_gets_no_session_socket_and_no_env_sets_one) {
    static Ran x;
    guards_ran_with(&x, "", false);
    static char *inherited[] = {"PATH=/usr/bin:/bin", "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1/bus", "XDG_RUNTIME_DIR=/run/user/1", "SSH_AUTH_SOCK=/tmp/agent.1", "CLAUDE_CODE_MESSAGING_SOCKET=/tmp/cc", "CLAUDE_CODE_MESSAGING_TOKEN=tok", "ZIG_PROGRESS=3", "HOME=/h", NULL}; // cstr: test literals, environ's shape
    SpawnStrs env = {NULL, 0};
    Str agent = S("");
    HostEnvNames gone = {NULL, 0};
    EXPECT(host_env_inherit(&x.host, inherited, &env, &agent, &gone));
    EXPECT_EQ(env.len, 2);
    EXPECT_STR(at(env, 0).s, S("PATH=/usr/bin:/bin"));
    EXPECT_STR(at(env, 1).s, S("HOME=/h"));
    EXPECT_STR(agent, S("SSH_AUTH_SOCK=/tmp/agent.1"));
    EXPECT_EQ(gone.len, 5);
    x.lh.env = env;
    x.lh.agent = agent;
    guards_zig_fake(&x, "fakebin/ssh", "#!/bin/sh\necho \"agent=$SSH_AUTH_SOCK $PATH\"\n");
    guards_unconfined_bin(&x, "fakebin");
    ran_put(&x, "hosts", "deploy@example.com\n");
    FileSystemCPath hosts_cp;
    const char *hosts_c = NULL;
    EXPECT_EQ(file_system_cstr(&hosts_cp, os_str_own(ran_at(&x, "hosts")), &hosts_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_HOSTS", hosts_c, 1), 0);
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\nfunction j(a, o) return host(\"job_start\", a, o) end\n"
                  "print(f({\"printenv\", \"SSH_AUTH_SOCK\", \"DBUS_SESSION_BUS_ADDRESS\", \"XDG_RUNTIME_DIR\", \"CLAUDE_CODE_MESSAGING_TOKEN\"}, {timeout_ms = 5000, tail = 3}).code)\n"
                  "for k in {\"DBUS_SESSION_BUS_ADDRESS\", \"XDG_RUNTIME_DIR\", \"SSH_AUTH_SOCK\", \"CLAUDE_CODE_MESSAGING_TOKEN\"} do\n"
                  "  local e = {}\n  e[k] = \"decoy\"\n"
                  "  print(attempt(f, {\"true\"}, {timeout_ms = 5000, tail = 1, env = e, want = \"t: x\"}).error)\nend\n"
                  "print(attempt(j, {\"true\"}, {log = \"j.log\", env = {XDG_RUNTIME_DIR = \"decoy\"}}).error)\n"
                  "print(f({\"ssh\", \"deploy@example.com\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\"}).out)\n"),
           ran_budgets());
    EXPECT_EQ(unsetenv("SHALE_HOSTS"), 0);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("1\n"
                                     "`run`'s `env` DBUS_SESSION_BUS_ADDRESS names a socket outside the confinement, even with a want: no program Shale starts gets the session's D-Bus, its runtime folder, ssh's agent or Claude Code's messaging\n"
                                     "`run`'s `env` XDG_RUNTIME_DIR names a socket outside the confinement, even with a want: no program Shale starts gets the session's D-Bus, its runtime folder, ssh's agent or Claude Code's messaging\n"
                                     "`run`'s `env` SSH_AUTH_SOCK names a socket outside the confinement, even with a want: no program Shale starts gets the session's D-Bus, its runtime folder, ssh's agent or Claude Code's messaging\n"
                                     "`run`'s `env` CLAUDE_CODE_MESSAGING_TOKEN names a socket outside the confinement, even with a want: no program Shale starts gets the session's D-Bus, its runtime folder, ssh's agent or Claude Code's messaging\n"
                                     "`job_start`'s `env` XDG_RUNTIME_DIR names a socket outside the confinement, even with a want: no program Shale starts gets the session's D-Bus, its runtime folder, ssh's agent or Claude Code's messaging\n"
                                     "agent=/tmp/agent.1 /usr/bin:/bin\n\n"));
    ran_free(&x);
}

// An unconfined program is the system's own, by its path, never one PATH
// finds: with a decoy `ssh` first on PATH that leaves a mark outside every
// mount (which only an unconfined one could write), the host's ssh runs,
// with the system's PATH, and the mark is never made; a program named by a
// path is refused. (The push test holds git and git's own ssh the same way.)
TEST(an_unconfined_program_is_the_system_s_never_one_planted_on_path) {
    static Ran x;
    guards_ran_with(&x, "", false);
    guards_zig_fake(&x, "fakebin/ssh", "#!/bin/sh\necho \"system ssh $* PATH=$PATH\"\n");
    guards_zig_fake(&x, "decoy/ssh", "");
    ran_write(&x, "decoy/ssh", ran_d(&x, "#!/bin/sh\necho planted > D/planted-ssh 2>/dev/null\necho planted\n"));
    static OsStr path_env[1];
    SLICE_TYPE(EnvRoom, OsStr);
    EnvRoom room = {path_env, 1};
    Buf pb = buf_make(&x.host);
    EXPECT_EQ(buf_lit(&pb, "PATH="), OK);
    EXPECT_EQ(buf_str(&pb, ran_at(&x, "decoy")), OK);
    EXPECT_EQ(buf_lit(&pb, ":/usr/bin:/bin"), OK);
    at(room, 0) = os_str_own(buf_view(&pb));
    x.lh.env = (SpawnStrs){path_env, 1};
    guards_unconfined_bin(&x, "fakebin");
    ran_put(&x, "hosts", "deploy@example.com\n");
    FileSystemCPath hosts_cp;
    const char *hosts_c = NULL;
    EXPECT_EQ(file_system_cstr(&hosts_cp, os_str_own(ran_at(&x, "hosts")), &hosts_c), FILE_SYSTEM_OK);
    EXPECT_EQ(setenv("SHALE_HOSTS", hosts_c, 1), 0);
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\n"
                  "local o = {timeout_ms = 10000, tail = 3, unconfined = \"t: x\"}\n"
                  "print(f({\"ssh\", \"deploy@example.com\", \"true\"}, o).out)\n"
                  "print(attempt(f, {\"./ssh\", \"deploy@example.com\", \"true\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\", want = \"t: a path\"}).error)\n"),
           ran_budgets());
    EXPECT_EQ(unsetenv("SHALE_HOSTS"), 0);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("system ssh deploy@example.com true PATH=/usr/bin:/bin\n\n"
                                     "`run`'s `unconfined` names its program bare (`git`, `ssh`, `scp` or `rsync`), not ./ssh: it runs the system's own, by its path\n"));
    EXPECT_STR(guards_decoy(&x, "planted-ssh"), S("absent"));
    ran_free(&x);
}

// A program runs confined to the mounts (host_confine.c): it writes only under
// the root and the writable mounts, reads nothing outside them and the
// system's folders, and a link out of the root reaches nothing. Each refusal
// is aimed at a decoy under the test's own folder, outside every mount: a
// read-only `tools` mount, a folder no mount names, a decoy home's
// `.local/bin`, and a job's program as well as `run`'s. What a script sees is
// the program's own EACCES.
TEST(a_program_writes_and_reads_only_where_the_mounts_allow) {
    static Ran x;
    guards_ran_with(&x, "tools = D/tools ro tools\nout = D/out\n", false);
    ran_put(&x, "root/a.txt", "a\n");
    ran_put(&x, "tools/t.txt", "t\n");
    ran_put(&x, "out/.keep", "");
    ran_put(&x, "decoy/keep.txt", "keep\n");
    ran_put(&x, "home/.local/bin/.keep", "");
    FileSystemCPath to_cp;
    FileSystemCPath link_cp;
    const char *to_c = NULL;
    const char *link_c = NULL;
    EXPECT_EQ(file_system_cstr(&to_cp, os_str_own(ran_at(&x, "decoy")), &to_c), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_cstr(&link_cp, os_str_own(ran_at(&x, "root/out_link")), &link_c), FILE_SYSTEM_OK);
    EXPECT_EQ(symlink(to_c, link_c), 0);
    char exe[PATH_MAX];
    EXPECT(realpath("c/out/tcc/shale", exe) != NULL);
    x.lh.reaper = ran_cs(exe);
    ran_run(&x, ran_d(&x, "function c(a) return host(\"run\", a, {timeout_ms = 5000, tail = 3}) end\n"
                          "print(c({\"cp\", \"a.txt\", \"D/tools/new.txt\"}).out)\n"
                          "print(c({\"cp\", \"a.txt\", \"D/decoy/new.txt\"}).code)\n"
                          "print(c({\"truncate\", \"-s\", \"0\", \"D/decoy/keep.txt\"}).code)\n"
                          "print(c({\"cp\", \"D/decoy/keep.txt\", \"copy.txt\"}).out)\n"
                          "print(c({\"cp\", \"out_link/keep.txt\", \"copy.txt\"}).code)\n"
                          "print(c({\"cp\", \"a.txt\", \"out_link/new.txt\"}).code)\n"
                          "print(host(\"run\", {\"cp\", \"a.txt\", \"D/home/.local/bin/git\"}, {timeout_ms = 5000, tail = 3, env = {HOME = \"D/home\"}}).code)\n"
                          "local j = host(\"job_start\", {\"cp\", \"a.txt\", \"D/decoy/job.txt\"}, {log = \"j.log\"})\n"
                          "print(host(\"job_wait\", j, {timeout_ms = 10000}).code)\n"
                          "print(c({\"cp\", \"D/tools/t.txt\", \"D/out/t.txt\"}).code)\n"
                          "print(c({\"mkdir\", \"sub\"}).code)\n"
                          "print(c({\"mv\", \"a.txt\", \"sub/a.txt\"}).code)\n"
                          "print(c({\"ln\", \"sub/a.txt\", \"b.txt\"}).code)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), ran_d(&x, "cp: cannot create regular file 'D/tools/new.txt': Permission denied\n\n"
                                             "1\n1\n"
                                             "cp: cannot open 'D/decoy/keep.txt' for reading: Permission denied\n\n"
                                             "1\n1\n1\n1\n"
                                             "0\n0\n0\n0\n"));
    // Every decoy is as it was; what the mounts allow was done.
    EXPECT_STR(guards_decoy(&x, "tools/new.txt"), S("absent"));
    EXPECT_STR(guards_decoy(&x, "decoy/new.txt"), S("absent"));
    EXPECT_STR(guards_decoy(&x, "decoy/keep.txt"), S("keep\n"));
    EXPECT_STR(guards_decoy(&x, "decoy/job.txt"), S("absent"));
    EXPECT_STR(guards_decoy(&x, "home/.local/bin/git"), S("absent"));
    EXPECT_STR(guards_decoy(&x, "root/copy.txt"), S("absent"));
    EXPECT_STR(guards_decoy(&x, "out/t.txt"), S("t\n"));
    EXPECT_STR(guards_decoy(&x, "root/sub/a.txt"), S("a\n"));
    // A hard link into another folder, which no program falls back from as
    // `mv` and git do (to a copy), needs Landlock's REFER.
    EXPECT_STR(guards_decoy(&x, "root/b.txt"), S("a\n"));
    ran_free(&x);
}

// What the confinement allows still works: git makes a repository in a mount
// and commits to it (its objects moved between folders, Landlock's REFER),
// a build writes its out folder, and `mktemp` makes a file in the run's own
// temporary folder, its TMPDIR.
TEST(git_commits_in_a_mount_and_a_build_writes_its_out_folder_confined) {
    static Ran x;
    guards_ran_with(&x, "work = D/work\n", false);
    ran_put(&x, "work/r/a.c", "int f(void) { return 1; }\n");
    ran_run(&x, S("function c(a) return host(\"run\", a, {timeout_ms = 10000, tail = 3, cwd = \"work:r\"}) end\n"
                  "print(c({\"git\", \"init\", \"-q\"}).code)\n"
                  "print(c({\"git\", \"add\", \"a.c\"}).code)\n"
                  "print(c({\"git\", \"-c\", \"user.name=t\", \"-c\", \"user.email=t@t\", \"commit\", \"-q\", \"-m\", \"m\"}).code)\n"
                  "print(c({\"git\", \"rev-list\", \"--count\", \"HEAD\"}).out)\n"
                  "mkdir(\"work:r/out\")\n"
                  "print(c({\"tcc\", \"-c\", \"a.c\", \"-o\", \"out/a.o\"}).code)\n"
                  "print(exists(\"work:r/out/a.o\"))\n"
                  "print(c({\"mktemp\"}).code)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("0\n0\n0\n1\n\n0\ntrue\n0\n"));
    ran_free(&x);
}

TEST_PART(run_guards, TEST_CASE(a_program_writes_and_reads_only_where_the_mounts_allow), TEST_CASE(git_commits_in_a_mount_and_a_build_writes_its_out_folder_confined),
          TEST_CASE(an_env_that_picks_the_program_needs_a_want), TEST_CASE(a_refused_program_by_another_name_is_refused_by_the_one_it_reaches),
          TEST_CASE(each_kind_of_refused_program_is_refused_by_its_other_names), TEST_CASE(copy_mode_gives_a_copy_its_source_s_bits_and_a_read_only_mount_refuses_it),
          TEST_CASE(a_nul_in_an_argument_an_env_a_secrets_name_or_a_secret_is_refused_before_any_guard),
          TEST_CASE(a_git_that_cannot_start_is_said_not_taken_for_a_program_it_does_not_track),
          TEST_CASE(a_program_by_a_secret_s_path_is_the_secrets_guard_s_before_git_is_asked),
          TEST_CASE(a_log_is_a_change_a_read_only_mount_refuses_it_and_an_undo_puts_it_back),
          TEST_CASE(a_log_or_a_secret_at_a_fifo_is_refused_not_waited_on),
          TEST_CASE(stdout_secret_s_file_is_held_to_one_byte_past_its_cap_while_the_program_runs),
          TEST_CASE(job_wait_names_the_script_s_path_never_the_os_path),
          TEST_CASE(zig_ast_s_stderr_is_drained_while_it_answers_and_a_hung_one_stops_at_the_deadline), TEST_CASE(zig_ast_runs_confined_to_the_mounts),
          TEST_CASE(ssh_reads_its_known_hosts_and_no_key_and_the_caches_are_the_build_mount_s), TEST_CASE(an_unconfined_run_is_a_git_push_or_a_deploy_to_the_prod_host_alone), TEST_CASE(an_unconfined_copy_s_hosts_are_the_hosts_file_s_and_none_without_one),
          TEST_CASE(an_unconfined_deploy_s_host_may_be_a_secret_and_is_never_shown),
          TEST_CASE(an_unconfined_push_reaches_the_remotes_file_s_remotes_alone),
          TEST_CASE(an_unconfined_fetch_comes_from_the_remotes_file_s_remotes_alone),
          TEST_CASE(an_unconfined_ls_remote_asks_the_remotes_file_s_remotes_for_their_head_alone), TEST_CASE(gh_reads_no_configuration_and_never_says_its_token),
          TEST_CASE(a_program_gets_no_session_socket_and_no_env_sets_one), TEST_CASE(an_unconfined_program_is_the_system_s_never_one_planted_on_path));
