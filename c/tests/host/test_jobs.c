// The jobs' tests (c/host/jobs.c): Zig Shale's jobs tests on the reaper and
// the process boundary. The reaper
// ends what a job's leader left in its group before it writes the job's end
// (TERM at the leader's exit, KILL at the grace), and leaves alone a process
// that left the group; a forged pgid signals nobody; the record's temp file
// follows no link; and no fd past 0 to 2 leaks into the reaper or the job.
//
// The reaper runs as `jobs_reap_main` in a child of the test, not as an
// exec'd `shale --reap`, so a plant in jobs.c is the reaper's too. The job is
// c/tests/host/fixtures/spawn_fixture.c, at the path SPAWN_FIXTURE names.
#define _GNU_SOURCE
#include "test.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "buf.h"

#include "../../host/spawn_argv.c"
#include "../../host/pgroup.c"
#include "../../host/landlock.c"
#include "../../host/spawn_child.c"
#include "../../host/job_record.c"
#include "../../host/jobs.c"

#ifndef SPAWN_FIXTURE
#error "SPAWN_FIXTURE names the fixture's path, relative to the root: -DSPAWN_FIXTURE='\"c/out/...\"'"
#endif

// A folder of the test's own under c/out, the record and the log made in it.
typedef struct {
    char dir[256];
    char record[320];
    char log[320];
} JobsPlace;

static void place_make(JobsPlace *p, const char *name) { // cstr: a literal
    snprintf(p->dir, sizeof p->dir, "c/out/host/jobs_%s.XXXXXX", name);
    if (mkdtemp(p->dir) == NULL) abort();
    snprintf(p->record, sizeof p->record, "%s/j.log.job", p->dir);
    snprintf(p->log, sizeof p->log, "%s/j.log", p->dir);
}

// The place's files and folder removed, once a test has read them.
static void place_free(JobsPlace *p) {
    char victim[400];
    snprintf(victim, sizeof victim, "%s/victim", p->dir);
    unlink(p->record);
    unlink(p->log);
    unlink(victim);
    rmdir(p->dir);
}

// The fixture's absolute path, since the reaper takes the program as given.
static void fixture_abs(char *out, size_t len) { // cstr: a path for the kernel
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd) == NULL) abort();
    snprintf(out, len, "%s/%s", cwd, SPAWN_FIXTURE);
}

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// A file's text, at most 4 KiB, in `room`; "" when it cannot be read.
static Str file_text(const char *path, Bytes room) { // cstr: a path for the kernel
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return S("");
    ssize_t n = read(fd, room.ptr, room.len);
    close(fd);
    if (n <= 0) return S("");
    return (Str){room.ptr, (size_t)n};
}

// The fixture as a job, `verb ms` ("" none), under `jobs_reap_main` in a
// child that holds what `jobs_launch`'s reaper holds: /dev/null as 0 to 2,
// the started pipe as fd 3, and nothing else. Waits for the reaper to end,
// and gives its exit code; `*ms` is how long it took.
static int reap_fixture(JobsPlace *p, const char *verb, const char *ms, uint64_t *took) { // cstr: literals
    char fixture[4400];
    fixture_abs(fixture, sizeof fixture);
    char *args_all[8] = {p->record, p->log, (char *)".", fixture, (char *)"--", fixture, (char *)verb, (char *)ms};
    size_t n = strlen(ms) > 0 ? 8 : 7;
    int started[2] = {-1, -1};
    SLICE_TYPE(Fds, int);
    Fds ends = {started, 2};
    if (pipe2(ends.ptr, O_CLOEXEC) != 0) abort();
    uint64_t began = mono_ms();
    pid_t reaper = fork();
    if (reaper < 0) abort();
    if (reaper == 0) {
        if (dup2(at(ends, 1), JOBS_STARTED_FD) < 0) _exit(90);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull < 0) _exit(91);
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        syscall(SYS_close_range, JOBS_STARTED_FD + 1, ~0u, 0u);
        bool refused = false;
        Arena a;
        arena_init(&a, (size_t)1 << 20, &refused);
        const OsStr env_all[1] = {os_str_own(S("PATH=/usr/bin:/bin"))};
        SpawnStrs env = {env_all, 1};
        _exit(jobs_reap_main(&a, args_all, n, env));
    }
    close(at(ends, 1));
    // The read end stays open until the reaper ends: its byte to a pipe no
    // one holds would end it by SIGPIPE.
    int status = 0;
    while (waitpid(reaper, &status, 0) < 0 && errno == EINTR) {
    }
    close(at(ends, 0));
    *took = mono_ms() - began;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

