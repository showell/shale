// spawn.c: runs one program to its end or its deadline, the raw spawn and
// poll loop under `run`.
//
// It forks by hand, since `posix_spawn` has no hook for a death signal. The
// child is spawn_child.c's: it sets a death signal, takes a process group of
// its own, gets stdin from the given bytes or /dev/null and stdout and
// stderr on one pipe (stdout to a given file instead, for `run`'s
// `stdout_secret`), then `chdir`s and `execve`s along PATH. A failed exec
// reports its errno through a close-on-exec pipe, so "not found" is an
// answer, not a hang.
//
// The parent polls the output, the stdin it is still writing, a pidfd for
// the exit and a signalfd for TERM and HUP. On the deadline, or a TERM or HUP
// to `shale` itself, the whole group gets TERM, a grace period, then KILL.
// When the leader exits, what it left running in its group gets the same
// TERM, grace and KILL before `run` returns, so no program outlives its `run`
// (a long-lived one belongs under `job_start`). A TERM or HUP to `shale`
// while that group ends, or while the output drains, still stops the run.
// After the leader exits, output is read for at most `drain_ms` more, so a
// process that left the group and holds the pipe open never holds `run`.
//
// The including file defines _GNU_SOURCE before its first include, for
// pipe2, wait4, signalfd and syscall. The process must not ignore SIGPIPE,
// and must not die of it either: `spawn_quiet_sigpipe` sets a handler that
// does nothing, so a write to a closed stdin is an error, not a death, and a
// program still starts with SIGPIPE's default.
//
// Include spawn_argv.c, pgroup.c and spawn_child.c first: spawn_argv.c's
// strings and pipes are jobs.c's too.

#ifndef _GNU_SOURCE
#error "spawn.c needs _GNU_SOURCE defined before the first include: pipe2, wait4, signalfd and syscall"
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "arena.h"
#include "status.h"

// A program to run and how; `spawn_spec_default` gives its defaults.
typedef struct {
    // argv[0] is found along PATH unless it has a `/`; then it is a path,
    // relative to `cwd`.
    SpawnStrs argv;
    // The OS folder it starts in; empty for `shale`'s own.
    OsStr cwd;
    // The whole environment, as `K=V` strings.
    SpawnStrs env;
    // The bytes for stdin, or NULL for /dev/null.
    const Str *stdin_bytes;
    // An open file stdout goes to instead of the pipe (`run`'s
    // `stdout_secret`); stderr stays on the pipe. -1 for the pipe.
    int stdout_fd;
    // The most bytes the program may write to any file (RLIMIT_FSIZE, set in
    // the child before exec), so `stdout_fd`'s file cannot grow without bound
    // while it runs: past it the kernel ends the program with SIGXFSZ, or its
    // write fails. SPAWN_UNLIMITED for no limit.
    uint64_t file_max;
    // The niceness the program runs at, 0 to 19, set in the child before
    // exec (`run`'s `nice`), so no `nice` program wraps it; SPAWN_NICE_UNSET
    // leaves `shale`'s own.
    int nice;
    // When the program's group gets TERM.
    uint64_t timeout_ms;
    // The time between TERM and KILL.
    uint64_t grace_ms;
    // How long output is still read after the program exits.
    uint64_t drain_ms;
} SpawnSpec;

static void spawn_spec_default(SpawnSpec *s) {
    memset(s, 0, sizeof *s);
    s->stdin_bytes = NULL;
    s->stdout_fd = -1;
    s->file_max = SPAWN_UNLIMITED;
    s->nice = SPAWN_NICE_UNSET;
    s->grace_ms = 2000;
    s->drain_ms = 200;
}

// How a program ended.
typedef enum {
    // It exited with `code`.
    SPAWN_EXIT,
    // Its deadline passed and it was ended.
    SPAWN_TIMEOUT,
    // A signal `run` did not send ended it; see `signal`.
    SPAWN_SIGNAL,
    // `shale` itself was told to stop (TERM or HUP) and ended it.
    SPAWN_INTERRUPTED,
} SpawnEnded;

// An outcome's `code` when the program did not exit.
enum { SPAWN_NO_CODE = -1 };

