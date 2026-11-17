// test_host_procs.c: the Linux host's `procs` and `proc_stop`
// (c/host/host_procs.c), each source run as the C `shale` runs it (ran.c),
// with `host(...)` called bare since ran.c loads no prelude: the port of
// src/shale_procs_test.zig. Every message is the one Zig's `shale` gives
// the same call. The processes stopped are this test's own children, a
// `sh` waiting on a pipe only the test writes, so no test signals anything
// but its own.

// A part of ran_tests.c (`TEST_PART`, test.h), which defines _GNU_SOURCE and
// includes test.h and ran.c before it.

#include <stdarg.h>

// A child of the test's, `sh -c <script> sh <extra>...`, blocked on a read
// of its stdin, a pipe whose other end `feed` holds: closing it ends the
// child.
typedef struct {
    pid_t pid;
    int feed;
} ProcsChild;

SLICE_TYPE(ProcsArgs, char *);

// Starts the child and waits for its exec, so its command line is `sh`'s
// when the test reads it. Its argv is made in `a`.
static ProcsChild procs_child(Arena *a, const char *script, ProcsArgs extra) { // cstr: a test's literal
    int in_ends[2] = {-1, -1};
    int exec_ends[2] = {-1, -1};
    SpawnPipe in = {in_ends, 2};
    SpawnPipe execd = {exec_ends, 2};
    if (pipe2(in.ptr, O_CLOEXEC) != 0 || pipe2(execd.ptr, O_CLOEXEC) != 0) abort();
    char **argv_p = ARENA_ARRAY(a, char *, extra.len + 5);
    if (argv_p == NULL) abort();
    ProcsArgs argv = {argv_p, extra.len + 5};
    at(argv, 0) = "sh";
    at(argv, 1) = "-c";
    at(argv, 2) = (char *)script;
    at(argv, 3) = "sh";
    for (size_t i = 0; i < extra.len; i++) at(argv, 4 + i) = at(extra, i);
    at(argv, extra.len + 4) = NULL;
    pid_t pid = fork();
    if (pid < 0) abort();
    if (pid == 0) {
        dup2(at(in, 0), 0);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        signal(SIGTERM, SIG_DFL);
        execv("/bin/sh", argv.ptr);
        _exit(127);
    }
    close(at(in, 0));
    close(at(execd, 1));
    char c = 0;
    // EOF once the exec has closed the child's end.
    while (read(at(execd, 0), &c, 1) > 0) {
    }
    close(at(execd, 0));
    ProcsChild out = {pid, at(in, 1)};
    return out;
}

static ProcsChild procs_waiter(Arena *a) {
    ProcsArgs none = {NULL, 0};
    return procs_child(a, "read x", none);
}

// How the child ended, once it has: its wait status.
static int procs_reap(ProcsChild c) {
    close(c.feed);
    int status = 0;
    if (waitpid(c.pid, &status, 0) != c.pid) abort();
    return status;
}

// Whether the child, its stdin closed, ended as an untouched one does:
// `read` at the pipe's end exits 1, no signal.
static bool procs_ended_untouched(ProcsChild c) {
    int status = procs_reap(c);
    return WIFEXITED(status) && WEXITSTATUS(status) == 1;
}

// Whether the child still runs: not yet waitable.
static bool procs_running(ProcsChild c) {
    int status = 0;
    return waitpid(c.pid, &status, WNOHANG) == 0;
}

// The child's start time as /proc has it, in `a`.
static Str procs_start_of(Arena *a, pid_t pid) {
    ProcsStatRoom room;
    ProcsStat s;
    if (!procs_stat(&room, (int32_t)pid, &s)) abort();
    Str out = S("");
    if (!arena_dup(a, s.start, &out)) abort();
    return out;
}

// `fmt` with its `%`s filled, in `a`, as a source or an expected text.
static Str procs_text(Arena *a, const char *fmt, ...) { // cstr: a test's literal
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) abort();
    char *p = ARENA_ARRAY(a, char, (size_t)n + 1);
    if (p == NULL) abort();
    va_start(ap, fmt);
    vsnprintf(p, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return (Str){p, (size_t)n};
}

TEST(procs_lists_a_process_whole_in_pid_order_and_never_this_one_or_one_above_it) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    ProcsChild c = procs_waiter(&x.host);
    Str start = procs_start_of(&x.host, c.pid);
    // A filter every process passes, as `pkill -f` matching its own shell's
    // command line would: this run and its parent are not there.
    ran_run(&x,
            procs_text(&x.host,
                       "local seen = 0\nlocal mine = 0\nlocal last = 0\nlocal ordered = true\n"
                       "for p in host(\"procs\") do\n"
                       "  seen = seen + 1\n"
                       "  if p.pid <= last then ordered = false end\n"
                       "  last = p.pid\n"
                       "  if p.pid == %d or p.pid == %d then mine = mine + 1 end\n"
                       "  if p.pid == %d then print(p.ppid, p.uid, p.name, p.args, p.start, get(p, \"args_cut\", false), count(p)) end\n"
                       "end\n"
                       "print(seen > 1, mine, ordered)\n",
                       (int)getpid(), (int)getppid(), (int)c.pid),
            ran_budgets());
    EXPECT_EQ(x.ran, OK);
    Str want = procs_text(&x.host, "%d\t%d\tsh\tsh -c read x sh\t%.*s\tfalse\t6\ntrue\t0\ttrue\n", (int)getpid(), (int)getuid(), (int)start.len, start.ptr);
    EXPECT_STR(buf_view(&x.r.out), want);
    // A listing stops nothing and records nothing.
    EXPECT(procs_running(c));
    EXPECT_EQ(x.lh.records.len, 0);
    EXPECT(procs_ended_untouched(c));
    ran_free(&x);
}

