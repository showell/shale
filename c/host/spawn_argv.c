// spawn_argv.c: what spawn.c and jobs.c both hand the kernel: a program's
// argv and environment as NUL-ended strings, and a pipe's two ends; and how
// both reap a child of their own. A unit that includes jobs.c without
// spawn.c (test_jobs.c) takes these alone.

#include <errno.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "arena.h"

SLICE_TYPE(SpawnStrs, const Str);
// A NUL-ended string for the kernel, and a list of them ended by NULL.
SLICE_TYPE(SpawnCStrs, char *);
// A pipe's two ends, as `pipe2` fills them.
SLICE_TYPE(SpawnPipe, int);

// `s` with a NUL after it, in the arena; NULL when refused.
static char *spawn_cstr(Arena *a, Str s) {
    // DRAGON: size overflow. `s.len + 1` would wrap to 0 for a length of
    // SIZE_MAX, and the copy would run past a zero-byte allocation.
    // REMEDY: a length of SIZE_MAX - 1 or more is refused through the arena
    // before the sum is taken.
    if (s.len >= SIZE_MAX - 1) return arena_refuse(a);
    char *p = ARENA_ARRAY(a, char, s.len + 1);
    if (p == NULL) return NULL;
    Bytes room = {p, s.len + 1};
    if (s.len > 0) mem_copy(room.ptr, s.ptr, s.len);
    at(room, s.len) = 0;
    return p;
}

// `items` as a NULL-ended list of NUL-ended strings, for execve; NULL when
// refused.
static char **spawn_cstr_list(Arena *a, SpawnStrs items) {
    char **list = ARENA_ARRAY(a, char *, items.len + 1);
    if (list == NULL) return NULL;
    SpawnCStrs out = {list, items.len + 1};
    for (size_t i = 0; i < items.len; i++) {
        at(out, i) = spawn_cstr(a, at(items, i));
        if (at(out, i) == NULL) return NULL;
    }
    at(out, items.len) = NULL;
    return list;
}

// Reaps `pid`, a child of this process, waiting for it to end: its raw
// status in `*raw` and, when `usage` is not NULL, what it used.
// DRAGON: the process boundary. A wait that fails on our own child (ECHILD,
// EINVAL) leaves the child unreaped and its status unread, so the caller
// would go on as if it had ended with a status of 0.
// REMEDY: a wait broken by a signal is asked again; any other failure is a
// bug in the C (nothing else in `shale` reaps its children, and SIGCHLD keeps
// its default), so the process ends here.
static inline void spawn_reap(pid_t pid, int *raw, struct rusage *usage) {
    for (;;) {
        pid_t got = wait4(pid, raw, 0, usage);
        if (got == pid) return;
        if (got < 0 && errno == EINTR) continue;
        c_bug("spawn: a wait for one of shale's own children failed");
    }
}

// Whether `pid`, a child of this process, has ended and is reaped now, its
// raw status in `*raw` and what it used in `*usage`; false while it runs or
// when a signal broke the wait. A wait that fails otherwise is a bug in the
// C, as in spawn_reap.
static inline bool spawn_reaped_now(pid_t pid, int *raw, struct rusage *usage) {
    pid_t got = wait4(pid, raw, WNOHANG, usage);
    if (got == pid) return true;
    if (got == 0 || (got < 0 && errno == EINTR)) return false;
    c_bug("spawn: a wait for one of shale's own children failed");
    // Never reached: TCC refuses a function that might return no value.
    return false;
}