// How a program ended, and how long it ran.
typedef struct {
    SpawnEnded ended;
    // The exit code, or SPAWN_NO_CODE when it did not exit.
    int code;
    // The signal that ended it, or 0 when none did (no signal is numbered 0).
    uint32_t signal;
    // Milliseconds the run took, the drain included.
    uint64_t ms;
    // Its user and system CPU time, and that of the children it waited for
    // (`wait4`'s rusage), in milliseconds. A busy box stretches `ms` and
    // barely moves this, so a budget can judge it.
    uint64_t cpu_ms;
    // Why a program did not start, when spawn_run said SPAWN_NOT_RUNNABLE,
    // SPAWN_NO_CWD or SPAWN_REFUSED: the errno of the call that failed (the
    // exec, the `chdir`, or the pipe, fork, signalfd or pidfd). 0 otherwise.
    int err;
} SpawnOutcome;

// What `spawn_run` gives: OK, or why the program did not run to its end.
typedef enum {
    SPAWN_OK,
    // The arena refused the argv, the environment or the paths.
    SPAWN_OUT_OF_MEMORY,
    // No such program, along PATH or at its path.
    SPAWN_NOT_FOUND,
    // Found but not runnable: no permission, or not a program.
    SPAWN_NOT_RUNNABLE,
    // `cwd` is not a folder that can be entered.
    SPAWN_NO_CWD,
    // The kernel refused a pipe, a fork or a pidfd.
    SPAWN_REFUSED,
    // The sink returned STOP; the program's group was killed and reaped
    // first. The sink's own state says why.
    SPAWN_SINK_STOPPED,
} SpawnResult;

// Where the output goes as it comes, stdout and stderr as one stream. A STOP
// ends the run (the keep at the host's cap).
typedef struct {
    void *ctx;
    Status (*write)(void *ctx, Str bytes);
} SpawnSink;

typedef struct pollfd SpawnPollFd;
SLICE_TYPE(SpawnPollFds, SpawnPollFd);

static void spawn_noop_handler(int sig) {
    (void)sig;
}

// SIGPIPE given a handler that does nothing, so a write to a pipe whose
// reader is gone fails with EPIPE, and an exec resets it to the default for
// the program. Ignoring it instead would pass SIG_IGN on to every program.
static void spawn_quiet_sigpipe(void) {
    struct sigaction act;
    memset(&act, 0, sizeof act);
    act.sa_handler = spawn_noop_handler;
    sigemptyset(&act.sa_mask);
    sigaction(SIGPIPE, &act, NULL);
}

// Milliseconds on the monotonic clock, which a deadline is measured on
// because the wall clock can jump.
static uint64_t spawn_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint64_t spawn_sat_sub(uint64_t a, uint64_t b) {
    return a > b ? a - b : 0;
}

static uint64_t spawn_usage_ms(const struct rusage *u) {
    int64_t us = ((int64_t)u->ru_utime.tv_sec + (int64_t)u->ru_stime.tv_sec) * 1000000 + (int64_t)u->ru_utime.tv_usec + (int64_t)u->ru_stime.tv_usec;
    return (uint64_t)(us / 1000);
}

