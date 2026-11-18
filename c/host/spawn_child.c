// spawn_child.c: the child's side of a fork, the code between the fork and
// the exec. Every child the host starts (`run`'s program in spawn.c, the
// run's zig-ast in zig_ast.c, a job's reaper and the job itself in jobs.c)
// is made by `spawn_exec_child` from a `SpawnChild` its parent built before
// the fork, so the rules of that stretch are kept in one place.
//
// Include spawn_argv.c first. The including file defines _GNU_SOURCE before
// its first include, for syscall.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

// A `file_max` that sets no limit.
#define SPAWN_UNLIMITED UINT64_MAX

// A `nice` that leaves the niceness as its parent's.
enum { SPAWN_NICE_UNSET = -1 };

// A failure's word on the report pipe for a `chdir` that failed: this plus
// its errno, so it reads apart from an exec's errno.
enum { SPAWN_CHILD_NO_CWD = 1000 };

// The process group the child is in once it runs.
typedef enum {
    // A group of its own (`setpgid`), so a signal to the group reaches every
    // process the program starts.
    SPAWN_CHILD_GROUP,
    // A session of its own (`setsid`), and so a group: a job, apart from its
    // reaper.
    SPAWN_CHILD_SESSION,
    // Its parent's: the grandchild `spawn_exec_detached` forks, in the
    // session its parent made.
    SPAWN_CHILD_INHERIT,
} SpawnChildGroup;

// What a child is made into before its exec, built before the fork;
// `spawn_child_default` gives its defaults.
typedef struct {
    // The pid that forked it, for a death signal that ends it should that
    // process die; 0 for none (a job outlives the `shale` that started it).
    pid_t parent;
    SpawnChildGroup group;
    // A descriptor the program is handed as `pass_at` (jobs.c's started
    // pipe), every descriptor above `pass_at` closed; -1 for none. A child
    // that passes one has no `report_w`, which that would close.
    int pass_fd;
    int pass_at;
    // Its stdin, stdout and stderr; -1 is /dev/null.
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    // The most bytes the program may write to any file (RLIMIT_FSIZE), or
    // SPAWN_UNLIMITED.
    uint64_t file_max;
    // The niceness it runs at, 0 to 19 (`run`'s `nice`), or SPAWN_NICE_UNSET
    // for its parent's: a job and zig-ast leave it unset.
    int nice;
    // The OS folder it starts in, or NULL for its parent's.
    const char *cwd; // cstr: NUL-ended for the kernel
    // The paths an exec tries, in order, the first that runs taken.
    SpawnCStrs paths;
    char **argv;
    char **envp;
    // A close-on-exec pipe's write end, where a failure before the exec
    // writes its word (an errno, or SPAWN_CHILD_NO_CWD plus one), so the
    // parent reads an end of file at the exec and a word otherwise; -1 for
    // none.
    int report_w;
} SpawnChild;

static void spawn_child_default(SpawnChild *c) {
    memset(c, 0, sizeof *c);
    c->group = SPAWN_CHILD_GROUP;
    c->pass_fd = -1;
    c->pass_at = -1;
    c->stdin_fd = -1;
    c->stdout_fd = -1;
    c->stderr_fd = -1;
    c->file_max = SPAWN_UNLIMITED;
    c->nice = SPAWN_NICE_UNSET;
    c->cwd = NULL;
    c->report_w = -1;
}

// The child's failure: `word` on the report pipe, if it has one, then exit
// `code`.
static void spawn_child_fail(const SpawnChild *c, uint32_t word, int code) {
    if (c->report_w >= 0) {
        ssize_t w = write(c->report_w, &word, sizeof word);
        (void)w;
    }
    _exit(code);
}

// `fd` as the child's descriptor `to`, kept across the exec: dup2's copy has
// no close-on-exec, and an `fd` already at `to` has its own cleared.
static void spawn_child_place(int fd, int to) {
    if (fd == to) {
        fcntl(to, F_SETFD, 0);
    } else {
        dup2(fd, to);
    }
}

