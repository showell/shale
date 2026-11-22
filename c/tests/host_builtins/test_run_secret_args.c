// test_run_secret_args.c: `run`'s `secret_args` (c/host/host_run_secrets.c,
// host_run.c), a secret put into an argument at its marker `<secret NAME>`:
// the program is handed the value, and the script, the record, the answer,
// a stop, the report and the time line see only the marker; each refusal
// comes before any secret is read or program started.

// A part of ran_tests.c (`TEST_PART`, test.h), which defines _GNU_SOURCE and
// includes test.h and ran.c before it.

static const char *const SECRET_ARGS_MOUNT = "k = D/root/keys secrets\n";

// The planted secret's value, which nothing the script or the report sees
// may hold.
static const char *const SECRET_ARGS_VALUE = "deploy@192.0.2.7";

static bool secret_args_holds(Str s, const char *lit) { // cstr: a test's literal
    Str l = ran_cs(lit);
    return mem_find(s.ptr, s.len, l.ptr, l.len) != SIZE_MAX;
}

TEST(a_secret_fills_an_argument_and_only_its_marker_is_ever_shown) {
    static Ran x;
    RanOpts o = {SECRET_ARGS_MOUNT, false, false};
    ran_init(&x, o);
    ran_put(&x, "root/keys/host", "  deploy@192.0.2.7\n");
    // printf proves the value reached the program: the length of each
    // argument it was handed, then the arguments themselves, which the
    // scrubber gives back as the marker. The marker inside an argument
    // (rsync's `HOST:/path`) is filled in place.
    ran_run(&x, S("local r = host(\"run\", {\"printf\", \"%s|%s\\n\", \"<secret H>\", \"<secret H>:/srv/x/\"}, {timeout_ms = 5000, tail = 5, secret_args = {H = \"k:host\"}})\n"
                  "print(r.code, r.out)\nprint(r.argv[3], r.argv[4])\n"
                  "local n = host(\"run\", {\"expr\", \"length\", \"<secret H>:/srv/x/\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:host\"}})\n"
                  "print(n.out)\n"
                  // With no `secret_args`, a marker is the script's own text.
                  "print(host(\"run\", {\"echo\", \"<secret H>\"}, {timeout_ms = 5000, tail = 1}).out)\n"
                  // A stop that names the program and its output: the
                  // stage's name is the argv as the script wrote it.
                  "host(\"run\", {\"sh\", \"-c\", \"i=0; while [ $i -lt 30000 ]; do i=$((i+1)); done; echo \\\"to $1\\\" >&2; exit 2\", \"sh\", \"<secret H>\"}, {timeout_ms = 20000, tail = 5, cpu_budget_ms = 0, want = \"t: sh\", secret_args = {H = \"k:host\"}})\n"),
           ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    Str out = buf_view(&x.r.out);
    // 16 bytes of value, 24 with `:/srv/x/`.
    EXPECT_STR(out, S("0\t<secret H>|<secret H>:/srv/x/\n\n<secret H>\t<secret H>:/srv/x/\n24\n\n<secret H>\n\n"));
    Str stop = x.r.ctx.stop.message;
    EXPECT(str_starts_with(stop, S("`run`: stage `sh -c i=0; while")));
    EXPECT(secret_args_holds(stop, "sh <secret H>` took "));
    EXPECT(secret_args_holds(stop, "its output's end:\nto <secret H>\n"));
    EXPECT(!secret_args_holds(stop, SECRET_ARGS_VALUE));
    // The records name the marker and the secret's path, never the value.
    EXPECT_EQ(x.lh.records.len, 4);
    EXPECT(str_starts_with(at(x.lh.records, 0), S("run printf %s|%s\n <secret H> <secret H>:/srv/x/ argv H=<secret k:host> -> exit 0 (")));
    for (size_t i = 0; i < x.lh.records.len; i++) EXPECT(!secret_args_holds(at(x.lh.records, i), SECRET_ARGS_VALUE));
    Str report = ran_report_of(&x, &x.host, true);
    EXPECT(secret_args_holds(report, "argv H=<secret k:host>"));
    EXPECT(!secret_args_holds(report, SECRET_ARGS_VALUE));
    // The time line names each program by the script's argv.
    Buf w = buf_make(&x.host);
    EXPECT_EQ(host_time_write(&w, &x.lh.time), OK);
    EXPECT(secret_args_holds(buf_view(&w), "<secret H>"));
    EXPECT(!secret_args_holds(buf_view(&w), SECRET_ARGS_VALUE));
    EXPECT(!secret_args_holds(buf_view(&w), "192.0.2.7"));
    ran_free(&x);
}

TEST(each_secret_args_mistake_is_refused_before_a_secret_is_read) {
    static Ran x;
    RanOpts o = {SECRET_ARGS_MOUNT, false, false};
    ran_init(&x, o);
    ran_put(&x, "root/keys/host", "deploy@192.0.2.7\n");
    ran_put(&x, "root/keys/empty", " \n");
    FileSystemCPath link_cp;
    const char *link_c = NULL;
    EXPECT_EQ(file_system_cstr(&link_cp, os_str_own(ran_at(&x, "root/keys/link")), &link_c), FILE_SYSTEM_OK);
    EXPECT_EQ(symlink("host", link_c), 0);
    ran_run(&x, S("function f(a, o) return host(\"run\", a, o) end\nfunction j(a, o) return host(\"job_start\", a, o) end\n"
                  "print(attempt(f, {\"echo\", \"<secret a-b>\"}, {timeout_ms = 5000, tail = 1, secret_args = {[\"a-b\"] = \"k:host\"}}).error)\n"
                  "print(attempt(f, {\"echo\", \"<secret H>\"}, {timeout_ms = 5000, tail = 1, secrets = {H = \"k:host\"}, secret_args = {H = \"k:host\"}}).error)\n"
                  "print(attempt(f, {\"<secret H>\", \"x\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:host\"}}).error)\n"
                  "print(attempt(f, {\"echo\", \"<secret X>\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:host\"}}).error)\n"
                  "print(attempt(f, {\"echo\", \"<secret H\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:host\"}}).error)\n"
                  "print(attempt(f, {\"echo\", \"hi\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:host\"}}).error)\n"
                  "print(attempt(f, {\"echo\", \"<secret H>\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = 3}}).error)\n"
                  "print(attempt(f, {\"echo\", \"<secret H>\"}, {timeout_ms = 5000, tail = 1, secret_args = {\"k:host\"}}).error)\n"
                  "print(attempt(f, {\"echo\", \"<secret H>\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:nope\"}}).error)\n"
                  "print(attempt(f, {\"echo\", \"<secret H>\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:empty\"}}).error)\n"
                  "print(attempt(f, {\"echo\", \"<secret H>\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:link\"}}).error)\n"
                  // The guards still read the script's own argv.
                  "print(attempt(f, {\"echo\", \"<secret H>\", \"keys/host\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:host\"}}).error)\n"
                  "print(attempt(f, {\"bash\", \"<secret H>\"}, {timeout_ms = 5000, tail = 1, secret_args = {H = \"k:host\"}}).error)\n"
                  "print(attempt(j, {\"echo\", \"<secret H>\"}, {log = \"j.log\", secret_args = {H = \"k:host\"}}).error)\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`run`'s `secret_args` name `a-b` is not a word (letters, digits and `_`, not first a digit): its place in an argument is `<secret NAME>`\n"
                                     "`run`: `H` is a name in both `secrets` and `secret_args`: the output would name two secrets as one, `<secret H>`\n"
                                     "`run`'s program is the secret `H`: a secret fills an argument after the program's, which the guards read as the script wrote it\n"
                                     "`run`'s argument 2 holds `<secret ` with no name `secret_args` gives: each `<secret NAME>` in the argv is one of its names\n"
                                     "`run`'s argument 2 holds `<secret ` with no name `secret_args` gives: each `<secret NAME>` in the argv is one of its names\n"
                                     "`run`'s `secret_args` gives `H`, and no argument holds `<secret H>`\n"
                                     "`run`: its path must be a string, not an integer\n"
                                     "`run`: its `secret_args` must be a map, not a list\n"
                                     "`run`: the secret `H`'s file k:nope cannot be read: FileNotFound\n"
                                     "`run`: the secret `H`'s file k:empty is empty\n"
                                     "k:link is a symbolic link: the file system follows none\n"
                                     "`run`'s argument 3 names the mount `k`'s folder, which holds secrets: hand one over as `secrets`\n"
                                     "`run` of `bash`: a shell or an interpreter: Shale is the script; {want = \"tag: why\"} to run it anyway\n"
                                     "`job_start`: no `secret_args` on a job: a detached job's log has nobody to scrub it\n"));
    // Nothing ran, and nothing was recorded.
    EXPECT_EQ(x.r.host_runs, 0);
    EXPECT_EQ(x.lh.records.len, 0);
    EXPECT_EQ(x.lh.started.len, 0);
    ran_free(&x);
}

TEST_PART(run_secret_args, TEST_CASE(a_secret_fills_an_argument_and_only_its_marker_is_ever_shown),
          TEST_CASE(each_secret_args_mistake_is_refused_before_a_secret_is_read));
