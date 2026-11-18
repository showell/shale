// jobs.c: long jobs, programs that outlive the `shale` that started them,
// each under a reaper of its own.
//
// `jobs_launch` double-forks a reaper out of the caller's process tree, so
// stopping the `shale` that waits on a job never kills the job. The first
// child makes its own session, forks the reaper and exits; the reaper,
// re-parented to init, execs `shale --reap`. The reaper (`jobs_reap_main`)
// starts the job in its own session with stdin /dev/null and stdout and
// stderr to its log, writes the job's record, waits for it, ends what it left
// running in its group (TERM, a grace, KILL, as `run` does), and writes how it
// ended. The record, `<log>.job`, is job_record.c's `key=value` lines, each
// write a new file renamed over the old.
//
// Include spawn_argv.c, pgroup.c, spawn_child.c and job_record.c first. The
// including file defines _GNU_SOURCE before its first include, for pipe2.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// The reaper's end of `jobs_launch`'s started pipe.
enum { JOBS_STARTED_FD = 3 };

// Room for a process's /proc stat line, read once.
typedef struct {
    char bytes[1024];
} JobsStat;

// A process's raw start time, field 22 of `/proc/<pid>/stat`, counted after
// the last `)` (a program's name may hold one), into `*out`, which points
// into `room`. False when there is no such process.
static bool jobs_start_of(JobsStat *room, int32_t pid, Str *out) {
    char path[64];
    int n = snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    if (n <= 0) return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t got = read(fd, room->bytes, sizeof room->bytes);
    close(fd);
    if (got < 0) return false;
    Str text = {room->bytes, (size_t)got};
    size_t close_at = text.len;
    while (close_at > 0 && str_byte(text, close_at - 1) != ')') close_at--;
    if (close_at == 0) return false;
    Str rest = str_sub(text, close_at, text.len);
    // Field 3 (the state) is the first after `)`; the start time is 22.
    size_t field = 3;
    size_t i = 0;
    while (i < rest.len) {
        while (i < rest.len && str_byte(rest, i) == ' ') i++;
        if (i == rest.len) break;
        size_t from = i;
        while (i < rest.len && str_byte(rest, i) != ' ') i++;
        if (field == 22) {
            *out = str_sub(rest, from, i);
            return true;
        }
        field++;
    }
    return false;
}

// Whether the reaper the record names is alive: while it is, a job that is
// gone still has its end to be written.
static bool jobs_reaper_alive(const JobRecord *r) {
    if (r->reaper <= 0) return false;
    JobsStat room;
    Str s = S("");
    if (!jobs_start_of(&room, r->reaper, &s)) return false;
    return str_eq(s, r->reaper_start);
}

// How long what a job's leader left in its group has, after the TERM at the
// leader's exit, before it is KILLed (`jobs_stop`'s and `run`'s grace), and
// how often the group is asked whether it is empty, as spawn.c asks.
enum { JOBS_GROUP_GRACE_MS = 2000, JOBS_GROUP_TICK_MS = 10 };

// Whether a job is alive: its leader, while it is there, by the record's
// start time; once it is gone, its group, if a process of it lives on. The
// leader is asked first: it makes its group (`setsid`) only once it runs.
static bool jobs_alive(const JobRecord *r) {
    JobsStat room;
    Str s = S("");
    if (jobs_start_of(&room, r->pid, &s)) return str_eq(s, r->start);
    return pgroup_signal(r->pgid, 0);
}

