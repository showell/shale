// test_host_run.c: the Linux host's `run`, `sleep` and jobs (c/host/
// host_run.c, jobs.c, host_jobs.c), each source run as the C `shale` runs
// it (ran.c), with `host(...)` called bare since ran.c loads no prelude.
// Every message is the one Zig's `shale` gives the same call
// (c/out/cmp.shale held the two side by side).

// A part of ran_tests.c (`TEST_PART`, test.h), which defines _GNU_SOURCE and
// includes test.h and ran.c before it.


static Str host_text_of(Ran *x) {
    return buf_view(&x->r.out);
}

TEST(run_keeps_the_head_and_tail_and_answers_every_field) {
    static Ran x;
    ran_source(&x, S("local r = host(\"run\", {\"seq\", \"1\", \"20\"}, {timeout_ms = 5000, head = 2, tail = 2})\n"
                     "print(r.code, r.ended, r.total_lines, r.matched_lines, r.kept_lines, #r.argv, r.argv[3])\nprint(r.out)\n"),
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(host_text_of(&x), S("0\texit\t20\t20\t4\t3\t20\n1\n2\n… 16 lines left out …\n19\n20\n\n"));
    // 51 bytes of output: what is read, and a record of the program.
    EXPECT_EQ(x.r.read_bytes, 51);
    EXPECT_EQ(x.r.host_runs, 1);
    EXPECT_EQ(x.lh.records.len, 1);
    EXPECT(str_starts_with(at(x.lh.records, 0), S("run seq 1 20 -> exit 0 (")));
    EXPECT_EQ(x.lh.time.runs, 1);
    ran_free(&x);
}

TEST(run_charges_its_output_a_step_per_16_bytes) {
    static Ran a;
    static Ran b;
    ran_source(&a, S("host(\"run\", {\"printf\", \"\"}, {timeout_ms = 5000, whole = 100})\n"), ran_budgets());
    ran_source(&b, S("host(\"run\", {\"printf\", \"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}, {timeout_ms = 5000, whole = 100})\n"), ran_budgets());
    EXPECT_OK_IN(&a.r.ctx, a.ran);
    EXPECT_OK_IN(&b.r.ctx, b.ran);
    // 33 bytes are three steps' worth; the argvs are the same length.
    EXPECT_EQ(b.r.steps - a.r.steps, 3);
    EXPECT_EQ(b.r.read_bytes, 33);
    EXPECT_EQ(a.r.read_bytes, 0);
    ran_free(&a);
    ran_free(&b);
}

TEST(run_refuses_each_bad_call_with_its_message) {
    static Ran x;
    ran_source(&x, S("function f(a, o) return host(\"run\", a, o) end\n"
                     "print(attempt(f, {\"bash\"}, {timeout_ms = 1, tail = 1}).error)\n"
                     "print(attempt(f, {\"grep\", \"x\"}, {timeout_ms = 1, tail = 1, want = \"a: b\"}).class)\n"
                     "print(attempt(f, {\"python3.12\"}, {timeout_ms = 1, tail = 1}).error)\n"
                     "print(attempt(f, {\"./nope.sh\"}, {timeout_ms = 1000, tail = 1}).error)\n"
                     "print(attempt(f, {\"echo\"}, {tail = 1}).error)\n"
                     "print(attempt(f, {\"echo\"}, {timeout_ms = 1}).error)\n"
                     "print(attempt(f, {\"echo\"}, {timeout_ms = 1, whole = 1, tail = 1}).error)\n"
                     "print(attempt(f, {\"no-such-program-x\"}, {timeout_ms = 1000, tail = 1}).error)\n"),
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(host_text_of(&x), S("`run` of `bash`: a shell or an interpreter: Shale is the script; {want = \"tag: why\"} to run it anyway\n"
                                   "refused\n"
                                   "`run` of `python3`: a shell or an interpreter: Shale is the script; {want = \"tag: why\"} to run it anyway\n"
                                   "`run` of `./nope.sh`: a program by path that git does not track (a script just written?); {want = \"tag: why\"} to run it anyway\n"
                                   "`run` needs `timeout_ms`: how long the program may take is the script's to say\n"
                                   "`run` needs a keep: `head`, `tail` (or both), or `whole = N`: what to keep of the output is the script's to say\n"
                                   "`run`'s `whole` keeps everything, and takes no filter or keep (`re`, `not_re`, `nocase`, `before`, `after`, `head`, `tail`)\n"
                                   "`run`: `no-such-program-x` is not found along PATH\n"));
    // Only the program that was found ran.
    EXPECT_EQ(x.r.host_runs, 1);
    EXPECT_EQ(x.lh.records.len, 0);
    ran_free(&x);
}

TEST(a_stream_past_the_arena_budget_stops_on_it_and_the_program_is_ended) {
    static Ran x;
    Budgets b = ran_budgets();
    b.arena_bytes = 4u << 20;
    ran_source(&x, S("host(\"run\", {\"seq\", \"1\", \"2000000\"}, {timeout_ms = 20000, whole = 100000000})\n"), b);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_BUDGET);
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_ARENA);
    EXPECT(str_starts_with(x.r.ctx.stop.message, S("`run`: what it holds of the output is past the arena budget (")));
    EXPECT_EQ(x.lh.records.len, 0);
    ran_free(&x);
}

