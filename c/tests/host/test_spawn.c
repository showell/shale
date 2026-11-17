// spawn's tests: Zig Shale's, on the same fixture verbs with the same
// expectations, then the attacks on the process boundary spawn guards
// against: a child that ignores TERM, a grandchild that must end with
// the group, at the deadline or when its leader exits, a deadline met to the
// millisecond, output past any keep, and a TERM or HUP sent to the parent
// while a program runs or after it has exited, while its output drains.
//
// The fixture is c/tests/host/fixtures/spawn_fixture.c, built by c/build.shale
// at the path SPAWN_FIXTURE names, relative to the repository's root, where
// the tests run.
#define _GNU_SOURCE
#include "test.h"

#include <fcntl.h>

#include "../../host/keep.c"
#include "../../host/spawn.c"

#ifndef SPAWN_FIXTURE
#error "SPAWN_FIXTURE names the fixture's path, relative to the root: -DSPAWN_FIXTURE='\"c/out/...\"'"
#endif

// A sink that collects everything, in its arena.
typedef struct {
    Buf out;
    // When set, the first write sends this signal to this process (Zig's
    // `Interrupting`): a TERM to `shale` from outside.
    int raise_signal;
    // When set, the first write sleeps this long before it signals: long
    // enough that the program, which exits at once, has exited by then.
    long raise_after_ms;
    bool raised;
    // When set, every write refuses, as the keep does at the host's cap,
    // noting the program's pid from `pid-sleep`'s line first.
    bool refuse;
    long pid;
} Collect;

static STATUS_MUST_USE Status collect_write(void *ctx, Str bytes) {
    Collect *c = ctx;
    if (c->raise_signal != 0 && !c->raised) {
        c->raised = true;
        struct timespec ts = {c->raise_after_ms / 1000, (c->raise_after_ms % 1000) * 1000000L};
        if (c->raise_after_ms > 0) nanosleep(&ts, NULL);
        raise(c->raise_signal);
    }
    if (c->refuse) {
        if (str_starts_with(bytes, S("pid "))) c->pid = strtol(str_sub(bytes, 4, bytes.len).ptr, NULL, 10);
        return STOP;
    }
    return buf_str(&c->out, bytes);
}

static SpawnSink sink_of(Collect *c) {
    SpawnSink s = {c, collect_write};
    return s;
}

// The fixture's absolute path, since a relative argv[0] is taken against
// `cwd`.
static Str fixture_path(Arena *a) {
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd) == NULL) abort();
    Buf b = buf_make(a);
    if (buf_str(&b, (Str){cwd, strlen(cwd)}) != OK || buf_lit(&b, "/" SPAWN_FIXTURE) != OK) abort();
    return buf_view(&b);
}

VEC_DEFINE(ArgList, Str);

// A spec running the fixture with `verb` and up to two arguments ("" none).
static void fixture_spec(SpawnSpec *s, Arena *a, ArgList *argv, const char *verb, const char *x, const char *y) { // cstr: literals
    *argv = ArgList_make(a);
    if (ArgList_push(argv, fixture_path(a)) != OK) abort();
    if (ArgList_push(argv, (Str){verb, strlen(verb)}) != OK) abort();
    if (strlen(x) > 0 && ArgList_push(argv, (Str){x, strlen(x)}) != OK) abort();
    if (strlen(y) > 0 && ArgList_push(argv, (Str){y, strlen(y)}) != OK) abort();
    spawn_spec_default(s);
    s->argv.ptr = argv->ptr;
    s->argv.len = argv->len;
    static const Str env_all[1] = {S("PATH=/usr/bin:/bin")};
    s->env.ptr = env_all;
    s->env.len = 1;
    s->timeout_ms = 10000;
}

static void collect_make(Collect *c, Arena *a) {
    memset(c, 0, sizeof *c);
    c->out = buf_make(a);
}

