// spawn_argv.c: what spawn.c and jobs.c both hand the kernel: a program's
// argv and environment as NUL-ended strings, and a pipe's two ends. A unit
// that includes jobs.c without spawn.c (test_jobs.c) takes these alone.

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
    if (s.len > 0) memcpy(room.ptr, s.ptr, s.len);
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
