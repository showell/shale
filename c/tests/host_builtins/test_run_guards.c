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

// ssh reads its known hosts and its configuration from a decoy home, and no
// key file there; gh's configuration stays closed; every program gets its
// caches in the `build` mount's cache folder, unless the script's `env`
// says otherwise.
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
                  "print(host(\"run\", {\"printenv\", \"XDG_CACHE_HOME\", \"ZIG_GLOBAL_CACHE_DIR\"}, {timeout_ms = 5000, tail = 3}).out)\n"
                  "print(host(\"run\", {\"printenv\", \"XDG_CACHE_HOME\"}, {timeout_ms = 5000, tail = 3, env = {XDG_CACHE_HOME = \"mine\"}}).out)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), ran_d(&x, "0\n0\n1\n1\nD/b/cache\nD/b/cache\n\nmine\n\n"));
    EXPECT_STR(guards_decoy(&x, "root/kh"), S("known\n"));
    EXPECT_STR(guards_decoy(&x, "root/key"), S("absent"));
    EXPECT_STR(guards_decoy(&x, "root/gh"), S("absent"));
    ran_free(&x);
}

// `unconfined` runs a git push past the mounts, to a bare repository no
// mount names, and a deploy's `ssh`, `scp` and `rsync` to a host of a
// planted hosts file (SHALE_HOSTS),
// and nothing else: another program (one with a `push` among its words
// too), a `-c`, a `--receive-pack`, another host, an ssh option, an scp
// option, a local path outside the mounts, an rsync shell, option or daemon,
// and an `env` are each refused, and the record says the push was
// unconfined. No test reaches any host: `ssh`, `scp` and `rsync` are fakes
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
                  "print(attempt(c, {\"scp\", \"where.txt\", \"other@example.com:/tmp/x\"}, o).error)\n"
                  "print(attempt(c, {\"scp\", \"deploy@example.com:/etc/hosts\", \"../x.txt\"}, o).error)\n"
                  "print(attempt(c, {\"rsync\", \"-e\", \"ssh -o ProxyCommand=x\", \"where.txt\", \"deploy@example.com:/tmp/\"}, o).error)\n"
                  "print(attempt(c, {\"rsync\", \"--rsync-path=x\", \"where.txt\", \"deploy@example.com:/tmp/\"}, o).error)\n"
                  "print(attempt(c, {\"rsync\", \"-avz\", \"where.txt\", \"deploy@example.com::mod\"}, o).error)\n"
                  "print(attempt(c, {\"ssh\", \"deploy@example.com\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\", env = {SSH_AUTH_SOCK = \"x\"}}).error)\n"
                  "print(c({\"ssh\", \"deploy@example.com\", \"true\"}, o).out)\n"
                  "print(c({\"scp\", \"-q\", \"where.txt\", \"deploy@example.com:/tmp/x\"}, o).out)\n"
                  "print(c({\"rsync\", \"-rlt\", \"--delete\", \"--chmod=D755,F644\", \"-e\", \"ssh\", \"where.txt\", \"deploy@example.com:/tmp/\"}, o).out)\n"
                  "print(attempt(c, {\"git\", \"-c\", \"core.sshCommand=x\", \"push\", out, \"master\"}, o).error)\n"
                  "print(attempt(c, {\"git\", \"push\", \"--receive-pack=x\", out, \"master\"}, o).error)\n"
                  "print(attempt(c, {\"git\", \"push\", out, \"master\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\", env = {GIT_SSH_COMMAND = \"x\"}}).error)\n"
                  "print(c({\"git\", \"push\", \"-q\", out, \"master\"}, o).code)\n"),
           ran_budgets());
    EXPECT_EQ(unsetenv("SHALE_HOSTS"), 0);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("0\t0\t0\ntrue\n"
                                     "`run`'s `unconfined` (t: past the mounts) is for a `git push`, or `ssh`, `scp` or `rsync` to a host of the hosts file; every other program runs confined to the mounts\n"
                                     "`run`'s `unconfined` (t: past the mounts) is for a `git push`, or `ssh`, `scp` or `rsync` to a host of the hosts file; every other program runs confined to the mounts\n"
                                     "`run`'s `unconfined` ssh goes only to a host of the hosts file (~/.config/shale/hosts, or SHALE_HOSTS's), not `steve@example.org`\n"
                                     "`run`'s `unconfined` ssh takes no option, not -o: an option could name a program, a config or a hop\n"
                                     "`run`'s `unconfined` ssh takes no option, not -F: an option could name a program, a config or a hop\n"
                                     "`run`'s `unconfined` scp takes -q, -r and -p, not -S: another option could name a program, a config or a hop\n"
                                     "`run`'s `unconfined` scp goes only to a host of the hosts file (~/.config/shale/hosts, or SHALE_HOSTS's), not `other@example.com`\n"
                                     "`run`'s `unconfined` scp: its local path ../x.txt is under neither the root nor a mount\n"
                                     "`run`'s `unconfined` rsync takes `-e ssh` alone of a remote shell: any other is a program it would start\n"
                                     "`run`'s `unconfined` rsync does not take --rsync-path=x: its options are HOST_UNCONFINED_RSYNC_LETTERS' and HOST_UNCONFINED_RSYNC_LONG's (c/host/host_unconfined.c)\n"
                                     "`run`'s `unconfined` rsync takes no rsync daemon's module (`host::module`), not deploy@example.com::mod\n"
                                     "`run`'s `unconfined` ssh takes no `env` (git: but GIT_TERMINAL_PROMPT), not SSH_AUTH_SOCK: a variable could name a program it starts\n"
                                     "fake ssh deploy@example.com true\n\n"
                                     "fake scp -q where.txt deploy@example.com:/tmp/x\n\n"
                                     "fake rsync -rlt --delete --chmod=D755,F644 -e ssh where.txt deploy@example.com:/tmp/\n\n"
                                     "`run`'s `unconfined` push takes no `-c` but `color.ui=never`: a setting could name a program git starts outside the confinement\n"
                                     "`run`'s `unconfined` push takes no `--receive-pack` or `--exec`: each names a program it would start outside the confinement\n"
                                     "`run`'s `unconfined` git takes no `env` (git: but GIT_TERMINAL_PROMPT), not GIT_SSH_COMMAND: a variable could name a program it starts\n"
                                     "0\n"));
    Str ref = S("");
    EXPECT(ran_file(&x, "outside.git/refs/heads/master", &ref));
    EXPECT(x.lh.records.len > 0);
    Buf want = buf_make(&x.host);
    EXPECT_EQ(buf_lit(&want, "run git push -q "), OK);
    EXPECT_EQ(buf_str(&want, ran_at(&x, "outside.git")), OK);
    EXPECT_EQ(buf_lit(&want, " master (unconfined: t: past the mounts) -> exit 0"), OK);
    EXPECT(str_starts_with(at(x.lh.records, x.lh.records.len - 1), buf_view(&want)));
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
    EXPECT_EQ(setenv("SHALE_HOSTS", none_c, 1), 0);
    ran_run(&y, S("host(\"run\", {\"ssh\", \"deploy@example.com\", \"true\"}, {timeout_ms = 10000, tail = 3, unconfined = \"t: x\"})\n"), ran_budgets());
    EXPECT_EQ(y.ran, STOP);
    EXPECT(str_starts_with(y.r.ctx.stop.message, S("`run`'s `unconfined`: the hosts file ")));
    EXPECT_EQ(unsetenv("SHALE_HOSTS"), 0);
    ran_free(&y);
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
          TEST_CASE(ssh_reads_its_known_hosts_and_no_key_and_the_caches_are_the_build_mount_s), TEST_CASE(an_unconfined_run_is_a_git_push_or_a_deploy_to_the_prod_host_alone), TEST_CASE(an_unconfined_copy_s_hosts_are_the_hosts_file_s_and_none_without_one));
