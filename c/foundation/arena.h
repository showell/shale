// arena.h: the run's arena, whose size is a hard byte budget. Every byte the
// C takes from the system comes through here; no other file calls `malloc` or
// `free` (`c/check_discipline.shale` holds it).
//
// Its accounting is Zig Shale's `value.Budgeted` exactly (src/script/value.zig),
// because what a run holds decides where it stops on the arena budget and what
// `budget().arena_bytes` says, and both must match byte for byte:
//
//   - Small allocations are bumped out of chunks of at least `ARENA_CHUNK_LEAST`,
//     each twice the last when the limit has that left. Each chunk is a 16-byte
//     header, then its bytes, and `held` counts every chunk's whole size.
//   - An allocation of `ARENA_BIG` or more has a chunk of its own, given back to
//     the system when it is freed, so a list grown to megabytes holds its live
//     buffer, not every one it outgrew.
//   - Only the last small allocation can grow, shrink or be freed, in place; any
//     other free is kept, as in any arena.
//   - Past the limit an allocation is refused: it returns NULL and sets `hit`.
//     The caller makes that the budget stop (`run_oom`, err.h), whose message is
//     Zig's: "the arena budget ran out".
//
// Beyond Zig's: a mark and a reset, which give back everything allocated since
// the mark, small and big alike.

#ifndef SHALE_ARENA_H
#define SHALE_ARENA_H

#include "slice.h"

typedef struct ArenaChunk {
    struct ArenaChunk *next;
    // The whole chunk, its header first, as the system gave it.
    size_t total;
} ArenaChunk;

_Static_assert(sizeof(ArenaChunk) == 16, "the chunk header is Zig's 16 bytes");

enum {
    ARENA_CHUNK_LEAST = 16 * 1024,
    ARENA_BIG = 64 * 1024,
    ARENA_MARKS_MOST = 16,
};

typedef struct {
    ArenaChunk *chunk;
    size_t end;
    ArenaChunk *bigs;
    size_t used;
} ArenaSaved;

SLICE_TYPE(ArenaSavedSlice, ArenaSaved);

typedef struct {
    // The most `held` may reach.
    size_t limit;
    // Every chunk's bytes, its header among them: what the arena holds.
    size_t held;
    // The bytes handed out (a measure, not a bound).
    size_t used;
    // Whether an allocation was refused.
    bool hit;
    // The newest small chunk, linked to the older ones.
    ArenaChunk *chunk;
    // How far into the newest chunk's bytes the next allocation starts.
    size_t end;
    // The big allocations' chunks, one each, newest first.
    ArenaChunk *bigs;
    // The marks open, innermost last (`arena_mark`).
    ArenaSaved marks[ARENA_MARKS_MOST];
    size_t marks_len;
    // For tests: when `backing_faults`, the system refuses once
    // `backing_ok_left` more allocations have been made.
    bool backing_faults;
    size_t backing_ok_left;
} Arena;

// Which mark: a reset gives back what came after it.
typedef struct {
    size_t depth;
} ArenaMark;

// What a zero-length allocation returns: never NULL, never read. It is
// Zig's own sentinel, the alignment as an address, so no compiler takes it
// for a small object a later write would overflow.
static inline void *arena_empty(size_t align) {
    return (void *)(uintptr_t)align;
}

// A misuse of the arena by its caller (a bad alignment, a free of memory it
// did not hand out): a bug in the C, so the process ends.
static inline void arena_misuse(const char *what) { // cstr: a literal naming the misuse
    fprintf(stderr, "arena: %s\n", what);
    abort();
}

static inline Arena arena_make(size_t limit) {
    Arena a;
    memset(&a, 0, sizeof a);
    a.limit = limit;
    return a;
}

static inline size_t arena_sat_sub(size_t a, size_t b) {
    return a > b ? a - b : 0;
}

static inline size_t arena_forward(uintptr_t addr, size_t align) {
    return (size_t)((addr + (align - 1)) & ~(uintptr_t)(align - 1));
}