// The job's record as the reaper last wrote it.
static bool record_of(JobsPlace *p, Bytes room, JobRecord *r) {
    Str text = file_text(p->record, room);
    Bytes mine = {room.ptr, text.len};
    return text.len > 0 && job_record_parse(mine, r);
}

// The pid in a `grandchild N` line of `out`, or 0.
static long grandchild_pid(Str out) {
    for (size_t i = 0; i + 11 <= out.len; i++) {
        if (str_eq(str_sub(out, i, i + 11), S("grandchild "))) return strtol(str_sub(out, i + 11, out.len).ptr, NULL, 10);
    }
    return 0;
}

// Whether `pid` is gone, or a zombie no one has reaped yet, within 500 ms. A
// process still there is KILLed, so a failing test leaves nothing behind.
static bool gone_or_kill(long pid) {
    for (int waited = 0; waited <= 500; waited += 10) {
        JobsStat room;
        Str s = S("");
        if (!jobs_start_of(&room, (int32_t)pid, &s)) return true;
        char path[64];
        snprintf(path, sizeof path, "/proc/%ld/stat", pid);
        char st_bytes[512];
        Str st = file_text(path, (Bytes){st_bytes, sizeof st_bytes});
        size_t close_at = st.len;
        while (close_at > 0 && at(st, close_at - 1) != ')') close_at--;
        if (close_at + 1 < st.len && at(st, close_at + 1) == 'Z') return true;
        jobs_sleep_ms(10);
    }
    kill((pid_t)pid, SIGKILL);
    return false;
}

TEST(attack_what_a_jobs_leader_leaves_in_its_group_is_gone_once_its_end_is_written_by_term_or_by_kill_at_the_grace) {
    char room_bytes[4096];
    Bytes room = {room_bytes, sizeof room_bytes};
    char log_bytes[4096];
    Bytes log_room = {log_bytes, sizeof log_bytes};
    // The leader exits 0 at once; its child would sleep 20 s. The TERM at
    // the leader's exit ends it, so the end comes without the grace.
    JobsPlace p;
    place_make(&p, "term");
    uint64_t took = 0;
    EXPECT_EQ(reap_fixture(&p, "grandchild-exit", "20000", &took), 0);
    EXPECT(took < 1500);
    JobRecord r;
    EXPECT(record_of(&p, room, &r));
    EXPECT(r.has_exit && r.exit == 0 && r.has_ended && !r.has_signal);
    long g = grandchild_pid(file_text(p.log, log_room));
    EXPECT(g > 0);
    EXPECT(gone_or_kill(g));
    // A child that ignores TERM is KILLed at the grace's end, still before
    // the end is written; the leader's own exit is the job's.
    JobsPlace q;
    place_make(&q, "late");
    EXPECT_EQ(reap_fixture(&q, "grandchild-late", "20000", &took), 0);
    EXPECT(took >= 2000 && took < 6000);
    EXPECT(record_of(&q, room, &r));
    EXPECT(r.has_exit && r.exit == 0 && r.has_ended && !r.has_signal);
    Str log = file_text(q.log, log_room);
    // It outlived its leader's exit and the TERM, so it printed `late`.
    EXPECT(log.len >= 5 && str_eq(str_sub(log, log.len - 5, log.len), S("late\n")));
    g = grandchild_pid(log);
    EXPECT(g > 0);
    EXPECT(gone_or_kill(g));
    place_free(&p);
    place_free(&q);
}