// The paths `execve` tries, in order, into `*paths`: argv[0] itself if it
// has a `/`, else argv[0] in each folder of the environment's PATH (its last
// `PATH=`), or of a default PATH; an empty folder is
// argv[0] itself. False when the arena refuses.
static bool spawn_candidates(Arena *a, OsStr argv0_os, SpawnStrs env, SpawnCStrs *paths) {
    Str argv0 = argv0_os.s;
    Str path = S("/usr/local/bin:/usr/bin:/bin");
    if (str_find_byte(argv0, '/') != NULL) {
        path = S("");
    } else {
        for (size_t i = 0; i < env.len; i++) {
            Str e = at(env, i).s;
            if (str_starts_with(e, S("PATH="))) path = str_sub(e, 5, e.len);
        }
    }
    size_t n = 1;
    for (size_t i = 0; i < path.len; i++) n += at(path, i) == ':';
    char **list = ARENA_ARRAY(a, char *, n);
    if (list == NULL) return false;
    SpawnCStrs out = {list, n};
    size_t from = 0;
    size_t k = 0;
    for (size_t i = 0; i <= path.len; i++) {
        if (i < path.len && at(path, i) != ':') continue;
        Str dir = str_sub(path, from, i);
        from = i + 1;
        if (dir.len == 0) {
            at(out, k) = arena_cstr(a, argv0_os);
        } else {
            if (dir.len > SIZE_MAX / 4 || argv0.len > SIZE_MAX / 4) return arena_refuse(a) != NULL;
            char *full = ARENA_ARRAY(a, char, dir.len + 1 + argv0.len + 1);
            if (full == NULL) return false;
            Bytes room = {full, dir.len + 1 + argv0.len + 1};
            mem_copy(room.ptr, dir.ptr, dir.len);
            at(room, dir.len) = '/';
            Bytes name = SLICE_SUB(Bytes, room, dir.len + 1, room.len);
            if (argv0.len > 0) mem_copy(name.ptr, argv0.ptr, argv0.len);
            at(room, room.len - 1) = 0;
            at(out, k) = full;
        }
        if (at(out, k) == NULL) return false;
        k++;
    }
    *paths = out;
    return true;
}

static void spawn_close(int *fd) {
    if (*fd >= 0) close(*fd);
    *fd = -1;
}

static void spawn_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Passes what the pipe holds to the sink until a read would block, into
// `buf`. Closes the pipe at its end. STOP when the sink does.
// DRAGON: memory. The program's output is unbounded and arrives at the
// program's pace, so a reader that held it would grow without bound.
// REMEDY: each read is at most `buf`'s 64 KiB and goes straight to the
// sink, which holds what it keeps under its own bound (keep.c's slots,
// `whole`'s cap), so nothing here grows with the output.
static STATUS_MUST_USE Status spawn_read_available(int *fd, Bytes buf, SpawnSink sink) {
    while (1) {
        ssize_t r = read(*fd, buf.ptr, buf.len);
        if (r < 0 && errno == EAGAIN) return OK;
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            spawn_close(fd);
            return OK;
        }
        TRY(sink.write(sink.ctx, str_sub(((Str){buf.ptr, buf.len}), 0, (size_t)r)));
    }
}

// The poll loop's state while a program runs.
typedef struct {
    pid_t pid;
    int out_r;
    int in_w;
    int pid_fd;
    int sig_fd;
    // The leader is reaped.
    bool exited;
    // The leader's group is empty, asked each turn once it is reaped.
    bool group_gone;
    // The leader is reaped and its group is empty, or was left a grace after
    // its KILL (a process stuck in the kernel), so `run` still returns.
    bool group_done;
    int raw;
    struct rusage usage;
    Str stdin_left;
    bool has_termed;
    uint64_t termed_at;
    bool killed;
    uint64_t killed_at;
    bool has_why;
    SpawnEnded why;
    uint64_t drain_until;
    uint64_t deadline;
} SpawnLoop;

// What the loop does at `now`, before it polls: TERM the group at the
// deadline, KILL it at the grace's end, while the leader runs or once it has
// exited leaving its group not done. Gives how long the poll may wait: the
// nearest of the deadline, the grace's end and the drain's, at most a
// second, and 10 ms while the group outlives its leader, to ask again
// whether it is gone. Pure, so its edges are tested to the millisecond.
typedef struct {
    bool term;
    bool kill;
    uint64_t wait_ms;
} SpawnStep;

static void spawn_step(bool exited, bool group_done, bool has_termed, uint64_t termed_at, bool killed, uint64_t deadline, uint64_t grace_ms, uint64_t drain_until, uint64_t now, SpawnStep *out) {
    SpawnStep s = {false, false, 1000};
    if (!exited && !has_termed && now >= deadline) {
        s.term = true;
        has_termed = true;
        termed_at = now;
    }
    if (!killed && !group_done && has_termed && now >= termed_at + grace_ms) {
        s.kill = true;
        killed = true;
    }
    uint64_t w = s.wait_ms;
    if (!exited && !has_termed && spawn_sat_sub(deadline, now) < w) w = spawn_sat_sub(deadline, now);
    if (has_termed && !killed && !group_done && spawn_sat_sub(termed_at + grace_ms, now) < w) w = spawn_sat_sub(termed_at + grace_ms, now);
    if (exited && now < drain_until && drain_until - now < w) w = drain_until - now;
    if (exited && !group_done && w > 10) w = 10;
    s.wait_ms = w;
    *out = s;
}

