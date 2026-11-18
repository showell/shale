// arena.h: the run's arena, whose size is a hard byte budget. Every byte the
// C takes from the system comes through here; no other file calls `malloc` or
// `free` (`c/check_discipline.shale` holds it).
//
// Its shape is Zig Shale's budgeted arena, so a budget means about what it
// means there, though the bytes held need not match (docs/C-SEAMS.md,
// "Fidelity"):
//
//   - Small allocations are bumped out of chunks of at least
//     `ARENA_CHUNK_MIN`, each twice the last when the limit has that left.
//     Each chunk is a 16-byte header, then its bytes, and `held` counts every
//     chunk's whole size.
//   - An allocation of `ARENA_BIG` or more has a chunk of its own, given back
//     to the system when it is freed, so a list grown to megabytes holds its
//     live buffer, not every one it outgrew.
//   - Only the last small allocation can grow, shrink or be freed, in place;
//     any other free gives nothing back until a reset.
//   - Past the limit an allocation is refused: it returns NULL and sets `hit`,
//     and the refusal the arena was made with (`arena_init`'s `refused`, its
//     run's `RunCtx.refused`). The caller makes that the budget stop
//     (`run_oom`, err.h), "the arena budget ran out".
//
// A mark and a reset give back everything allocated since the mark, small
// and big alike.

#ifndef SHALE_ARENA_H
#define SHALE_ARENA_H

#include "slice.h"

typedef struct ArenaChunk {
    struct ArenaChunk *next;
    // The chunk's size, its header included.
    size_t total;
} ArenaChunk;

_Static_assert(sizeof(ArenaChunk) == 16, "the chunk header is Zig's 16 bytes");

enum {
    ARENA_CHUNK_MIN = 16 * 1024,
    ARENA_BIG = 64 * 1024,
    ARENA_MARKS_MAX = 16,
};

// What a mark remembers: the arena's place when the mark was opened.
typedef struct {
    ArenaChunk *chunk;
    size_t end;
    ArenaChunk *bigs;
    size_t used;
} ArenaSaved;

SLICE_TYPE(ArenaSavedSlice, ArenaSaved);

typedef struct {
    // DRAGON: the arena budget. The fields are public, so a caller that
    // writes `limit` or `held` lets a run hold more than its budget.
    // REMEDY: only arena_take adds to `held`, after checking `limit`, and only
    // arena_give_back takes from it; only arena_init and the tests set
    // `limit`. No check holds this: a reading of the C does.

    // The most `held` may reach.
    size_t limit;
    // Every chunk's bytes, its header among them: what the arena holds.
    size_t held;
    // The bytes handed out (a measure, not a bound).
    size_t used;
    // Whether an allocation was refused.
    bool hit;
    // The refusal a refused allocation sets beside `hit`: its run's
    // `RunCtx.refused`, or a local of the code that made it outside any run.
    bool *refused;
    // The newest small chunk, linked to the older ones.
    ArenaChunk *chunk;
    // How far into the newest chunk's bytes the next allocation starts.
    size_t end;
    // The big allocations' chunks, one each, newest first.
    ArenaChunk *bigs;
    // The marks open, innermost last (`arena_mark`).
    ArenaSaved marks[ARENA_MARKS_MAX];
    size_t marks_len;
    // For tests: when `backing_faults`, the system refuses once
    // `backing_ok_left` more allocations have been made.
    bool backing_faults;
    size_t backing_ok_left;
} Arena;

// Marks `a` as having refused an allocation, and its refusal (its run's, for
// an arena of a run) as one no stop has read yet. A run's refusals come from
// its values, its machine and a dozen scratch arenas, all made with its
// RunCtx's `refused`, so the stop that reads one (err.h's run_oom) is held to
// there being one, whichever refused.
// DRAGON: the arena budget. A STOP that no arena refused, read as the arena
// budget, would say a budget ran out when a bug in the C stopped the run.
// REMEDY: every refusal sets `*refused` through arena_set_hit (no `hit =
// true` anywhere else), run_oom aborts unless it is set and clears it, and
// test_err's death tests hold both.
static inline void arena_set_hit(Arena *a) {
    a->hit = true;
    *a->refused = true;
}

// Which mark: a reset gives back what came after it.
typedef struct {
    size_t depth;
} ArenaMark;

// What a zero-length allocation returns: never NULL, never read. It is the
// alignment as an address, so no compiler takes it for a small object a later
// write would overflow.
static inline void *arena_empty(size_t align) {
    return (void *)(uintptr_t)align;
}

// A misuse of the arena by its caller (a bad alignment, a free of memory it
// did not hand out): a bug in the C, so the process ends.
static inline void arena_misuse(const char *what) { // cstr: a literal naming the misuse
    c_bug_str("arena", (Str){what, strlen(what)});
}

