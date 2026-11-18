// pgroup.c: a process group signalled, or asked whether it is empty. Every
// `kill(-pgid, ...)` in the host is one of these, so the two ways a group's
// id can name the wrong processes are guarded here alone: spawn.c's ends of
// a program's group, zig_ast.c's of zig-ast's, jobs.c's of a job's.

#include <errno.h>
#include <signal.h>
#include <sys/types.h>

// Whether no process is left in the group `pgid`. Asked only once its
// leader is reaped: until then the leader's zombie is a member.
static bool pgroup_gone(pid_t pgid) {
    return kill(-pgid, 0) != 0 && errno == ESRCH;
}

// Signals the group `pgid`; whether the kernel took it. A pgid below 2 is
// refused before any `kill`.
// DRAGON: the process boundary. `kill(-pgid)` signals whichever group holds
// the id: kill(-1) every process `shale` may signal, kill(0) this process's
// own group, and once a group's last process is gone the kernel may give its
// id to a new one. A pgid may come from a job record's text, which any
// process can write.
// REMEDY: a pgid below 2 is refused here, so no forged or broken record
// reaches kill(-1) or this group. A caller signals through this only a group
// whose leader it has not reaped, so the leader's zombie still holds the id;
// after the reap it goes through pgroup_kill_left. jobs_stop, which signals
// a job from another process, checks the leader's start time first, which
// narrows the race with a reused id and does not close it.
static bool pgroup_signal(pid_t pgid, int sig) {
    if (pgid < 2) return false;
    return kill(-pgid, sig) == 0;
}

// KILLs what is left in the group `pgid` once its leader is reaped: a
// process the leader started that outlived it.
// DRAGON: the process boundary. Once the leader is reaped, its pid stays the
// group's id only while a member is left; a group that empties frees it, and
// a new group given the id would be signalled.
// REMEDY: the KILL goes only to a group pgroup_gone finds not empty a moment
// before, whose members hold the id; a group that empties in that moment is
// the one window left. A job's leader KILLed before its `setsid` never made
// the group, so its id is free from the reap, and the same check guards it.
static void pgroup_kill_left(pid_t pgid) {
    if (!pgroup_gone(pgid)) pgroup_signal(pgid, SIGKILL);
}