TEST(attack_a_process_that_left_the_jobs_group_is_neither_signalled_nor_waited_for) {
    char room_bytes[4096];
    Bytes room = {room_bytes, sizeof room_bytes};
    char log_bytes[4096];
    JobsPlace p;
    place_make(&p, "away");
    uint64_t took = 0;
    EXPECT_EQ(reap_fixture(&p, "grandchild-escape", "20000", &took), 0);
    EXPECT(took < 1500);
    JobRecord r;
    EXPECT(record_of(&p, room, &r));
    EXPECT(r.has_exit && r.exit == 0);
    long g = grandchild_pid(file_text(p.log, (Bytes){log_bytes, sizeof log_bytes}));
    EXPECT(g > 0);
    // It outlives the job, out of its reach; the test ends it.
    EXPECT(!gone_or_kill(g));
    place_free(&p);
}

TEST(the_job_holds_0_1_and_2_alone_no_dev_null_or_log_fd_leaks_into_it) {
    char log_bytes[4096];
    JobsPlace p;
    place_make(&p, "fds");
    uint64_t took = 0;
    EXPECT_EQ(reap_fixture(&p, "fds", "", &took), 0);
    EXPECT_STR(file_text(p.log, (Bytes){log_bytes, sizeof log_bytes}), S("fds 0 1 2\n"));
    place_free(&p);
}

TEST(find_takes_a_path_or_the_path_and_a_job_gone_is_neither_alive_nor_stopped) {
    bool refused = false;
    Arena a;
    arena_init(&a, (size_t)1 << 20, &refused);
    char fixture[4400];
    fixture_abs(fixture, sizeof fixture);
    Str fx = {fixture, strlen(fixture)};
    const OsStr env_all[1] = {os_str_own(S("PATH=/usr/bin:/bin"))};
    SpawnStrs env = {env_all, 1};
    int found = -1;
    OsStr out = os_str_own(S(""));
    OsStr here = os_str_own(S("."));
    EXPECT_EQ(jobs_find(&a, os_str_own(fx), env, here, &found, &out), OK);
    EXPECT_EQ(found, 0);
    EXPECT_STR(out.s, fx);
    EXPECT_EQ(jobs_find(&a, os_str_own(S("no-such-program-x")), env, here, &found, &out), OK);
    EXPECT_EQ(found, ENOENT);
    // A file there that cannot be run is told from one not there, as execve
    // tells them apart.
    EXPECT_EQ(jobs_find(&a, os_str_own(S("c/tests/host/test_jobs.c")), env, here, &found, &out), OK);
    EXPECT_EQ(found, EACCES);
    // A job run to its end: its leader, its group and its reaper are gone.
    JobsPlace p;
    place_make(&p, "gone");
    uint64_t took = 0;
    EXPECT_EQ(reap_fixture(&p, "exit", "3", &took), 0);
    char room_bytes[4096];
    JobRecord r;
    EXPECT(record_of(&p, (Bytes){room_bytes, sizeof room_bytes}, &r));
    EXPECT(job_record_done(r));
    EXPECT_EQ(r.exit, 3);
    EXPECT(!jobs_alive(&r));
    EXPECT(!jobs_reaper_alive(&r));
    EXPECT(!jobs_stop(&r, 100));
    // This process, named as a reaper by its start time, is alive.
    JobsStat own;
    Str start = S("");
    EXPECT(jobs_start_of(&own, (int32_t)getpid(), &start));
    r.reaper = (int32_t)getpid();
    r.reaper_start = start;
    EXPECT(jobs_reaper_alive(&r));
    place_free(&p);
    arena_deinit(&a);
}