// Whether the signalfd held a TERM or HUP, taking it.
static bool spawn_take_signal(int sig_fd) {
    struct signalfd_siginfo info;
    return read(sig_fd, &info, sizeof info) == (ssize_t)sizeof info;
}

// A TERM or HUP read, in a turn's poll or as the loop ends: the run stops
// for it whenever it came, while the program runs, while its group ends or
// while its output drains; only a timeout already said keeps its word.
static void spawn_heard(SpawnLoop *l) {
    if (!l->has_why) {
        l->has_why = true;
        l->why = SPAWN_INTERRUPTED;
    }
}

// The group's state at `now`, before a turn: once the leader is reaped,
// whether its group is gone, and whether it is done (gone, or left a grace
// after its KILL).
static void spawn_settle(SpawnLoop *l, uint64_t grace_ms, uint64_t now) {
    if (l->exited && !l->group_gone) l->group_gone = pgroup_gone(l->pid);
    l->group_done = l->exited && (l->group_gone || (l->killed && now >= l->killed_at + grace_ms));
}

// One turn of the poll loop: the deadline's and the grace's signals, a poll,
// then what it found. STOP when the sink does.
static STATUS_MUST_USE Status spawn_turn(SpawnLoop *l, const SpawnSpec *spec, Bytes buf, SpawnSink sink) {
    uint64_t now = spawn_mono_ms();
    SpawnStep step;
    spawn_step(l->exited, l->group_done, l->has_termed, l->termed_at, l->killed, l->deadline, spec->grace_ms, l->drain_until, now, &step);
    // The TERM goes only to a leader not yet reaped, and the KILL, which may
    // come after the reap, asks first whether the group is gone (the DRAGONs
    // at pgroup_signal and pgroup_kill_left).
    if (step.term) {
        l->has_why = true;
        l->why = SPAWN_TIMEOUT;
        pgroup_signal(l->pid, SIGTERM);
        l->has_termed = true;
        l->termed_at = now;
    }
    if (step.kill) {
        pgroup_kill_left(l->pid);
        l->killed = true;
        l->killed_at = now;
    }
    SpawnPollFd fds_room[4];
    SpawnPollFds fds = {fds_room, 4};
    size_t nfds = 0;
    size_t out_at = SIZE_MAX;
    size_t in_at = SIZE_MAX;
    size_t pid_at = SIZE_MAX;
    if (l->out_r >= 0) {
        at(fds, nfds) = (SpawnPollFd){l->out_r, POLLIN, 0};
        out_at = nfds++;
    }
    if (l->in_w >= 0) {
        at(fds, nfds) = (SpawnPollFd){l->in_w, POLLOUT, 0};
        in_at = nfds++;
    }
    if (!l->exited) {
        at(fds, nfds) = (SpawnPollFd){l->pid_fd, POLLIN, 0};
        pid_at = nfds++;
    }
    at(fds, nfds) = (SpawnPollFd){l->sig_fd, POLLIN, 0};
    size_t sig_at = nfds++;
    int prc = poll(fds.ptr, (nfds_t)nfds, (int)(step.wait_ms < 60000 ? step.wait_ms : 60000));
    if (prc < 0 && errno == EINTR) return OK;
    // The descriptors are the run's own, open, and at most four: a poll that
    // fails otherwise is a bug in the C, and the loop would spin past the
    // program's deadline on it.
    if (prc < 0) c_bug("spawn: a poll of a program's own pipes and pidfd failed");

    if (out_at != SIZE_MAX && at(fds, out_at).revents != 0) TRY(spawn_read_available(&l->out_r, buf, sink));
    if (in_at != SIZE_MAX && at(fds, in_at).revents != 0) {
        size_t n = l->stdin_left.len < 64 * 1024 ? l->stdin_left.len : 64 * 1024;
        // DRAGON: the process boundary. A program that exits or closes its
        // stdin before reading it all makes this write raise SIGPIPE, whose
        // default kills `shale` mid-run, its program's group left running.
        // REMEDY: every caller (c/shale.c, the tests' ran.c and fixtures)
        // calls spawn_quiet_sigpipe first, so the write fails with EPIPE and
        // the rest of stdin is dropped.
        ssize_t w = write(l->in_w, l->stdin_left.ptr, n);
        if (w >= 0) {
            l->stdin_left = str_sub(l->stdin_left, (size_t)w, l->stdin_left.len);
        } else if (errno != EAGAIN && errno != EINTR) {
            l->stdin_left = str_sub(l->stdin_left, l->stdin_left.len, l->stdin_left.len);
        }
        if (l->stdin_left.len == 0) spawn_close(&l->in_w);
    }
    if (pid_at != SIZE_MAX && at(fds, pid_at).revents != 0) {
        // The leader has exited and is a zombie until wait4 reaps it, so its
        // pid is still the group's id: what it left running in the group
        // gets TERM now, the grace and the KILL after.
        if (!l->has_termed) {
            pgroup_signal(l->pid, SIGTERM);
            l->has_termed = true;
            l->termed_at = spawn_mono_ms();
        }
        if (spawn_reaped_now(l->pid, &l->raw, &l->usage)) {
            l->exited = true;
            l->group_gone = pgroup_gone(l->pid);
            // All the leader wrote is in the pipe now, though this poll may
            // have looked before it was. Read it before the drain starts, so
            // the drain bounds only what outlives the leader.
            if (l->out_r >= 0) TRY(spawn_read_available(&l->out_r, buf, sink));
            l->drain_until = spawn_mono_ms() + spec->drain_ms;
            spawn_close(&l->in_w);
        }
    }
    if (at(fds, sig_at).revents != 0 && spawn_take_signal(l->sig_fd)) {
        spawn_heard(l);
        if (!l->exited && !l->has_termed) {
            pgroup_signal(l->pid, SIGTERM);
            l->has_termed = true;
            l->termed_at = spawn_mono_ms();
        }
    }
    return OK;
}