// Whether `pid` is gone, or a zombie no one has reaped yet: either way it
// runs no more. Waits up to `ms` for it.
static bool ended_within(long pid, int ms) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/stat", pid);
    for (int waited = 0; waited <= ms; waited += 10) {
        int fd = open(path, O_RDONLY);
        if (fd < 0) return true;
        char st[512];
        ssize_t n = read(fd, st, sizeof st - 1);
        close(fd);
        if (n > 0) {
            Str text = {st, (size_t)n};
            size_t close_at = text.len;
            while (close_at > 0 && at(text, close_at - 1) != ')') close_at--;
            if (close_at + 1 < text.len && at(text, close_at + 1) == 'Z') return true;
        }
        struct timespec ts = {0, 10000000L};
        nanosleep(&ts, NULL);
    }
    return false;
}

// The pid in a `grandchild N` line of `out`, or 0.
static long grandchild_pid(Str out) {
    for (size_t i = 0; i + 11 <= out.len; i++) {
        if (str_eq(str_sub(out, i, i + 11), S("grandchild "))) return strtol(str_sub(out, i + 11, out.len).ptr, NULL, 10);
    }
    return 0;
}

TEST(output_exit_code_stdin_cwd_and_the_environment_as_given) {
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    SpawnOutcome o;
    static const Str env_all[2] = {S("SHALE_FIXTURE=present"), S("PATH=/usr/bin:/bin")};
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "lines", "3", "");
    s.env.ptr = env_all;
    s.env.len = 2;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_EXIT);
    EXPECT(o.has_code);
    EXPECT_EQ(o.code, 0);
    EXPECT(!o.has_signal);
    EXPECT_STR(buf_view(&c.out), S("line 1\nline 2\nline 3\n"));
    buf_clear(&c.out);
    fixture_spec(&s, &a, &argv, "exit", "7", "");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.code, 7);
    EXPECT_STR(buf_view(&c.out), S(""));
    fixture_spec(&s, &a, &argv, "cat", "", "");
    s.has_stdin = true;
    s.stdin_bytes = S("fed\nin\n");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_STR(buf_view(&c.out), S("fed\nin\n"));
    buf_clear(&c.out);
    // stdin unset is /dev/null: `cat` ends at once.
    fixture_spec(&s, &a, &argv, "cat", "", "");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.code, 0);
    EXPECT_STR(buf_view(&c.out), S(""));
    // An empty stdin is closed before the loop.
    fixture_spec(&s, &a, &argv, "cat", "", "");
    s.has_stdin = true;
    s.stdin_bytes = S("");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_STR(buf_view(&c.out), S(""));
    fixture_spec(&s, &a, &argv, "env", "SHALE_FIXTURE", "");
    s.env.ptr = env_all;
    s.env.len = 2;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_STR(buf_view(&c.out), S("present\n"));
    buf_clear(&c.out);
    // The environment is the whole of it: nothing of the test's own leaks in.
    fixture_spec(&s, &a, &argv, "env", "HOME", "");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_STR(buf_view(&c.out), S("(unset)\n"));
    buf_clear(&c.out);
    fixture_spec(&s, &a, &argv, "pwd", "", "");
    s.has_cwd = true;
    s.cwd = S("/");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_STR(buf_view(&c.out), S("/\n"));
    buf_clear(&c.out);
    // stderr is the same stream.
    fixture_spec(&s, &a, &argv, "stderr", "on two", "");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_STR(buf_view(&c.out), S("on two\n"));
    arena_deinit(&a);
}