TEST(a_cpu_budget_stops_naming_the_stage_and_how_the_program_ended) {
    static Ran x;
    ran_source(&x, S("host(\"run\", {\"sh\", \"-c\", \"i=0; while [ $i -lt 30000 ]; do i=$((i+1)); done; echo out; exit 2\"}, {timeout_ms = 20000, tail = 5, cpu_budget_ms = 0, stage = \"busy\", want = \"t: sh\"})\n"), ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_CPU);
    Str m = x.r.ctx.stop.message;
    EXPECT(str_starts_with(m, S("`run`: stage `busy` took ")));
    Str end = S("; it also ended exit 2, its output's end:\nout\n");
    EXPECT(m.len > end.len && str_eq(str_sub(m, m.len - end.len, m.len), end));
    // The program ran to its end and was recorded before the stop.
    EXPECT_EQ(x.lh.records.len, 1);
    Str report = ran_report_of(&x, &x.host, false);
    EXPECT(str_starts_with(report, S("── stopped at stdin:1:1: budget cpu: `run`: stage `busy` took ")));
    ran_free(&x);
}

TEST(the_scrubber_holds_back_a_secret_split_across_reads_and_widens_an_overlap) {
    bool refused = false;
    Arena a;
    arena_init(&a, (size_t)1 << 20, &refused);
    HostSecret all[2] = {{S("K"), S("abc")}, {S("L"), S("bcdef")}};
    HostSecrets secrets = {all, 2};
    Buf whole = buf_make(&a);
    HostOut out;
    memset(&out, 0, sizeof out);
    out.log_fd = -1;
    out.whole = &whole;
    out.whole_max = 1000;
    HostScrub s = {secrets, buf_make(&a), buf_make(&a), &out};
    EXPECT_EQ(host_scrub_write(&s, S("xa")), OK);
    // `a` may begin `abc`: held back.
    EXPECT_STR(buf_view(&whole), S("x"));
    EXPECT_EQ(host_scrub_write(&s, S("bcdefy ab")), OK);
    // `abc` then `bcdef` inside it: one secret's name, none of either raw.
    EXPECT_STR(buf_view(&whole), S("x<secret K>y "));
    EXPECT_EQ(host_scrub_finish(&s), OK);
    EXPECT_STR(buf_view(&whole), S("x<secret K>y ab"));
    EXPECT_EQ(out.bytes, 15);
    arena_deinit(&a);
}

TEST(the_time_line_keeps_the_slowest_three_and_both_ends_of_a_long_argv) {
    bool refused = false;
    Arena a;
    arena_init(&a, (size_t)1 << 20, &refused);
    HostTime t;
    memset(&t, 0, sizeof t);
    host_time_ran(&t, &a, S("git log"), 200);
    host_time_ran(&t, &a, S("zig build test"), 1200);
    host_time_ran(&t, &a, S("ls"), 10);
    host_time_ran(&t, &a, S("true"), 5);
    t.waits = 1;
    t.wait_ms = 40000;
    t.sleep_ms = 300;
    EXPECT(host_time_worth_saying(&t, false));
    Buf w = buf_make(&a);
    EXPECT_EQ(host_time_write(&w, &t), OK);
    EXPECT_STR(buf_view(&w), S("── time: 4 programs, 1.4 s in all; the slowest 1.2 s (2%): zig build test; 0.2 s (0%): git log; 0.0 s (0%): ls; 1 job wait, 40.0 s; slept 0.3 s\n"));
    Str kept = S("");
    Str long_argv = S("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaz");
    EXPECT(host_keep_ends(&a, long_argv, &kept));
    EXPECT_EQ(kept.len, 80);
    EXPECT(str_starts_with(kept, S("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\xe2\x80\xa6")));
    EXPECT_EQ(str_byte(kept, 79), 'z');
    HostTime quiet;
    memset(&quiet, 0, sizeof quiet);
    EXPECT(!host_time_worth_saying(&quiet, true));
    arena_deinit(&a);
}