// Makes this process, just forked, into `c` and execs it. It never returns:
// a failure exits 126 for a `chdir`, else 127, its word on the report pipe.
// DRAGON: the process boundary. Between fork and exec the child holds a copy
// of the parent's memory and locks, so a malloc, a stdio call or anything not
// async-signal-safe can deadlock or corrupt it.
// REMEDY: every child the host forks runs this and nothing else (or
// spawn_exec_detached, which forks into it): it makes only raw system calls
// (and `sigemptyset`) on memory built before the fork, `c` and all it points
// to, and leaves by exec or _exit, never by return.
static void spawn_exec_child(const SpawnChild *c) {
    if (c->parent != 0) {
        // DRAGON: the process boundary. The death signal ends the child when
        // its parent dies, so a program never outlives `shale`; but the
        // parent may die before the prctl, and then the child's parent is
        // already another process and no signal will come.
        // REMEDY: the getppid check closes that race: a child whose parent
        // changed exits at once.
        prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
        if (getppid() != c->parent) _exit(127);
    }
    if (c->group == SPAWN_CHILD_GROUP) {
        // DRAGON: the process boundary. Its own group is what lets a TERM
        // and a KILL reach every process the program starts (`kill(-pid)`);
        // a signal sent before the group exists would miss it.
        // REMEDY: spawn.c and zig_ast.c signal the group only after reading
        // the report pipe, which closes at the exec, after this call.
        setpgid(0, 0);
    } else if (c->group == SPAWN_CHILD_SESSION) {
        setsid();
    }
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    if (c->pass_fd >= 0) {
        spawn_child_place(c->pass_fd, c->pass_at);
        syscall(SYS_close_range, (unsigned)c->pass_at + 1, ~0u, 0u);
    }
    // O_CLOEXEC, so the copy beside 0 to 2 never reaches the program.
    int null_fd = -1;
    if (c->stdin_fd < 0 || c->stdout_fd < 0 || c->stderr_fd < 0) null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    spawn_child_place(c->stdin_fd >= 0 ? c->stdin_fd : null_fd, 0);
    spawn_child_place(c->stdout_fd >= 0 ? c->stdout_fd : null_fd, 1);
    spawn_child_place(c->stderr_fd >= 0 ? c->stderr_fd : null_fd, 2);
    // DRAGON: the budgets. `stdout_secret`'s file is the program's stdout,
    // which no pipe bounds, so it could fill the disk before the cap is
    // checked at exit.
    // REMEDY: the hard and soft RLIMIT_FSIZE, one raw prlimit64 (async-signal
    // safe, nothing allocated), so the kernel holds it to `file_max` bytes;
    // a failure exits 127 before exec, never running the program unbounded.
    if (c->file_max != SPAWN_UNLIMITED) {
        struct rlimit lim = {(rlim_t)c->file_max, (rlim_t)c->file_max};
        if (syscall(SYS_prlimit64, 0, RLIMIT_FSIZE, &lim, NULL) != 0) spawn_child_fail(c, EPERM, 127);
    }
    // The niceness, one raw setpriority (async-signal safe). A process may
    // not lower its own niceness unprivileged, so when its parent already
    // runs nicer than asked (EACCES, EPERM) the program runs at the parent's,
    // never less nice; any other refusal exits 127 before exec.
    if (c->nice != SPAWN_NICE_UNSET && setpriority(PRIO_PROCESS, 0, c->nice) != 0 && errno != EACCES && errno != EPERM) spawn_child_fail(c, (uint32_t)errno, 127);
    if (c->cwd != NULL && chdir(c->cwd) != 0) spawn_child_fail(c, SPAWN_CHILD_NO_CWD + (uint32_t)errno, 126);
    int last = ENOENT;
    for (size_t i = 0; i < c->paths.len; i++) {
        execve(at(c->paths, i), c->argv, c->envp);
        if (errno != ENOENT && errno != ENOTDIR) last = errno;
    }
    spawn_child_fail(c, (uint32_t)last, 127);
}

// The first child of a double fork: a session of its own, then a grandchild
// made into `c` (whose `group` is SPAWN_CHILD_INHERIT), then exit 0, so the
// grandchild is re-parented to init, out of the caller's tree. It never
// returns; what it does between the forks is raw system calls alone, as in
// spawn_exec_child (the DRAGON there). `static inline`: a unit without
// jobs.c never calls it.
static inline void spawn_exec_detached(const SpawnChild *c) {
    setsid();
    pid_t second = fork();
    if (second == 0) spawn_exec_child(c);
    _exit(0);
}