TEST(a_deadline_ends_it_a_signal_of_its_own_is_said_and_not_found_is_an_answer) {
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "sleep", "20000", "");
    s.timeout_ms = 200;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_TIMEOUT);
    EXPECT(o.has_signal);
    EXPECT_EQ(o.signal, SIGTERM);
    EXPECT(!o.has_code);
    EXPECT(o.ms >= 200 && o.ms < 5000);
    // The attack: a child that ignores TERM is KILLed at the grace's end.
    fixture_spec(&s, &a, &argv, "ignore-term", "20000", "");
    s.timeout_ms = 300;
    s.grace_ms = 300;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_TIMEOUT);
    EXPECT(o.has_signal);
    EXPECT_EQ(o.signal, SIGKILL);
    EXPECT(o.ms >= 600 && o.ms < 5000);
    EXPECT_STR(buf_view(&c.out), S("ignoring\n"));
    fixture_spec(&s, &a, &argv, "kill-self", "", "");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_SIGNAL);
    EXPECT(o.has_signal);
    EXPECT_EQ(o.signal, SIGKILL);
    EXPECT(!o.has_code);
    static const Str missing[1] = {S("shale-no-such-program")};
    s.argv.ptr = missing;
    s.argv.len = 1;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_NOT_FOUND);
    static const Str relative[1] = {S("./not-here")};
    s.argv.ptr = relative;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_NOT_FOUND);
    // A folder is there but is no program.
    static const Str folder[1] = {S("/tmp")};
    s.argv.ptr = folder;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_NOT_RUNNABLE);
    fixture_spec(&s, &a, &argv, "pwd", "", "");
    s.has_cwd = true;
    s.cwd = S("/no/such/folder");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_NO_CWD);
    // Found along PATH: a program with no `/` in its name, past a folder
    // that is not there.
    buf_clear(&c.out);
    Str full = fixture_path(&a);
    size_t slash = full.len;
    while (at(full, slash - 1) != '/') slash--;
    Buf path_env = buf_make(&a);
    EXPECT(buf_lit(&path_env, "PATH=/nonexistent:") == OK);
    EXPECT(buf_str(&path_env, str_sub(full, 0, slash - 1)) == OK);
    Str env_one[1] = {buf_view(&path_env)};
    Str by_name[3] = {str_sub(full, slash, full.len), S("lines"), S("1")};
    spawn_spec_default(&s);
    s.argv.ptr = by_name;
    s.argv.len = 3;
    s.env.ptr = env_one;
    s.env.len = 1;
    s.timeout_ms = 10000;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_STR(buf_view(&c.out), S("line 1\n"));
    arena_deinit(&a);
}

TEST(cpu_ms_counts_what_a_program_burns_a_child_waited_for_included_and_none_it_sleeps) {
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "burn", "300", "");
    s.timeout_ms = 30000;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.code, 0);
    EXPECT(o.cpu_ms >= 290);
    fixture_spec(&s, &a, &argv, "burn-child", "300", "");
    s.timeout_ms = 30000;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.code, 0);
    EXPECT(o.cpu_ms >= 290);
    fixture_spec(&s, &a, &argv, "sleep", "300", "");
    s.timeout_ms = 30000;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT(o.ms >= 300);
    EXPECT(o.cpu_ms < 100);
    arena_deinit(&a);
}

TEST(much_output_past_the_pipes_buffer_all_of_it_read) {
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "spill", "1000000", "");
    s.timeout_ms = 30000;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.code, 0);
    EXPECT_EQ(c.out.len, 1000000);
    arena_deinit(&a);
}

static bool term_blocked(void) {
    sigset_t now;
    sigprocmask(SIG_BLOCK, NULL, &now);
    return sigismember(&now, SIGTERM) == 1;
}

TEST(a_term_to_shale_while_a_program_runs_ends_its_group_and_says_so_and_the_mask_is_put_back) {
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    c.raise_signal = SIGTERM;
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "ignore-term", "20000", "");
    s.timeout_ms = 30000;
    s.grace_ms = 300;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT(c.raised);
    EXPECT_EQ(o.ended, SPAWN_INTERRUPTED);
    EXPECT_EQ(o.signal, SIGKILL);
    EXPECT(o.ms < 5000);
    // The TERM was taken by the loop, and TERM is unblocked again.
    EXPECT(!term_blocked());
    arena_deinit(&a);
}

TEST(a_sink_that_stops_ends_the_programs_group_and_reaps_it_before_the_stop_is_said) {
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    c.refuse = true;
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "pid-sleep", "20000", "");
    s.timeout_ms = 30000;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_SINK_STOPPED);
    EXPECT(c.pid > 0);
    // Neither running nor a zombie: gone, reaped by the run itself.
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/stat", c.pid);
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        kill((pid_t)c.pid, SIGKILL);
    }
    EXPECT(fd < 0);
    EXPECT(!term_blocked());
    arena_deinit(&a);
}