static inline char *arena_chunk_bytes(ArenaChunk *c) {
    return (char *)c + sizeof(ArenaChunk);
}

static inline size_t arena_chunk_len(const ArenaChunk *c) {
    return c->total - sizeof(ArenaChunk);
}

// A chunk of `total` bytes from the system, if the limit has that left.
static inline ArenaChunk *arena_take(Arena *a, size_t total) {
    if (total > arena_sat_sub(a->limit, a->held)) {
        a->hit = true;
        return NULL;
    }
    if (a->backing_faults) {
        if (a->backing_ok_left == 0) {
            a->hit = true;
            return NULL;
        }
        a->backing_ok_left--;
    }
    ArenaChunk *c = malloc(total);
    if (c == NULL) {
        a->hit = true;
        return NULL;
    }
    c->next = NULL;
    c->total = total;
    a->held += total;
    return c;
}

static inline void arena_give_back(Arena *a, ArenaChunk *c) {
    a->held -= c->total;
    free(c);
}

// Where in the newest chunk an allocation of `len` aligned to `align` would
// start, or SIZE_MAX when it does not fit.
static inline size_t arena_fits(const Arena *a, size_t len, size_t align) {
    if (a->chunk == NULL) return SIZE_MAX;
    uintptr_t base = (uintptr_t)arena_chunk_bytes(a->chunk);
    size_t start = arena_forward(base + a->end, align) - (size_t)base;
    if (start + len > arena_chunk_len(a->chunk)) return SIZE_MAX;
    return start;
}

static inline char *arena_big_bytes(ArenaChunk *c, size_t align) {
    return (char *)arena_forward((uintptr_t)arena_chunk_bytes(c), align);
}

// `len` bytes aligned to `align` (a power of two), or NULL with `hit` set
// when the budget or the system refuses.
static inline void *arena_alloc(Arena *a, size_t len, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) arena_misuse("an alignment that is not a power of two");
    if (len == 0) return arena_empty(align);
    if (len > SIZE_MAX - sizeof(ArenaChunk) - align) {
        a->hit = true;
        return NULL;
    }
    size_t need = sizeof(ArenaChunk) + len + align;
    if (len >= ARENA_BIG) {
        ArenaChunk *c = arena_take(a, need);
        if (c == NULL) return NULL;
        c->next = a->bigs;
        a->bigs = c;
        a->used += len;
        return arena_big_bytes(c, align);
    }
    size_t start = arena_fits(a, len, align);
    if (start == SIZE_MAX) {
        // A new chunk: room for this and its alignment, at least the least,
        // twice the last when the limit has that left.
        size_t last = a->chunk != NULL ? a->chunk->total : 0;
        size_t twice = last > SIZE_MAX / 2 ? SIZE_MAX : 2 * last;
        size_t grown = twice > ARENA_CHUNK_LEAST ? twice : ARENA_CHUNK_LEAST;
        size_t left = arena_sat_sub(a->limit, a->held);
        size_t want = grown < left ? grown : left;
        if (want < need) want = need;
        ArenaChunk *c = arena_take(a, want);
        if (c == NULL) return NULL;
        c->next = a->chunk;
        a->chunk = c;
        a->end = 0;
        start = arena_fits(a, len, align);
    }
    a->end = start + len;
    a->used += len;
    return arena_chunk_bytes(a->chunk) + start;
}

// Whether `p` of `len` is the newest chunk's last allocation.
static inline bool arena_is_last(const Arena *a, const void *p, size_t len) {
    if (a->chunk == NULL) return false;
    return (const char *)p + len == arena_chunk_bytes(a->chunk) + a->end;
}

// Grows or shrinks `p` from `old_len` to `new_len` in place, if it can; Zig's
// `resize`. A big allocation never changes in place, and a small one becomes
// big only by moving. Any allocation but the last can only shrink.
static inline bool arena_resize(Arena *a, void *p, size_t old_len, size_t new_len) {
    if (old_len >= ARENA_BIG || new_len >= ARENA_BIG) return false;
    if (old_len == 0) return new_len == 0;
    if (!arena_is_last(a, p, old_len)) return new_len <= old_len;
    size_t start = a->end - old_len;
    if (start + new_len > arena_chunk_len(a->chunk)) return false;
    a->end = start + new_len;
    if (new_len > old_len) a->used += new_len - old_len;
    return true;
}