TEST(a_command_line_past_one_read_is_read_whole_and_one_past_args_most_says_it_was_cut) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    // Past one read of 4096 bytes: read to its end.
    char *long_p = ARENA_ARRAY(&x.host, char, 6001);
    EXPECT(long_p != NULL);
    memset(long_p, 'y', 6000);
    Bytes long_b = {long_p, 6001};
    at(long_b, 6000) = 0;
    char *one[1] = {long_p};
    ProcsArgs whole_args = {one, 1};
    ProcsChild w = procs_child(&x.host, "read x", whole_args);
    // Past PROCS_ARGS_MOST: nine arguments of 120 KiB.
    char *part = ARENA_ARRAY(&x.host, char, 120 * 1024 + 1);
    EXPECT(part != NULL);
    memset(part, 'x', 120 * 1024);
    Bytes part_b = {part, 120 * 1024 + 1};
    at(part_b, 120 * 1024) = 0;
    char *nine[9] = {part, part, part, part, part, part, part, part, part};
    ProcsArgs cut_args = {nine, 9};
    ProcsChild c = procs_child(&x.host, "read x", cut_args);
    ran_run(&x,
            procs_text(&x.host,
                       "for p in host(\"procs\") do\n"
                       "  if p.pid == %d then print(#p.args, get(p, \"args_cut\", false)) end\n"
                       "  if p.pid == %d then print(#p.args, get(p, \"args_cut\", false)) end\n"
                       "end\n",
                       (int)w.pid, (int)c.pid),
            ran_budgets());
    EXPECT_EQ(x.ran, OK);
    // `sh -c read x sh ` is 16 bytes before the long one.
    EXPECT_STR(buf_view(&x.r.out), S("6016\tfalse\n1048576\ttrue\n"));
    EXPECT(procs_ended_untouched(w));
    EXPECT(procs_ended_untouched(c));
    ran_free(&x);
}

TEST(proc_stop_stops_the_process_by_pid_and_start_and_a_stale_start_is_left_alone) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    ProcsChild c = procs_waiter(&x.host);
    // A start that is not the process's: not `p`, left alone.
    ran_run(&x,
            procs_text(&x.host, "local found = nil\nfor p in host(\"procs\") do if p.pid == %d then found = p end end\n"
                                "print(host(\"proc_stop\", {pid = found.pid, start = \"1\"}))\n",
                       (int)c.pid),
            ran_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("false\n"));
    EXPECT(procs_running(c));
    EXPECT_EQ(x.lh.records.len, 0);
    ran_free(&x);
    // Its own entry: TERM, and it is gone, said in the record.
    static Ran y;
    ran_init(&y, o);
    ran_run(&y,
            procs_text(&y.host, "local found = nil\nfor p in host(\"procs\") do if p.pid == %d then found = p end end\nprint(host(\"proc_stop\", found))\n", (int)c.pid),
            ran_budgets());
    EXPECT_EQ(y.ran, OK);
    EXPECT_STR(buf_view(&y.r.out), S("true\n"));
    EXPECT_EQ(y.lh.records.len, 1);
    EXPECT_STR(at(y.lh.records, 0), procs_text(&y.host, "process %d stopped", (int)c.pid));
    Str start = procs_start_of(&y.host, c.pid);
    int status = procs_reap(c);
    EXPECT(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGTERM);
    ran_free(&y);
    // Reaped, no process holds the pid: false, nothing signalled.
    static Ran z;
    ran_init(&z, o);
    ran_run(&z, procs_text(&z.host, "print(host(\"proc_stop\", {pid = %d, start = \"%.*s\"}))\n", (int)c.pid, (int)start.len, start.ptr), ran_budgets());
    EXPECT_EQ(z.ran, OK);
    EXPECT_STR(buf_view(&z.r.out), S("false\n"));
    EXPECT_EQ(z.lh.records.len, 0);
    ran_free(&z);
}

TEST(proc_stop_refuses_this_process_those_above_it_and_pid_1_whatever_it_is_handed) {
    int pids[5] = {(int)getpid(), (int)getppid(), 1, 0, -5};
    SLICE_TYPE(ProcsPidList, int);
    ProcsPidList all = {pids, 5};
    for (size_t i = 0; i < all.len; i++) {
        static Ran x;
        RanOpts o = {NULL, false, false};
        ran_init(&x, o);
        ran_run(&x, procs_text(&x.host, "host(\"proc_stop\", {pid = %d, start = \"0\"})\n", at(all, i)), ran_budgets());
        EXPECT_EQ(x.ran, STOP);
        EXPECT_EQ(x.r.ctx.stop.cls, CLASS_ARGUMENT);
        EXPECT_STR(x.r.ctx.stop.message, procs_text(&x.host, "`proc_stop`: pid %d is this run or one above it, which a script never stops", at(all, i)));
        EXPECT_EQ(x.lh.records.len, 0);
        ran_free(&x);
    }
}