TEST(attack_a_grandchild_that_ignores_term_ends_with_the_group_at_the_deadline) {
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "grandchild-stay", "20000", "");
    s.timeout_ms = 300;
    s.grace_ms = 300;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_TIMEOUT);
    EXPECT_EQ(o.signal, SIGKILL);
    long g = grandchild_pid(buf_view(&c.out));
    EXPECT(g > 0);
    // The group's KILL reached the grandchild too: it runs no more.
    bool gone = ended_within(g, 2000);
    if (!gone) kill((pid_t)g, SIGKILL);
    EXPECT(gone);
    arena_deinit(&a);
}

TEST(attack_a_grandchild_holding_the_pipe_after_its_leader_exits_is_ended_before_run_returns) {
    // The leader exits 0 at once; its grandchild would hold the output open
    // for 3 seconds. As in Zig, the leader's exit sends its group TERM, so
    // the grandchild is gone, reaped and all, by the time `run` returns, and
    // `run` waits neither the 3 seconds nor the grace.
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "grandchild-exit", "3000", "");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_EXIT);
    EXPECT_EQ(o.code, 0);
    EXPECT(o.ms < 1500);
    long g = grandchild_pid(buf_view(&c.out));
    EXPECT(g > 0);
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/stat", g);
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        kill((pid_t)g, SIGKILL);
    }
    EXPECT(fd < 0);
    arena_deinit(&a);
}

TEST(attack_a_process_that_left_the_group_holding_the_pipe_never_holds_run) {
    // The leader exits 0 at once; its child has made a session of its own,
    // out of the group's TERM, and would hold the output open for 3 seconds.
    // `run` returns after the drain, not when the pipe closes; the child
    // outlives it, and the test ends it itself.
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "grandchild-escape", "3000", "");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_EXIT);
    EXPECT_EQ(o.code, 0);
    EXPECT(o.ms >= 200 && o.ms < 1500);
    long g = grandchild_pid(buf_view(&c.out));
    EXPECT(g > 0);
    bool alive = g > 0 && !ended_within(g, 0);
    if (g > 0) kill((pid_t)g, SIGKILL);
    EXPECT(alive);
    arena_deinit(&a);
}

TEST(attack_a_term_to_shale_after_its_program_exits_while_the_output_drains_still_stops_the_run) {
    // The leader exits 0 at once; the sink, at the first output, waits until
    // it has, then sends this process a TERM. The loop reads it once the
    // leader is reaped, as its group ends or its output drains: never dropped.
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    c.raise_signal = SIGTERM;
    c.raise_after_ms = 300;
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "grandchild-exit", "3000", "");
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT(c.raised);
    EXPECT_EQ(o.ended, SPAWN_INTERRUPTED);
    // The leader's own exit is kept beside the word.
    EXPECT(o.has_code);
    EXPECT_EQ(o.code, 0);
    EXPECT(o.ms < 5000);
    long g = grandchild_pid(buf_view(&c.out));
    EXPECT(g > 0);
    bool gone = ended_within(g, 0);
    if (!gone) kill((pid_t)g, SIGKILL);
    EXPECT(gone);
    EXPECT(!term_blocked());
    arena_deinit(&a);
}