// Gives back `p` of `len`: a big allocation's chunk goes to the system, the
// last small allocation is unbumped, and any other is kept until a reset.
static inline void arena_free(Arena *a, void *p, size_t len) {
    if (len == 0) return;
    if (len >= ARENA_BIG) {
        ArenaChunk **link = &a->bigs;
        while (*link != NULL) {
            ArenaChunk *c = *link;
            char *from = arena_chunk_bytes(c);
            if ((char *)p >= from && (char *)p < (char *)c + c->total) {
                *link = c->next;
                // A mark that remembered this chunk as its newest big now
                // remembers the one before it.
                ArenaSavedSlice marks = {a->marks, a->marks_len};
                for (size_t m = 0; m < marks.len; m++) {
                    if (at(marks, m).bigs == c) at(marks, m).bigs = c->next;
                }
                arena_give_back(a, c);
                return;
            }
            link = &c->next;
        }
        arena_misuse("a free of a big allocation the arena did not make");
    }
    if (arena_is_last(a, p, len)) a->end -= len;
}

// Opens a mark: `arena_reset` with it gives back everything allocated since.
// Marks nest, at most `ARENA_MARKS_MOST` deep.
static inline ArenaMark arena_mark(Arena *a) {
    if (a->marks_len == ARENA_MARKS_MOST) arena_misuse("more marks open than ARENA_MARKS_MOST");
    ArenaSaved s = {a->chunk, a->end, a->bigs, a->used};
    ArenaSavedSlice room = {a->marks, ARENA_MARKS_MOST};
    at(room, a->marks_len) = s;
    ArenaMark m = {a->marks_len};
    a->marks_len++;
    return m;
}

// Gives back every chunk, small and big, made since `m`, and closes `m` and
// every mark opened after it.
static inline void arena_reset(Arena *a, ArenaMark m) {
    if (m.depth >= a->marks_len) arena_misuse("a reset to a mark that is not open");
    ArenaSavedSlice open = {a->marks, a->marks_len};
    ArenaSaved s = at(open, m.depth);
    while (a->chunk != s.chunk) {
        ArenaChunk *c = a->chunk;
        a->chunk = c->next;
        arena_give_back(a, c);
    }
    a->end = s.end;
    while (a->bigs != s.bigs) {
        ArenaChunk *c = a->bigs;
        a->bigs = c->next;
        arena_give_back(a, c);
    }
    a->used = s.used;
    a->marks_len = m.depth;
}

// Every chunk back to the system; the arena is empty, its limit kept.
static inline void arena_deinit(Arena *a) {
    for (int k = 0; k < 2; k++) {
        ArenaChunk *c = k == 0 ? a->chunk : a->bigs;
        while (c != NULL) {
            ArenaChunk *next = c->next;
            free(c);
            c = next;
        }
    }
    *a = arena_make(a->limit);
}

static inline void *arena_refuse(Arena *a) {
    a->hit = true;
    return NULL;
}

// One `T`, or NULL when refused.
#define ARENA_NEW(a, T) ((T *)arena_alloc((a), sizeof(T), _Alignof(T)))

// `n` of `T`, or NULL when refused (an `n` whose size overflows among them).
#define ARENA_ARRAY(a, T, n) \
    ((T *)((n) > SIZE_MAX / sizeof(T) ? arena_refuse(a) : arena_alloc((a), (n) * sizeof(T), _Alignof(T))))

// A copy of `s` in the arena; false when refused.
static inline bool arena_dup(Arena *a, Str s, Str *out) {
    char *p = arena_alloc(a, s.len, 1);
    if (p == NULL) return false;
    if (s.len > 0) memcpy(p, s.ptr, s.len);
    out->ptr = p;
    out->len = s.len;
    return true;
}

#endif
