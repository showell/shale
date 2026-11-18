// vec.h: a typed dynamic array in an arena. It grows by Zig Shale's rule but
// for one number, so the bytes it holds can differ from Zig's, which
// docs/C-SEAMS.md ("Fidelity") allows:
//
//   - the new capacity for a need of `n` items is
//     `n + n/2 + max(1, 64 / sizeof(T))`, with a 64-byte cache line where
//     Zig's x86-64 build has 128;
//   - it first tries to grow in place (`arena_resize`, which only the arena's
//     last allocation can), and otherwise takes a new buffer, copies the
//     items and frees the old one.
//
// `VEC_DEFINE(Name, T)` makes `Name`, a struct of `ptr`, `len`, `cap` and
// `arena`, indexed with `at(v, i)` like any slice, and its functions:
// `Name_make`, `Name_reserve`, `Name_push`, `Name_pop` and `Name_items`.

#ifndef SHALE_VEC_H
#define SHALE_VEC_H

#include "arena.h"
#include "status.h"

// The capacity to grow to for a need of `minimum` items of `elem` bytes,
// saturating: half again, plus a 64-byte line of items.
static inline size_t vec_grow_capacity(size_t minimum, size_t elem) {
    size_t init = 64 / elem;
    if (init < 1) init = 1;
    size_t extra = minimum / 2 + init;
    return minimum > SIZE_MAX - extra ? SIZE_MAX : minimum + extra;
}

// Makes room for `need` items of `elem` bytes in the buffer at `*ptr`, which
// holds `len` of `*cap`. STOP when the arena refuses (its `hit` set) or the
// size overflows.
static inline STATUS_MUST_USE Status vec_grow_raw(Arena *a, void **ptr, size_t *cap, size_t len, size_t need, size_t elem, size_t align) {
    if (*cap >= need) return OK;
    size_t new_cap = vec_grow_capacity(need, elem);
    // On a vec of one item, I64s_reserve(SIZE_MAX / 4) would ask for 64 bytes
    // for 3 * 2^61 + 8 items: arena_size_mul refuses the product.
    size_t bytes = 0;
    if (!arena_size_mul(a, new_cap, elem, &bytes)) return STOP;
    if (*cap > 0 && arena_resize(a, *ptr, *cap * elem, bytes)) {
        *cap = new_cap;
        return OK;
    }
    void *fresh = arena_alloc(a, bytes, align);
    if (fresh == NULL) return STOP;
    // mem_move, not mem_copy: GCC at -O2 cannot prove the two buffers apart
    // and warns (-Wrestrict); they never overlap, and the cost is the same.
    // A vec never grown holds NULL and no items: it has nothing to copy or
    // free. The test is of `*cap`, not of `len`, since GCC at -O2 can see a
    // vec made with NULL but not that its `len` is 0, and warns
    // (-Warray-bounds) of a copy from NULL.
    if (*cap > 0) {
        mem_move(fresh, *ptr, len * elem);
        // A pointer into the old buffer dangles now (arena_free's DRAGON).
        arena_free(a, *ptr, *cap * elem);
    }
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
    static inline STATUS_MUST_USE Status Name##_reserve(Name *v, size_t more) {              \
        /* A wrapped `v->len + more` would report room the vec doesn't have. */            \
        size_t need = 0;                                                                     \
        if (!arena_size_add(v->arena, v->len, more, &need)) return STOP;                     \
        void *p = v->ptr;                                                                    \
        Status s = vec_grow_raw(v->arena, &p, &v->cap, v->len, need, sizeof(T), _Alignof(T)); \
        v->ptr = p;                                                                          \
        return s;                                                                            \
    }                                                                                        \
                                                                                             \
    static inline STATUS_MUST_USE Status Name##_push(Name *v, T x) {                         \
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