TEST(attack_the_deadline_and_the_grace_to_the_millisecond) {
    // The loop's decision at each edge, pure: TERM at the deadline and not a
    // millisecond before, KILL at the grace's end and not before, and the
    // poll's wait never past the nearest edge.
    SpawnStep before;
    spawn_step(false, false, false, 0, false, 1000, 2000, 0, 999, &before);
    EXPECT(!before.term);
    EXPECT(!before.kill);
    EXPECT_EQ(before.wait_ms, 1);
    SpawnStep on;
    spawn_step(false, false, false, 0, false, 1000, 2000, 0, 1000, &on);
    EXPECT(on.term);
    EXPECT(!on.kill);
    EXPECT_EQ(on.wait_ms, 1000);
    SpawnStep grace_before;
    spawn_step(false, false, true, 1000, false, 1000, 2000, 0, 2999, &grace_before);
    EXPECT(!grace_before.term);
    EXPECT(!grace_before.kill);
    EXPECT_EQ(grace_before.wait_ms, 1);
    SpawnStep grace_on;
    spawn_step(false, false, true, 1000, false, 1000, 2000, 0, 3000, &grace_on);
    EXPECT(!grace_on.term);
    EXPECT(grace_on.kill);
    EXPECT_EQ(grace_on.wait_ms, 1000);
    // A grace of 0 KILLs in the same turn as the TERM.
    SpawnStep no_grace;
    spawn_step(false, false, false, 0, false, 1000, 0, 0, 1000, &no_grace);
    EXPECT(no_grace.term);
    EXPECT(no_grace.kill);
    // Once exited with its group done, only the drain bounds the wait;
    // nothing is signalled, though the TERM at the exit (3000) is a grace
    // old.
    SpawnStep drain;
    spawn_step(true, true, true, 3000, false, 1000, 2000, 5150, 5000, &drain);
    EXPECT(!drain.term);
    EXPECT(!drain.kill);
    EXPECT_EQ(drain.wait_ms, 150);
    // Exited, its group TERMed at the exit (3000) and not gone, the drain
    // over (3200): no KILL before the grace's end, and the wait never 0, at
    // most 10 ms to ask again; at the grace's end, the KILL.
    SpawnStep group_before;
    spawn_step(true, false, true, 3000, false, 1000, 2000, 3200, 4999, &group_before);
    EXPECT(!group_before.term);
    EXPECT(!group_before.kill);
    EXPECT_EQ(group_before.wait_ms, 1);
    SpawnStep group_wait;
    spawn_step(true, false, true, 3000, false, 1000, 2000, 3200, 4000, &group_wait);
    EXPECT(!group_wait.kill);
    EXPECT_EQ(group_wait.wait_ms, 10);
    SpawnStep group_on;
    spawn_step(true, false, true, 3000, false, 1000, 2000, 3200, 5000, &group_on);
    EXPECT(!group_on.term);
    EXPECT(group_on.kill);
    EXPECT_EQ(group_on.wait_ms, 10);
    // KILLed, and the drain not over: 10 ms, not the drain's 1300.
    SpawnStep group_killed;
    spawn_step(true, false, true, 3000, true, 1000, 2000, 7300, 6000, &group_killed);
    EXPECT(!group_killed.kill);
    EXPECT_EQ(group_killed.wait_ms, 10);
    SpawnStep far;
    spawn_step(false, false, false, 0, false, 90000, 2000, 0, 0, &far);
    EXPECT_EQ(far.wait_ms, 1000);
    // Live: a deadline of 0 is already past at the first turn.
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    SpawnOutcome o;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "sleep", "20000", "");
    s.timeout_ms = 0;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_TIMEOUT);
    EXPECT_EQ(o.signal, SIGTERM);
    EXPECT(o.ms < 1000);
    // A program that ends well inside its deadline exits, never times out.
    fixture_spec(&s, &a, &argv, "exit", "0", "");
    s.timeout_ms = 5000;
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_EXIT);
    arena_deinit(&a);
}

// A sink into a keep.
static STATUS_MUST_USE Status keep_write(void *ctx, Str bytes) {
    return keep_feed(ctx, bytes);
}