// The parent's half once the child is forked: the exec's verdict, the pidfd,
// then the loop to the leader's exit and the drain's end.
static SpawnResult spawn_parent(const SpawnSpec *spec, SpawnSink sink, pid_t pid, uint64_t start, int *out_r, int err_r, int *in_w, int sig_fd, SpawnOutcome *outcome) {
    // A word on the error pipe means the exec failed.
    uint32_t why = 0;
    ssize_t n = read(err_r, &why, sizeof why);
    if (n == (ssize_t)sizeof why) {
        int status = 0;
        spawn_reap(pid, &status, NULL);
        if (why >= SPAWN_CHILD_NO_CWD) {
            outcome->err = (int)(why - SPAWN_CHILD_NO_CWD);
            return SPAWN_NO_CWD;
        }
        outcome->err = (int)why;
        return why == ENOENT ? SPAWN_NOT_FOUND : SPAWN_NOT_RUNNABLE;
    }
    // DRAGON: the process boundary. The loop sees the exit only through the
    // pidfd, so a child without one would be waited for by nothing.
    // REMEDY: when the kernel refuses the pidfd, the child's group is killed
    // and the child reaped before the refusal is said, so no program runs
    // untracked.
    int pid_fd = (int)syscall(SYS_pidfd_open, pid, 0);
    if (pid_fd < 0) {
        outcome->err = errno;
        pgroup_signal(pid, SIGKILL);
        int status = 0;
        spawn_reap(pid, &status, NULL);
        return SPAWN_REFUSED;
    }
    spawn_nonblocking(*out_r);
    if (*in_w >= 0) spawn_nonblocking(*in_w);

    SpawnLoop l;
    memset(&l, 0, sizeof l);
    l.pid = pid;
    l.out_r = *out_r;
    l.in_w = *in_w;
    l.pid_fd = pid_fd;
    l.sig_fd = sig_fd;
    l.stdin_left = spec->stdin_bytes != NULL ? *spec->stdin_bytes : S("");
    l.deadline = start + spec->timeout_ms;
    if (spec->stdin_bytes != NULL && l.stdin_left.len == 0) spawn_close(&l.in_w);
    char buf_room[64 * 1024];
    Bytes buf = {buf_room, sizeof buf_room};
    SpawnResult result = SPAWN_OK;
    while (1) {
        uint64_t now = spawn_mono_ms();
        spawn_settle(&l, spec->grace_ms, now);
        if (l.group_done) {
            // A signal that came since the last poll is taken here, not left
            // to kill `shale` without a word when the mask is put back.
            if (spawn_take_signal(l.sig_fd)) spawn_heard(&l);
            if (l.exited && (l.out_r < 0 || now >= l.drain_until)) break;
        }
        if (spawn_turn(&l, spec, buf, sink) != OK) {
            // A sink that stops (the keep at the host's cap) returns before
            // the program ends; without this the program would run on, then
            // linger as a zombie, or what it left in its group would.
            if (!l.exited) {
                pgroup_signal(pid, SIGKILL);
                spawn_reap(pid, &l.raw, NULL);
            }
            if (l.exited && !l.group_gone) pgroup_kill_left(pid);
            result = SPAWN_SINK_STOPPED;
            break;
        }
    }
    // The loop closed what it finished with; the caller closes the rest.
    *out_r = l.out_r;
    *in_w = l.in_w;
    close(pid_fd);
    if (result != SPAWN_OK) return result;
    uint64_t ms = spawn_mono_ms() - start;
    uint64_t cpu_ms = spawn_usage_ms(&l.usage);
    memset(outcome, 0, sizeof *outcome);
    outcome->code = SPAWN_NO_CODE;
    outcome->ms = ms;
    outcome->cpu_ms = cpu_ms;
    int status = l.raw;
    if (l.has_why) {
        outcome->ended = l.why;
        if (WIFSIGNALED(status)) outcome->signal = (uint32_t)WTERMSIG(status);
        if (WIFEXITED(status)) outcome->code = WEXITSTATUS(status);
    } else if (WIFEXITED(status)) {
        outcome->ended = SPAWN_EXIT;
        outcome->code = WEXITSTATUS(status);
    } else {
        outcome->ended = SPAWN_SIGNAL;
        outcome->signal = (uint32_t)WTERMSIG(status);
    }
    return SPAWN_OK;
}