TEST(procs_and_proc_stop_refuse_what_zig_refuses_with_zigs_words) {
    static Ran x;
    ran_source(&x, S("function p(a) return host(\"procs\", a) end\nfunction s(e) return host(\"proc_stop\", e) end\nfunction none() return host(\"proc_stop\") end\n"
                     "print(attempt(p, 1).error)\nprint(attempt(none).error)\n"
                     "local r = attempt(s, \"x\")\nprint(r.class, r.error)\n"
                     "r = attempt(s, {1})\nprint(r.class, r.error)\n"
                     "r = attempt(s, {})\nprint(r.class, r.error)\n"
                     "r = attempt(s, {start = \"1\"})\nprint(r.class, r.error)\n"
                     "r = attempt(s, {pid = \"1\", start = \"1\"})\nprint(r.class, r.error)\n"
                     "r = attempt(s, {pid = 5})\nprint(r.class, r.error)\n"
                     "r = attempt(s, {pid = 5, start = 1})\nprint(r.class, r.error)\n"
                     "r = attempt(s, {pid = 4294967296, start = \"1\"})\nprint(r.class, r.error)\n"),
               ran_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("`procs` takes nothing, given 1 arguments: filter its list by `args` or `name`\n"
                                     "`proc_stop` takes one entry of `procs()`, given 0 arguments\n"
                                     "type\t`proc_stop` takes an entry of `procs()`, not a string\n"
                                     "type\t`proc_stop` takes an entry of `procs()`, a map, not a list\n"
                                     "type\t`proc_stop` takes an entry of `procs()`, a map, not a list\n"
                                     "argument\t`proc_stop`: the entry has no `pid`: hand it one of `procs()`\n"
                                     "type\t`proc_stop`: the entry's `pid` is not an integer\n"
                                     "argument\t`proc_stop`: the entry has no `start`: hand it one of `procs()`, which names a process by its pid and its start\n"
                                     "type\t`proc_stop`: the entry's `start` is not a string\n"
                                     "argument\t`proc_stop`: pid 4294967296 is no process's\n"));
    EXPECT_EQ(x.lh.records.len, 0);
    ran_free(&x);
}

TEST(a_process_the_run_may_not_signal_stops_proc_stop_named_never_a_success) {
    // Root may signal every process, so there is none to refuse it.
    if (getuid() == 0) return;
    static Ran x;
    // Not root: a process of root's (there is always one beside pid 1)
    // answers EPERM, and the first such stops the run.
    ran_source(&x, S("for p in host(\"procs\") do\n  if p.uid == 0 and p.pid > 1 then\n    print(p.pid)\n    host(\"proc_stop\", p)\n  end\nend\n"), ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_HOST);
    Str out = buf_view(&x.r.out);
    EXPECT(out.len > 1);
    Str pid = str_sub(out, 0, out.len - 1);
    EXPECT_STR(x.r.ctx.stop.message, procs_text(&x.host, "`proc_stop`: pid %.*s refused TERM: PERM", (int)pid.len, pid.ptr));
    EXPECT_EQ(x.lh.records.len, 0);
    ran_free(&x);
}

TEST(attack_a_process_that_ignores_term_gets_kill_after_two_seconds) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    ProcsArgs none = {NULL, 0};
    ProcsChild c = procs_child(&x.host, "trap '' TERM; read x", none);
    uint64_t began = spawn_mono_ms();
    ran_run(&x, procs_text(&x.host, "for p in host(\"procs\") do if p.pid == %d then print(host(\"proc_stop\", p)) end end\n", (int)c.pid), ran_budgets());
    uint64_t took = spawn_mono_ms() - began;
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("true\n"));
    EXPECT(took >= 2000);
    EXPECT_EQ(x.lh.records.len, 1);
    int status = procs_reap(c);
    EXPECT(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGKILL);
    ran_free(&x);
}

TEST_PART(host_procs, TEST_CASE(procs_lists_a_process_whole_in_pid_order_and_never_this_one_or_one_above_it),
          TEST_CASE(a_command_line_past_one_read_is_read_whole_and_one_past_args_most_says_it_was_cut),
          TEST_CASE(proc_stop_stops_the_process_by_pid_and_start_and_a_stale_start_is_left_alone),
          TEST_CASE(proc_stop_refuses_this_process_those_above_it_and_pid_1_whatever_it_is_handed),
          TEST_CASE(procs_and_proc_stop_refuse_what_zig_refuses_with_zigs_words),
          TEST_CASE(a_process_the_run_may_not_signal_stops_proc_stop_named_never_a_success),
          TEST_CASE(attack_a_process_that_ignores_term_gets_kill_after_two_seconds));