TEST(sleep_takes_0_to_600000_ms_and_counts_its_time) {
    static Ran x;
    ran_source(&x, S("function f(n) return host(\"sleep\", n) end\nprint(attempt(f, -1).error)\nprint(attempt(f, 600001).error)\nprint(attempt(f, \"1\").error)\nhost(\"sleep\", 20)\n"), ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(host_text_of(&x), S("`sleep` takes 0 to 600000 ms, not -1\n`sleep` takes 0 to 600000 ms, not 600001\n`sleep`'s ms must be an integer, not a string\n"));
    EXPECT_EQ(x.lh.time.sleep_ms, 20);
    EXPECT_EQ(x.r.host_runs, 0);
    ran_free(&x);
}

TEST(a_job_runs_past_its_start_is_waited_for_and_a_stopped_run_ends_the_rest) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    // The reaper is the C `shale` the tier built.
    char exe[PATH_MAX];
    EXPECT(realpath("c/out/tcc/shale", exe) != NULL);
    x.lh.reaper = ran_cs(exe);
    ran_run(&x, S("local j = host(\"job_start\", {\"sh\", \"-c\", \"echo hello; exit 4\"}, {log = \"j.log\", want = \"t: sh\"})\n"
                  "local w = host(\"job_wait\", j, {timeout_ms = 10000})\nprint(j.record, w.ended, w.code)\n"
                  "local k = host(\"job_start\", {\"sleep\", \"30\"}, {log = \"k.log\"})\nprint(host(\"job_alive\", k))\nfail(\"stop\")\n"),
           ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_STR(host_text_of(&x), S("j.log.job\texited\t4\ntrue\n"));
    Str log = S("");
    EXPECT(ran_file(&x, "root/j.log", &log));
    EXPECT_STR(log, S("hello\n"));
    EXPECT_EQ(x.lh.started.len, 2);
    EXPECT_EQ(x.r.host_runs, 2);
    EXPECT_EQ(x.lh.time.waits, 1);
    Buf w = buf_make(&x.host);
    EXPECT_EQ(host_jobs_stop_with_run(&x.lh, &w), OK);
    EXPECT_STR(buf_view(&w), S("── stopped with the run, 1 job it started: `sleep 30` (log k.log)\n"));
    // The job is gone now: a second look stops nothing.
    Buf again = buf_make(&x.host);
    EXPECT_EQ(host_jobs_stop_with_run(&x.lh, &again), OK);
    EXPECT_EQ(again.len, 0);
    ran_free(&x);
}

// A loop polling a job holds the arena flat: each look's record is kept at
// its length, not in a 64 KiB buffer of the limit, which would hold 6.5 MB
// over 100 looks.
TEST(a_poll_of_job_alive_holds_the_arena_flat) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    char exe[PATH_MAX];
    EXPECT(realpath("c/out/tcc/shale", exe) != NULL);
    x.lh.reaper = ran_cs(exe);
    ran_run(&x, S("local k = host(\"job_start\", {\"sleep\", \"30\"}, {log = \"p.log\"})\nprint(host(\"job_alive\", k))\n"
                  "local before = budget().arena_bytes\nlocal i = 0\n"
                  "while i < 100 do\n  i = i + 1\n  if not host(\"job_alive\", k) then fail(\"ended\") end\nend\n"
                  "local held = before - budget().arena_bytes\nprint(held < 100 * 2048)\nprint(host(\"job_stop\", k))\n"),
           ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(host_text_of(&x), S("true\ntrue\ntrue\n"));
    ran_free(&x);
}

TEST(job_calls_refuse_each_bad_call_with_its_message) {
    static Ran x;
    ran_source(&x, S("function s(a, o) return host(\"job_start\", a, o) end\nfunction w(j, o) return host(\"job_wait\", j, o) end\n"
                     "print(attempt(s, {\"echo\"}, {}).error)\nprint(attempt(s, {\"echo\"}, {log = \"x.log\", secrets = {}}).error)\n"
                     "print(attempt(s, {\"echo\"}, {log = \"x.log\"}).error)\n"
                     "print(attempt(w, \"nope\", {timeout_ms = 1}).error)\nprint(attempt(w, \"nope.job\", {timeout_ms = 1}).error)\n"
                     "print(attempt(w, \"a.job\", {timeout_ms = 1, contains = \"\"}).error)\n"),
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(host_text_of(&x), S("`job_start` needs a `log`: a job's output goes there, and its record beside it\n"
                                   "`job_start`: no `secrets` on a job: a detached job's log has nobody to scrub it\n"
                                   "`job_start`: this host has no reaper to start a job under\n"
                                   "`job_wait`: nope is not a job's record (`<log>.job`)\n"
                                   "`job_wait`: no job's record at nope.job\n"
                                   "`job_wait`'s `contains` is \"\": every file holds it\n"));
    EXPECT_EQ(x.r.host_runs, 0);
    EXPECT_EQ(x.lh.started.len, 0);
    ran_free(&x);
}

TEST_PART(host_run, TEST_CASE(run_keeps_the_head_and_tail_and_answers_every_field), TEST_CASE(run_charges_its_output_a_step_per_16_bytes),
          TEST_CASE(run_refuses_each_bad_call_with_its_message), TEST_CASE(a_stream_past_the_arena_budget_stops_on_it_and_the_program_is_ended),
          TEST_CASE(a_cpu_budget_stops_naming_the_stage_and_how_the_program_ended),
          TEST_CASE(the_scrubber_holds_back_a_secret_split_across_reads_and_widens_an_overlap),
          TEST_CASE(the_time_line_keeps_the_slowest_three_and_both_ends_of_a_long_argv), TEST_CASE(sleep_takes_0_to_600000_ms_and_counts_its_time),
          TEST_CASE(a_job_runs_past_its_start_is_waited_for_and_a_stopped_run_ends_the_rest), TEST_CASE(a_poll_of_job_alive_holds_the_arena_flat),
          TEST_CASE(job_calls_refuse_each_bad_call_with_its_message));