// What `spawn_run` opens: three pipes, the signalfd, and whether TERM and
// HUP are blocked. `spawn_fds_release` closes and puts back whatever of it
// was opened, on every way out: a defer, in C.
typedef struct {
    int out_room[2];
    int err_room[2];
    int in_room[2];
    int sig_fd;
    bool masked;
    sigset_t old_mask;
} SpawnFds;

SLICE_TYPE(SpawnPipes, SpawnPipe);

static void spawn_fds_make(SpawnFds *f) {
    memset(f, 0, sizeof *f);
    SpawnPipe all[3] = {{f->out_room, 2}, {f->err_room, 2}, {f->in_room, 2}};
    SpawnPipes pipes = {all, 3};
    for (size_t p = 0; p < pipes.len; p++) {
        for (size_t i = 0; i < 2; i++) at(at(pipes, p), i) = -1;
    }
    f->sig_fd = -1;
}

static void spawn_fds_release(SpawnFds *f) {
    spawn_close(&f->sig_fd);
    // DRAGON: the process boundary. A mask left blocked would keep `shale`
    // deaf to TERM and HUP after a run.
    // REMEDY: spawn_run calls this on every way out of spawn_started, and it
    // puts the old mask back; a TERM that arrived after the loop's last look
    // is delivered here, with its default action.
    if (f->masked) sigprocmask(SIG_SETMASK, &f->old_mask, NULL);
    f->masked = false;
    SpawnPipe all[3] = {{f->out_room, 2}, {f->err_room, 2}, {f->in_room, 2}};
    SpawnPipes pipes = {all, 3};
    for (size_t p = 0; p < pipes.len; p++) {
        for (size_t i = 0; i < 2; i++) spawn_close(&at(at(pipes, p), i));
    }
}

