// vec.h: a typed dynamic array in an arena, where Zig Shale's
// `std.ArrayList(T)` goes. It grows as Zig's does, so an arena holds what Zig's
// held for the same pushes:
//
//   - the new capacity for a need of `n` items is `n + n/2 + max(1, 64 / sizeof(T))`
//     (Zig's `growCapacity`, its 64 the cache line);
//   - it first tries to grow in place (`arena_resize`, which only the arena's
//     last allocation can), and otherwise takes a new buffer, copies the items
//     and frees the old one (`ensureTotalCapacityPrecise`).
//
// `VEC_DEFINE(Name, T)` makes `Name`, a struct of `ptr`, `len`, `cap` and
// `arena`, indexed with `at(v, i)` like any slice, and its functions:
// `Name_make`, `Name_reserve`, `Name_push`, `Name_pop` and `Name_items`.

#ifndef SHALE_VEC_H
#define SHALE_VEC_H

#include "arena.h"
#include "status.h"

// Zig's `growCapacity(minimum)` for items of `elem` bytes, saturating.
static inline size_t vec_grow_capacity(size_t minimum, size_t elem) {
    size_t init = 64 / elem;
    if (init < 1) init = 1;
    size_t extra = minimum / 2 + init;
    return minimum > SIZE_MAX - extra ? SIZE_MAX : minimum + extra;
}

// Makes room for `need` items of `elem` bytes in the buffer at `*ptr`, which
// holds `len` of `*cap`. STOP when the arena refuses (its `hit` set) or the
// size overflows.
static inline Status vec_grow_raw(Arena *a, void **ptr, size_t *cap, size_t len, size_t need, size_t elem, size_t align) {
    if (*cap >= need) return OK;
    size_t new_cap = vec_grow_capacity(need, elem);
    if (new_cap > SIZE_MAX / elem) {
        a->hit = true;
        return STOP;
    }
    size_t bytes = new_cap * elem;
    if (*cap > 0 && arena_resize(a, *ptr, *cap * elem, bytes)) {
        *cap = new_cap;
        return OK;
    }
    void *fresh = arena_alloc(a, bytes, align);
    if (fresh == NULL) return STOP;
    // memmove, not memcpy: GCC at -O2 cannot prove the two buffers apart and
    // warns (-Wrestrict); they never overlap, and the cost is the same.
    if (len > 0) memmove(fresh, *ptr, len * elem);
    if (*cap > 0) arena_free(a, *ptr, *cap * elem);
    *ptr = fresh;
    *cap = new_cap;
    return OK;
}

#define VEC_DEFINE(Name, T)                                                                  \
    typedef struct {                                                                         \
        T *ptr;                                                                              \
        size_t len;                                                                          \
        size_t cap;                                                                          \
        Arena *arena;                                                                        \
    } Name;                                                                                  \
    SLICE_TYPE(Name##Items, T);                                                              \
                                                                                             \
    static inline Name Name##_make(Arena *a) {                                               \
        Name v = {NULL, 0, 0, a};                                                            \
        return v;                                                                            \
    }                                                                                        \
                                                                                             \
    /* Room for `more` items past `len`. */                                                  \
    static inline Status Name##_reserve(Name *v, size_t more) {                              \
        if (more > SIZE_MAX - v->len) {                                                      \
            v->arena->hit = true;                                                            \
            return STOP;                                                                     \
        }                                                                                    \
        void *p = v->ptr;                                                                    \
        Status s = vec_grow_raw(v->arena, &p, &v->cap, v->len, v->len + more, sizeof(T), _Alignof(T)); \
        v->ptr = p;                                                                          \
        return s;                                                                            \
    }                                                                                        \
                                                                                             \
    static inline Status Name##_push(Name *v, T x) {                                         \
        TRY(Name##_reserve(v, 1));                                                           \
        v->len++;                                                                            \
        at(*v, v->len - 1) = x;                                                              \
        return OK;                                                                           \
    }                                                                                        \
                                                                                             \
    /* The last item, taken off. Popping an empty one is a bug in the C. */                  \
    static inline T Name##_pop(Name *v) {                                                    \
        T x = at(*v, v->len - 1);                                                            \
        v->len--;                                                                            \
        return x;                                                                            \
    }                                                                                        \
                                                                                             \
    static inline Name##Items Name##_items(const Name *v) {                                  \
        Name##Items s = {v->ptr, v->len};                                                    \
        return s;                                                                            \
    }

#endif