static void jobs_sleep_ms(uint64_t ms) {
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

// Signals the job's leader only while `r->pid` is still it by its start
// time: a pid the kernel gave to another process since is never signalled.
static void jobs_signal_leader(const JobRecord *r, int sig) {
    JobsStat room;
    Str s = S("");
    if (!jobs_start_of(&room, r->pid, &s)) return;
    if (str_eq(s, r->start)) kill(r->pid, sig);
}

// TERM to a job's group, up to `grace_ms`, then KILL; whether the job was
// alive. The start-time check narrows the race with a reused pid; it does
// not close it.
static bool jobs_stop(const JobRecord *r, uint64_t grace_ms) {
    if (!jobs_alive(r)) return false;
    pgroup_signal(r->pgid, SIGTERM);
    // A leader that has not made its group yet is not reached by it.
    jobs_signal_leader(r, SIGTERM);
    for (uint64_t waited = 0; waited < grace_ms; waited += 50) {
        if (!jobs_alive(r)) return true;
        jobs_sleep_ms(50);
    }
    pgroup_signal(r->pgid, SIGKILL);
    jobs_signal_leader(r, SIGKILL);
    // The reaper reaps the leader; a little wait for it and the group to go.
    for (uint64_t waited = 0; waited < JOBS_GROUP_GRACE_MS && jobs_alive(r); waited += 20) jobs_sleep_ms(20);
    return true;
}

// `dir/name` in the arena, as spawn.c's candidates are made; false when
// refused.
static bool jobs_join(Arena *a, Str dir, Str name, OsStr *out) {
    if (dir.len > SIZE_MAX / 4 || name.len > SIZE_MAX / 4) return arena_refuse(a) != NULL;
    char *p = ARENA_ARRAY(a, char, dir.len + 1 + name.len);
    if (p == NULL) return false;
    Bytes room = {p, dir.len + 1 + name.len};
    if (dir.len > 0) mem_copy(room.ptr, dir.ptr, dir.len);
    at(room, dir.len) = '/';
    Bytes tail = SLICE_SUB(Bytes, room, dir.len + 1, room.len);
    if (name.len > 0) mem_copy(tail.ptr, name.ptr, name.len);
    *out = os_str_own((Str){p, room.len});
    return true;
}

// Whether `p` is a program this process may run: 0, or `access`'s errno
// (ENOENT for nothing there), into `*err`; false when the arena refuses.
static bool jobs_runnable(Arena *a, OsStr p, int *err) {
    char *z = arena_cstr(a, p);
    if (z == NULL) return false;
    *err = access(z, X_OK) == 0 ? 0 : errno;
    return true;
}

// argv[0] as execve would find it: a path (relative to `cwd`), or along
// PATH (its last `PATH=`), into `*out`. `*err` is 0 when it is found, ENOENT
// (or ENOTDIR) when nothing is there, and the errno of the first one there
// that cannot be run (EACCES) otherwise, as execve tells the two apart.
// STOP when the arena refuses.
static STATUS_MUST_USE Status jobs_find(Arena *a, OsStr argv0_os, SpawnStrs env, OsStr cwd, int *err, OsStr *out) {
    *err = ENOENT;
    Str argv0 = argv0_os.s;
    if (str_find_byte(argv0, '/') != NULL) {
        OsStr p = argv0_os;
        if (str_byte(argv0, 0) != '/' && !jobs_join(a, cwd.s, argv0, &p)) return STOP;
        if (!jobs_runnable(a, p, err)) return STOP;
        *out = p;
        return OK;
    }
    int seen = 0;
    Str path = S("/usr/local/bin:/usr/bin:/bin");
    for (size_t i = 0; i < env.len; i++) {
        Str e = at(env, i).s;
        if (str_starts_with(e, S("PATH="))) path = str_sub(e, 5, e.len);
    }
    Str rest = path;
    while (1) {
        const char *colon = str_find_byte(rest, ':');
        size_t to = colon != NULL ? (size_t)(colon - rest.ptr) : rest.len;
        Str dir = str_sub(rest, 0, to);
        if (dir.len > 0) {
            OsStr p = argv0_os;
            int e = 0;
            if (!jobs_join(a, dir, argv0, &p) || !jobs_runnable(a, p, &e)) return STOP;
            if (e == 0) {
                *err = 0;
                *out = p;
                return OK;
            }
            if (seen == 0 && e != ENOENT && e != ENOTDIR) seen = e;
        }
        if (colon == NULL) {
            if (seen != 0) *err = seen;
            return OK;
        }
        rest = str_sub(rest, to + 1, rest.len);
    }
}

// What `jobs_launch` gives: launched, or why not.
typedef enum {
    JOBS_LAUNCHED,
    JOBS_OUT_OF_MEMORY,
    // The reaper could not be forked.
    JOBS_SPAWN,
    // The reaper ended without writing a record.
    JOBS_NO_RECORD,
    // A record is there that cannot be read, is too long or is no record.
    JOBS_BAD_RECORD,
} JobsLaunched;

// The most a job's record may hold, as Zig's `.limited(64 * 1024)`.
enum { JOBS_RECORD_MAX = 64 * 1024 };

// `fd` read to its end, at most one byte past JOBS_RECORD_MAX (so a longer
// file shows as one), into `*out` in `a` at its length; `*err` is the errno
// of a read that failed, what came before it kept, else 0. False when the
// arena refuses. The read goes into a buffer of its own, a big allocation
// given back once its bytes are copied: Zig's `readFileAlloc` holds a
// record's length, and a buffer of the limit kept in the values' arena held
// 64 KiB a call, so a script polling `job_alive` every 20 ms ran a 256 MiB
// arena out in 40 seconds.
static bool jobs_read_capped(Arena *a, int fd, Bytes *out, int *err) {
    *err = 0;
    _Static_assert(JOBS_RECORD_MAX + 1 >= ARENA_BIG, "the read's buffer is a big allocation, which a free gives back");
    char *buf = arena_alloc(a, JOBS_RECORD_MAX + 1, 1);
    if (buf == NULL) return false;
    Bytes room = {buf, JOBS_RECORD_MAX + 1};
    size_t len = 0;
    while (len < room.len) {
        ssize_t got = read(fd, &at(room, len), room.len - len);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) {
            *err = errno;
            break;
        }
        if (got == 0) break;
        len += (size_t)got;
    }
    char *kept = arena_alloc(a, len, 1);
    if (kept != NULL && len > 0) mem_copy(kept, buf, len);
    arena_free(a, buf, room.len);
    if (kept == NULL) return false;
    out->ptr = kept;
    out->len = len;
    return true;
}