TEST(attack_output_past_any_keep_is_read_whole_and_kept_to_its_bounds) {
    // 200,000 lines of 100 bytes, 20 MB, through a keep of 2 and 3 in a 1 MiB
    // arena: every line is counted and the arena never holds the output.
    Arena a = arena_make(1 << 24);
    ArgList argv;
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "spill-lines", "200000", "");
    s.timeout_ms = 60000;
    Arena held = arena_make(1 << 20);
    KeepOptions ko;
    keep_options_default(&ko);
    ko.head = 2;
    ko.tail = 3;
    ko.width = 10;
    Keep k;
    EXPECT(keep_init(&k, &held, ko) == OK);
    SpawnSink sink = {&k, keep_write};
    SpawnOutcome o;
    EXPECT_EQ(spawn_run(&a, &s, sink, &o), SPAWN_OK);
    EXPECT_EQ(o.code, 0);
    Kept kept;
    EXPECT(keep_finish(&k, &kept) == OK);
    EXPECT_EQ(kept.total, 200000);
    EXPECT_EQ(kept.kept, 5);
    Str x10 = S("xxxxxxxxxx\xe2\x80\xa6\n");
    Buf want = buf_make(&a);
    for (int i = 0; i < 2; i++) EXPECT(buf_str(&want, x10) == OK);
    EXPECT(buf_lit(&want, "\xe2\x80\xa6 199995 lines left out \xe2\x80\xa6\n") == OK);
    for (int i = 0; i < 3; i++) EXPECT(buf_str(&want, x10) == OK);
    EXPECT_STR(kept.out, buf_view(&want));
    EXPECT(!held.hit);
    EXPECT(held.held < 256 * 1024);
    // One line of 2 MB with no newline: read past KEEP_LINE_MOST, kept cut.
    Arena held2 = arena_make(1 << 20);
    Keep k2;
    EXPECT(keep_init(&k2, &held2, ko) == OK);
    SpawnSink sink2 = {&k2, keep_write};
    fixture_spec(&s, &a, &argv, "spill", "2000000", "");
    s.timeout_ms = 60000;
    EXPECT_EQ(spawn_run(&a, &s, sink2, &o), SPAWN_OK);
    Kept one;
    EXPECT(keep_finish(&k2, &one) == OK);
    EXPECT_EQ(one.total, 1);
    EXPECT_STR(one.out, x10);
    EXPECT(!held2.hit);
    arena_deinit(&held);
    arena_deinit(&held2);
    arena_deinit(&a);
}

TEST(attack_a_term_or_hup_the_program_sends_its_parent_mid_run_ends_its_group) {
    // The program itself signals `shale` (its parent), then ignores TERM: the
    // loop takes the signal, TERMs the group, and KILLs it at the grace.
    static const char *sigs_all[2] = {"15", "1"}; // cstr: literals
    SLICE_TYPE(Sigs, const char *);
    Sigs sigs = {sigs_all, 2};
    for (size_t i = 0; i < sigs.len; i++) {
        Arena a = arena_make(1 << 24);
        ArgList argv;
        Collect c;
        collect_make(&c, &a);
        SpawnOutcome o;
        SpawnSpec s;
        fixture_spec(&s, &a, &argv, "signal-parent", at(sigs, i), "20000");
        s.timeout_ms = 30000;
        s.grace_ms = 300;
        EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
        EXPECT_EQ(o.ended, SPAWN_INTERRUPTED);
        EXPECT_EQ(o.signal, SIGKILL);
        EXPECT(o.ms >= 300 && o.ms < 5000);
        EXPECT_STR(buf_view(&c.out), S("signalling\n"));
        EXPECT(!term_blocked());
        arena_deinit(&a);
    }
}

TEST(attack_a_program_that_never_reads_its_stdin_leaves_shale_standing) {
    // A megabyte of stdin to a program that exits without reading it: the
    // write meets a pipe with no reader, which must fail with EPIPE, not
    // kill this process with SIGPIPE.
    spawn_quiet_sigpipe();
    Arena a = arena_make(1 << 24);
    ArgList argv;
    Collect c;
    collect_make(&c, &a);
    SpawnOutcome o;
    static char mb[1 << 20];
    memset(mb, 'y', sizeof mb);
    SpawnSpec s;
    fixture_spec(&s, &a, &argv, "exit", "3", "");
    s.has_stdin = true;
    s.stdin_bytes = (Str){mb, sizeof mb};
    EXPECT_EQ(spawn_run(&a, &s, sink_of(&c), &o), SPAWN_OK);
    EXPECT_EQ(o.ended, SPAWN_EXIT);
    EXPECT_EQ(o.code, 3);
    // The program itself starts with SIGPIPE's default, not the handler.
    struct sigaction now;
    sigaction(SIGPIPE, NULL, &now);
    EXPECT(now.sa_handler == spawn_noop_handler);
    arena_deinit(&a);
}