TEST(the_reaper_holds_0_1_2_and_its_started_pipe_alone) {
    bool refused = false;
    Arena a;
    arena_init(&a, (size_t)1 << 20, &refused);
    JobsPlace p;
    place_make(&p, "reaper");
    char fixture[4400];
    fixture_abs(fixture, sizeof fixture);
    Str fx = {fixture, strlen(fixture)};
    // The fixture as the reaper: it writes the fds it was handed to the
    // record's path, which is then no record.
    const OsStr argv_all[1] = {os_str_own(S("x"))};
    SpawnStrs argv = {argv_all, 1};
    const OsStr env_all[1] = {os_str_own(S("PATH=/usr/bin:/bin"))};
    SpawnStrs env = {env_all, 1};
    JobRecord r;
    Str record = {p.record, strlen(p.record)};
    Str log = {p.log, strlen(p.log)};
    EXPECT_EQ(jobs_launch(&a, os_str_own(fx), os_str_own(record), os_str_own(log), os_str_own(S(".")), os_str_own(fx), argv, env, -1, &r), JOBS_BAD_RECORD);
    char room_bytes[512];
    EXPECT_STR(file_text(p.record, (Bytes){room_bytes, sizeof room_bytes}), S("fds 0 1 2 3\n"));
    place_free(&p);
    arena_deinit(&a);
}

TEST(a_pgid_below_2_is_refused_before_any_kill_and_a_real_group_is_signalled) {
    // Signal 0 only asks, so the test signals nobody. Unguarded, kill(-1, 0)
    // and kill(0, 0) both succeed.
    EXPECT(!pgroup_signal(1, 0));
    EXPECT(!pgroup_signal(0, 0));
    EXPECT(!pgroup_signal(-7, 0));
    // The test's own group is there to be asked.
    EXPECT(pgroup_signal(getpgrp(), 0));
}

TEST(a_link_planted_at_the_records_temp_file_is_replaced_never_followed) {
    JobsPlace p;
    place_make(&p, "link");
    char temp[400];
    char victim[400];
    snprintf(temp, sizeof temp, "%s.7.tmp", p.record);
    snprintf(victim, sizeof victim, "%s/victim", p.dir);
    int fd = open(victim, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    EXPECT(fd >= 0);
    EXPECT_EQ(write(fd, "keep\n", 5), 5);
    close(fd);
    char abs_victim[4096];
    EXPECT(realpath(victim, abs_victim) != NULL);
    EXPECT_EQ(symlink(abs_victim, temp), 0);
    JobRecord w;
    memset(&w, 0, sizeof w);
    w.pid = 42;
    w.pgid = 42;
    w.start = S("1");
    w.started = 9;
    w.argv = S("x");
    w.reaper = 1;
    w.reaper_start = S("5");
    EXPECT(jobs_write_record(p.record, temp, w));
    char room_bytes[512];
    Bytes room = {room_bytes, sizeof room_bytes};
    // The file the link named is as it was.
    EXPECT_STR(file_text(victim, room), S("keep\n"));
    // The record is a file of its own, the record's text, and the temp gone.
    struct stat st;
    EXPECT_EQ(lstat(p.record, &st), 0);
    EXPECT(S_ISREG(st.st_mode));
    EXPECT(str_starts_with(file_text(p.record, room), S("pid=42\npgid=42\n")));
    EXPECT(lstat(temp, &st) != 0 && errno == ENOENT);
    place_free(&p);
}

TEST_MAIN(TEST_CASE(attack_what_a_jobs_leader_leaves_in_its_group_is_gone_once_its_end_is_written_by_term_or_by_kill_at_the_grace),
          TEST_CASE(attack_a_process_that_left_the_jobs_group_is_neither_signalled_nor_waited_for),
          TEST_CASE(the_job_holds_0_1_and_2_alone_no_dev_null_or_log_fd_leaks_into_it),
          TEST_CASE(find_takes_a_path_or_the_path_and_a_job_gone_is_neither_alive_nor_stopped),
          TEST_CASE(the_reaper_holds_0_1_2_and_its_started_pipe_alone),
          TEST_CASE(a_pgid_below_2_is_refused_before_any_kill_and_a_real_group_is_signalled),
          TEST_CASE(a_link_planted_at_the_records_temp_file_is_replaced_never_followed))