// What jobs_read_small gives besides 0 and an errno: the arena refused, or
// the file is past JOBS_RECORD_MAX. No errno is negative.
enum { JOBS_READ_NO_ROOM = -1, JOBS_READ_TOO_LONG = -2 };

// A file's bytes, at most 64 KiB, in the arena: 0, the errno of an open or
// a read that failed, JOBS_READ_TOO_LONG past the limit, or
// JOBS_READ_NO_ROOM when the arena refuses.
static int jobs_read_small(Arena *a, const char *path, Bytes *out) { // cstr: NUL-ended for the kernel
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return errno;
    Bytes text = {NULL, 0};
    int err = 0;
    bool read_ok = jobs_read_capped(a, fd, &text, &err);
    close(fd);
    if (!read_ok) return JOBS_READ_NO_ROOM;
    if (err != 0) return err;
    if (text.len > JOBS_RECORD_MAX) return JOBS_READ_TOO_LONG;
    *out = text;
    return 0;
}

// Double-forks `reaper --reap <record> <log> <cwd> <program> -- argv...` out
// of this process's tree with `env`, and reads the job's record once the
// reaper says it is written by a byte on its fd 3, a pipe. End of file with
// no byte means the reaper died first (JOBS_NO_RECORD, or JOBS_BAD_RECORD
// for one there that is no record). A slow reaper is
// only slow, so there is no deadline. The record's strings are in `a`.
static JobsLaunched jobs_launch(Arena *a, OsStr reaper, OsStr record, OsStr log, OsStr cwd, OsStr program, SpawnStrs argv, SpawnStrs env, JobRecord *out) {
    size_t n = 7 + argv.len;
    OsStr *all = ARENA_ARRAY(a, OsStr, n);
    if (all == NULL) return JOBS_OUT_OF_MEMORY;
    SLICE_TYPE(ArgsRoom, OsStr);
    ArgsRoom args = {all, n};
    at(args, 0) = reaper;
    at(args, 1) = os_str_own(S("--reap"));
    at(args, 2) = record;
    at(args, 3) = log;
    at(args, 4) = cwd;
    at(args, 5) = program;
    at(args, 6) = os_str_own(S("--"));
    for (size_t i = 0; i < argv.len; i++) at(args, 7 + i) = at(argv, i);
    SpawnStrs sargs = {all, n};
    char **zargv = spawn_cstr_list(a, sargs);
    char **zenv = spawn_cstr_list(a, env);
    char *zexe = arena_cstr(a, reaper);
    char *zrecord = arena_cstr(a, record);
    if (zargv == NULL || zenv == NULL || zexe == NULL || zrecord == NULL) return JOBS_OUT_OF_MEMORY;
    // An old record would be read as the new one's.
    unlink(zrecord);
    int started[2] = {-1, -1};
    SpawnPipe pipe_ends = {started, 2};
    if (pipe2(pipe_ends.ptr, O_CLOEXEC) != 0) return JOBS_SPAWN;
    // The reaper: /dev/null as 0 to 2 and the started pipe as fd 3, which
    // survives the exec, and nothing else.
    char *exe_room[1] = {zexe};
    SpawnChild child;
    spawn_child_default(&child);
    child.group = SPAWN_CHILD_INHERIT;
    child.pass_fd = at(pipe_ends, 1);
    child.pass_at = JOBS_STARTED_FD;
    child.paths = (SpawnCStrs){exe_room, 1};
    child.argv = zargv;
    child.envp = zenv;
    pid_t first = fork();
    if (first < 0) {
        close(at(pipe_ends, 0));
        close(at(pipe_ends, 1));
        return JOBS_SPAWN;
    }
    if (first == 0) spawn_exec_detached(&child);
    close(at(pipe_ends, 1));
    int status = 0;
    spawn_reap(first, &status, NULL);
    char byte = 0;
    while (read(at(pipe_ends, 0), &byte, 1) < 0 && errno == EINTR) {
    }
    close(at(pipe_ends, 0));
    // The reaper wrote its byte or exited; either way the record is final.
    // DRAGON: the process boundary. A record is whatever text the file in
    // the log's folder holds, here and wherever a later call finds the job
    // again (host_jobs_find), and what it says (`pid`, `pgid`, `start`)
    // decides what `jobs_stop` and `jobs_alive` send `kill`.
    // REMEDY: job_record.c reads only a record whose pgid is its pid, and
    // pgroup_signal refuses a pgid below 2, so no record reaches kill(-1)
    // or this process's own group. What is left: a forged record can name
    // any process or process group of the user's, which is no more than a
    // script can already do with `run({"kill", ...})`.
    Bytes text = {NULL, 0};
    int got = jobs_read_small(a, zrecord, &text);
    if (got == JOBS_READ_NO_ROOM) return JOBS_OUT_OF_MEMORY;
    if (got == ENOENT) return JOBS_NO_RECORD;
    if (got != 0 || !job_record_parse(text, out)) return JOBS_BAD_RECORD;
    return JOBS_LAUNCHED;
}