TEST(the_paths_tried_follow_the_last_PATH) {
    Arena a = arena_make(1 << 20);
    SpawnCStrs p;
    static const Str env_all[2] = {S("PATH=/a"), S("PATH=/b::/c")};
    SpawnStrs env = {env_all, 2};
    // The last PATH wins; an empty folder is the name itself.
    EXPECT(spawn_candidates(&a, S("prog"), env, &p));
    EXPECT_EQ(p.len, 3);
    EXPECT_STR(((Str){at(p, 0), strlen(at(p, 0))}), S("/b/prog"));
    EXPECT_STR(((Str){at(p, 1), strlen(at(p, 1))}), S("prog"));
    EXPECT_STR(((Str){at(p, 2), strlen(at(p, 2))}), S("/c/prog"));
    // A name with a `/` is the one path; no PATH is the default.
    EXPECT(spawn_candidates(&a, S("./x"), env, &p));
    EXPECT_EQ(p.len, 1);
    EXPECT_STR(((Str){at(p, 0), strlen(at(p, 0))}), S("./x"));
    SpawnStrs none = {env_all, 0};
    EXPECT(spawn_candidates(&a, S("x"), none, &p));
    EXPECT_EQ(p.len, 3);
    EXPECT_STR(((Str){at(p, 0), strlen(at(p, 0))}), S("/usr/local/bin/x"));
    EXPECT_STR(((Str){at(p, 2), strlen(at(p, 2))}), S("/bin/x"));
    // The arena refused: an answer of OUT_OF_MEMORY, nothing forked.
    Arena tiny = arena_make(8);
    SpawnSpec s;
    spawn_spec_default(&s);
    static const Str argv_all[1] = {S("x")};
    s.argv.ptr = argv_all;
    s.argv.len = 1;
    SpawnOutcome o;
    Collect c;
    memset(&c, 0, sizeof c);
    EXPECT_EQ(spawn_run(&tiny, &s, sink_of(&c), &o), SPAWN_OUT_OF_MEMORY);
    EXPECT(tiny.hit);
    arena_deinit(&a);
}

TEST_MAIN(TEST_CASE(output_exit_code_stdin_cwd_and_the_environment_as_given),
          TEST_CASE(a_deadline_ends_it_a_signal_of_its_own_is_said_and_not_found_is_an_answer),
          TEST_CASE(cpu_ms_counts_what_a_program_burns_a_child_waited_for_included_and_none_it_sleeps),
          TEST_CASE(much_output_past_the_pipes_buffer_all_of_it_read),
          TEST_CASE(a_term_to_shale_while_a_program_runs_ends_its_group_and_says_so_and_the_mask_is_put_back),
          TEST_CASE(a_sink_that_stops_ends_the_programs_group_and_reaps_it_before_the_stop_is_said),
          TEST_CASE(attack_a_grandchild_that_ignores_term_ends_with_the_group_at_the_deadline),
          TEST_CASE(attack_a_grandchild_holding_the_pipe_after_its_leader_exits_is_ended_before_run_returns),
          TEST_CASE(attack_a_process_that_left_the_group_holding_the_pipe_never_holds_run),
          TEST_CASE(attack_a_term_to_shale_after_its_program_exits_while_the_output_drains_still_stops_the_run),
          TEST_CASE(attack_the_deadline_and_the_grace_to_the_millisecond),
          TEST_CASE(attack_output_past_any_keep_is_read_whole_and_kept_to_its_bounds),
          TEST_CASE(attack_a_term_or_hup_the_program_sends_its_parent_mid_run_ends_its_group),
          TEST_CASE(attack_a_program_that_never_reads_its_stdin_leaves_shale_standing),
          TEST_CASE(the_paths_tried_follow_the_last_PATH))