// Opens the pipes and the signalfd into `f`, forks, and runs the parent's
// half. What it opened stays in `f` for its caller to release.
static SpawnResult spawn_started(const SpawnSpec *spec, SpawnSink sink, SpawnFds *f, const char *cwd, SpawnCStrs paths, char **argv, char **envp, SpawnOutcome *outcome) { // cstr: NUL-ended for the kernel
    SpawnPipe out_pipe = {f->out_room, 2};
    SpawnPipe err_pipe = {f->err_room, 2};
    SpawnPipe in_pipe = {f->in_room, 2};
    if (pipe2(out_pipe.ptr, O_CLOEXEC) != 0 || pipe2(err_pipe.ptr, O_CLOEXEC) != 0 || (spec->stdin_bytes != NULL && pipe2(in_pipe.ptr, O_CLOEXEC) != 0)) {
        outcome->err = errno;
        return SPAWN_REFUSED;
    }

    // While a program runs, TERM and HUP to `shale` go to the loop instead
    // of the default action; the child unblocks them before exec.
    // DRAGON: the process boundary. A TERM or HUP that lands between the
    // fork and the first poll would kill `shale` and leave the program's
    // group running; a child that kept the mask would start deaf to TERM.
    // REMEDY: the mask is blocked before the fork, so such a signal is held
    // for the signalfd and the loop ends the group first; the child sets an
    // empty mask before exec.
    sigset_t stop_set;
    sigemptyset(&stop_set);
    sigaddset(&stop_set, SIGTERM);
    sigaddset(&stop_set, SIGHUP);
    sigprocmask(SIG_BLOCK, &stop_set, &f->old_mask);
    f->masked = true;
    f->sig_fd = signalfd(-1, &stop_set, SFD_CLOEXEC | SFD_NONBLOCK);
    if (f->sig_fd < 0) {
        outcome->err = errno;
        return SPAWN_REFUSED;
    }

    SpawnChild child;
    spawn_child_default(&child);
    child.parent = getpid();
    child.stdin_fd = at(in_pipe, 0);
    child.stdout_fd = spec->stdout_fd >= 0 ? spec->stdout_fd : at(out_pipe, 1);
    child.stderr_fd = at(out_pipe, 1);
    child.file_max = spec->file_max;
    child.nice = spec->nice;
    child.cwd = cwd;
    child.paths = paths;
    child.argv = argv;
    child.envp = envp;
    child.report_w = at(err_pipe, 1);
    uint64_t start = spawn_mono_ms();
    pid_t pid = fork();
    if (pid < 0) {
        outcome->err = errno;
        return SPAWN_REFUSED;
    }
    if (pid == 0) spawn_exec_child(&child);

    spawn_close(&at(out_pipe, 1));
    spawn_close(&at(err_pipe, 1));
    spawn_close(&at(in_pipe, 0));
    return spawn_parent(spec, sink, pid, start, &at(out_pipe, 0), at(err_pipe, 0), &at(in_pipe, 1), f->sig_fd, outcome);
}

// Runs a program to its end or its deadline, passing its output (stdout and
// stderr as one stream) to `sink` as it comes, and fills `*outcome`. What it
// builds for the child (argv, envp, the paths) is in `a`.
static SpawnResult spawn_run(Arena *a, const SpawnSpec *spec, SpawnSink sink, SpawnOutcome *outcome) {
    memset(outcome, 0, sizeof *outcome);
    char **argv = spawn_cstr_list(a, spec->argv);
    if (argv == NULL) return SPAWN_OUT_OF_MEMORY;
    char **envp = spawn_cstr_list(a, spec->env);
    if (envp == NULL) return SPAWN_OUT_OF_MEMORY;
    SpawnCStrs paths = {NULL, 0};
    if (!spawn_candidates(a, at(spec->argv, 0), spec->env, &paths)) return SPAWN_OUT_OF_MEMORY;
    char *cwd = NULL;
    if (spec->cwd.s.len > 0) {
        cwd = arena_cstr(a, spec->cwd);
        if (cwd == NULL) return SPAWN_OUT_OF_MEMORY;
    }

    SpawnFds f;
    spawn_fds_make(&f);
    SpawnResult result = spawn_started(spec, sink, &f, cwd, paths, argv, envp, outcome);
    spawn_fds_release(&f);
    return result;
}