static int64_t jobs_now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec;
}

// Writes the record to `tmp` and renames it over `record`, so a reader never
// sees a partial one; whether it could.
static bool jobs_write_record(const char *record, const char *tmp, JobRecord r) { // cstr: NUL-ended for the kernel
    char room[8192];
    Bytes buf = {room, sizeof room};
    Str text = S("");
    if (!job_record_format(buf, r, &text)) return false;
    // DRAGON: the temp file is a name in the log's folder, which another
    // process may write: a link planted there would be followed, and the
    // file it names truncated and written.
    // REMEDY: what is there is unlinked (a link itself, never what it
    // names), and the file is made new, O_EXCL and O_NOFOLLOW, so one
    // planted between the two is refused, never followed.
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    ssize_t w = write(fd, text.ptr, text.len);
    close(fd);
    if (w < 0 || (size_t)w != text.len || rename(tmp, record) != 0) {
        unlink(tmp);
        return false;
    }
    return true;
}

// Waits, once a job's leader is reaped and its group has had its TERM, for
// the group to empty: KILL at `grace_ms`, then up to `grace_ms` more for a
// process stuck in the kernel, which is then left, so the job's end is still
// written. A process that left the group (`setsid`) is not waited for.
static void jobs_end_group(int32_t pgid, uint64_t grace_ms) {
    bool killed = false;
    for (uint64_t waited = 0; !pgroup_gone(pgid); waited += JOBS_GROUP_TICK_MS) {
        if (waited >= 2 * grace_ms) return;
        // The leader is reaped (the DRAGON at pgroup_kill_left).
        if (!killed && waited >= grace_ms) {
            pgroup_kill_left(pgid);
            killed = true;
        }
        jobs_sleep_ms(JOBS_GROUP_TICK_MS);
    }
}

// The reaper's argument `i`, as the kernel handed it: NUL-ended, so it
// holds no NUL.
static OsStr jobs_arg(char **argv, size_t argc, size_t i) {
    SLICE_TYPE(Argv, char *);
    Argv all = {argv, argc};
    return os_str_own((Str){at(all, i), strlen(at(all, i))});
}