// Makes `*a` an empty arena that may hold at most `limit` bytes, whose
// refusals set `*refused`: its run's `RunCtx.refused`, or, outside any run (a
// test, the CLI's own setup), a local of the code that made it. There is no
// arena without one.
static inline void arena_init(Arena *a, size_t limit, bool *refused) {
    if (refused == NULL) arena_misuse("an arena made with no refusal to set");
    memset(a, 0, sizeof *a);
    a->limit = limit;
    a->refused = refused;
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
    // DRAGON: the arena budget. This is the one check of `held` against
    // `limit`, so a chunk taken from the system anywhere else escapes the
    // budget.
    // REMEDY: the malloc below is the C's only one (check_discipline's alloc
    // rule), and `total` is at most what is left, so `held + total` can't
    // wrap.
    if (total > arena_sat_sub(a->limit, a->held)) {
        arena_set_hit(a);
        return NULL;
    }
    if (a->backing_faults) {
        if (a->backing_ok_left == 0) {
            arena_set_hit(a);
            return NULL;
        }
        a->backing_ok_left--;
    }
    ArenaChunk *c = malloc(total);
    if (c == NULL) {
        arena_set_hit(a);
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
    // DRAGON: bounds. `start + len` would wrap for a `len` near SIZE_MAX and
    // pass this check, handing out bytes past the chunk.
    // REMEDY: only a `len` under ARENA_BIG gets here (arena_alloc gives a
    // bigger one its own chunk), and `start` is less than `end + align`, with
    // `align` at most 2^63, so the sum can't wrap.
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
    // DRAGON: size overflow. `16 + len + align` can wrap to a few bytes, and
    // a huge allocation would get a tiny chunk whose writes run past it.
    // REMEDY: this check refuses such a `len` before the sum, and test_arena
    // catches its plant. Every container's growth ends here, so it is the
    // last check before the heap.
    if (len > SIZE_MAX - sizeof(ArenaChunk) - align) {
        arena_set_hit(a);
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
        // A new chunk is twice the last, at least ARENA_CHUNK_MIN, cut to
        // what the limit has left, and never smaller than `need`.
        size_t last = a->chunk != NULL ? a->chunk->total : 0;
        size_t twice = last > SIZE_MAX / 2 ? SIZE_MAX : 2 * last;
        size_t grown = twice > ARENA_CHUNK_MIN ? twice : ARENA_CHUNK_MIN;
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

// Grows or shrinks `p` from `old_len` to `new_len` in place, if it can. A big
// allocation never changes in place, and a small one becomes big only by
// moving. Any allocation but the last can only shrink.
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
                // DRAGON: use after free. The chunk goes back to the system,
                // so every pointer into it dangles from here: a buf_view, a
                // map's `_ptr`, a key or an Items slice taken before a
                // growth.
                // REMEDY: none in the arena, which can't see who holds a
                // pointer. buf_str rebases its own view of the buffer; every
                // other holder must drop its pointers before a growth, and
                // the full tier's ASan fails a test that reads a freed chunk.
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
// Marks nest, at most `ARENA_MARKS_MAX` deep.
static inline ArenaMark arena_mark(Arena *a) {
    if (a->marks_len == ARENA_MARKS_MAX) arena_misuse("more marks open than ARENA_MARKS_MAX");
    ArenaSaved s = {a->chunk, a->end, a->bigs, a->used};
    ArenaSavedSlice room = {a->marks, ARENA_MARKS_MAX};
    at(room, a->marks_len) = s;
    ArenaMark m = {a->marks_len};
    a->marks_len++;
    return m;
}

// Gives back every chunk, small and big, made since `m`, and closes `m` and
// every mark opened after it.
static inline void arena_reset(Arena *a, ArenaMark m) {
    // DRAGON: use after free. Every chunk made since the mark goes back to
    // the system, so a pointer into one held by anything older than the mark
    // dangles: a key in an older map, a stop's message or trail.
    // REMEDY: only c/tests/test_arena.c resets, on arenas of its own; a run
    // never does (a yield copies what it keeps into a fresh arena instead).
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

// Every chunk back to the system; the arena is empty, its limit and its
// refusal kept.
static inline void arena_deinit(Arena *a) {
    for (int k = 0; k < 2; k++) {
        ArenaChunk *c = k == 0 ? a->chunk : a->bigs;
        while (c != NULL) {
            ArenaChunk *next = c->next;
            free(c);
            c = next;
        }
    }
    size_t limit = a->limit;
    bool *refused = a->refused;
    arena_init(a, limit, refused);
}

static inline void *arena_refuse(Arena *a) {
    arena_set_hit(a);
    return NULL;
}

// One `T`, or NULL when refused.
#define ARENA_NEW(a, T) ((T *)arena_alloc((a), sizeof(T), _Alignof(T)))

// `n` of `T`, or NULL when refused (an `n` whose size overflows among them).
// DRAGON: size overflow. `n * sizeof(T)` can wrap to a small size (or to 0,
// whose sentinel is not NULL), and the caller would index `n` items past it.
// REMEDY: the division refuses such an `n` before the multiply, and
// test_arena catches its plant. `n` is evaluated twice, and nothing checks
// it for a side effect: one is the caller's bug.
#define ARENA_ARRAY(a, T, n) \
    ((T *)((n) > SIZE_MAX / sizeof(T) ? arena_refuse(a) : arena_alloc((a), (n) * sizeof(T), _Alignof(T))))

// A copy of `s` in the arena; false when refused.
static inline bool arena_dup(Arena *a, Str s, Str *out) {
    char *p = arena_alloc(a, s.len, 1);
    if (p == NULL) return false;
    mem_copy(p, s.ptr, s.len);
    out->ptr = p;
    out->len = s.len;
    return true;
}

#endif