// The reaper, `shale --reap <record> <log> <cwd> <program> -- argv...`, its
// environment the job's: starts the job, writes its record, waits for it,
// ends its group and writes how it ended, so a job's end is written only once
// its group is empty. The exit code says only whether it could. `args` are
// the words after `--reap`.
static inline int jobs_reap_main(Arena *a, char **args, size_t n, SpawnStrs env) {
    if (n < 6 || !str_eq(jobs_arg(args, n, 4).s, S("--"))) return 2;
    OsStr record_s = jobs_arg(args, n, 0);
    Buf tb = buf_make(a);
    // A temp file of its own, so a reaper ending as another starts never
    // shares one.
    if (buf_str(&tb, record_s.s) != OK || buf_byte(&tb, '.') != OK || buf_u64(&tb, (uint64_t)getpid()) != OK || buf_lit(&tb, ".tmp") != OK) return 2;
    char *tmp = arena_cstr(a, os_str_own(buf_view(&tb)));
    char *record = arena_cstr(a, record_s);
    char *log = arena_cstr(a, jobs_arg(args, n, 1));
    char *cwd = arena_cstr(a, jobs_arg(args, n, 2));
    char *program = arena_cstr(a, jobs_arg(args, n, 3));
    if (tmp == NULL || record == NULL || log == NULL || cwd == NULL || program == NULL) return 2;
    size_t argc = n - 5;
    OsStr *argv = ARENA_ARRAY(a, OsStr, argc);
    if (argv == NULL) return 2;
    SLICE_TYPE(ArgvRoom, OsStr);
    ArgvRoom room = {argv, argc};
    Buf shown = buf_make(a);
    for (size_t i = 0; i < argc; i++) {
        at(room, i) = jobs_arg(args, n, 5 + i);
        if ((i > 0 && buf_byte(&shown, ' ') != OK) || buf_str(&shown, at(room, i).s) != OK) return 2;
    }
    SpawnStrs sargv = {argv, argc};
    char **zargv = spawn_cstr_list(a, sargv);
    char **zenv = spawn_cstr_list(a, env);
    if (zargv == NULL || zenv == NULL) return 2;
    // The started pipe must not leak into the job.
    fcntl(JOBS_STARTED_FD, F_SETFD, FD_CLOEXEC);
    JobsStat own;
    Str reaper_start = S("");
    if (!jobs_start_of(&own, getpid(), &reaper_start)) return 2;
    // O_CLOEXEC, so the job has its log as 1 and 2 alone.
    int log_fd = open(log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (log_fd < 0) return 2;
    // The job: a session of its own, stdin /dev/null, stdout and stderr its
    // log; a `chdir` that fails exits 126, an exec 127.
    char *program_room[1] = {program};
    SpawnChild job;
    spawn_child_default(&job);
    job.group = SPAWN_CHILD_SESSION;
    job.stdout_fd = log_fd;
    job.stderr_fd = log_fd;
    job.cwd = cwd;
    job.paths = (SpawnCStrs){program_room, 1};
    job.argv = zargv;
    job.envp = zenv;
    pid_t pid = fork();
    if (pid < 0) return 2;
    if (pid == 0) spawn_exec_child(&job);
    close(log_fd);
    JobRecord r;
    memset(&r, 0, sizeof r);
    r.pid = (int32_t)pid;
    r.pgid = (int32_t)pid;
    r.started = jobs_now_seconds();
    r.argv = buf_view(&shown);
    r.reaper = (int32_t)getpid();
    r.reaper_start = reaper_start;
    r.start = S("");
    JobsStat child;
    // The child's stat may take a moment to appear.
    for (size_t tries = 0; tries < 100; tries++) {
        Str s = S("");
        if (jobs_start_of(&child, r.pid, &s)) {
            r.start = s;
            break;
        }
        jobs_sleep_ms(1);
    }
    // The record is a job's only handle, so a job whose first record cannot
    // be written is killed, and `jobs_launch` says JOBS_NO_RECORD.
    if (!jobs_write_record(record, tmp, r)) {
        pgroup_signal(r.pgid, SIGKILL);
        kill(pid, SIGKILL);
        int st = 0;
        spawn_reap(pid, &st, NULL);
        return 2;
    }
    ssize_t w = write(JOBS_STARTED_FD, "s", 1);
    (void)w;
    close(JOBS_STARTED_FD);
    // The leader's exit is seen first and left unreaped (WNOWAIT): its
    // zombie keeps its pid the group's id, so the TERM to what it left
    // running in its group can reach no stranger.
    // DRAGON: the job's end. A wait that failed on the reaper's own child
    // would leave `raw` 0, and the record would say the job exited 0.
    // REMEDY: a wait broken by a signal is asked again; any other failure
    // aborts the reaper (c_bug), so no end is written and `job_wait` says
    // `lost`, never a false `exited 0`.
    siginfo_t info;
    for (;;) {
        if (waitid(P_PID, (id_t)pid, &info, WEXITED | WNOWAIT) == 0) break;
        if (errno != EINTR) c_bug("jobs: the reaper's wait for its job's leader failed");
    }
    pgroup_signal(r.pgid, SIGTERM);
    int raw = 0;
    spawn_reap(pid, &raw, NULL);
    if (WIFEXITED(raw)) {
        r.has_exit = true;
        r.exit = (uint8_t)WEXITSTATUS(raw);
    } else {
        r.has_signal = true;
        r.signal = (uint32_t)WTERMSIG(raw);
    }
    jobs_end_group(r.pgid, JOBS_GROUP_GRACE_MS);
    r.has_ended = true;
    r.ended = jobs_now_seconds();
    return jobs_write_record(record, tmp, r) ? 0 : 2;
}
