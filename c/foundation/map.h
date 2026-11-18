// map.h: the one map Shale needs, from `Str` keys, in an arena. It keeps its
// keys in the order they were first set: `keys(m)` lists them in that order,
// and so does every walk of a map, so the order is part of the language.
//
// The entries sit in insertion order in parallel arrays (keys, hashes,
// values); an open-addressing index of `uint32_t` points into them, 0 for an
// empty slot and an entry's place plus one otherwise, probed linearly and
// kept at most three quarters full. A removal keeps the order and rebuilds
// the index, so there are no tombstones.
//
// A key is held as given, not copied: it must live as long as the map, which
// a key in the same arena does.
//
// `MAP_DEFINE(Name, V)` makes `Name` and its functions: `Name_make`,
// `Name_put`, `Name_get`, `Name_ptr`, `Name_remove`, `Name_key_at`,
// `Name_value_at` and `Name_len`.

#ifndef SHALE_MAP_H
#define SHALE_MAP_H

#include "vec.h"

SLICE_TYPE(MapIndex, uint32_t);
SLICE_TYPE(MapKeys, Str);
SLICE_TYPE(MapHashes, uint64_t);

// FNV-1a, 64 bits. The order of a map never depends on it.
static inline uint64_t map_hash(Str key) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < key.len; i++) {
        h ^= str_byte(key, i);
        h *= 1099511628211ull;
    }
    return h;
}

// The keys and the index; the typed map adds its values beside them. Each
// array grows on its own, as a vec, so a refusal part way through a put
// leaves every array whole and `len` as it was.
typedef struct {
    Arena *arena;
    // DRAGON: use after free. A key is held by its pointer, not copied, so a
    // key made in a buffer that later grows, or in an arena span later
    // reset, dangles, and each lookup that probes its slot reads freed
    // memory.
    // REMEDY: a caller gives keys that live as long as the map, as a key in
    // the map's own arena does, since a run never resets an arena. Nothing
    // checks it.
    Str *keys;
    size_t keys_cap;
    uint64_t *hashes;
    size_t hashes_cap;
    size_t len;
    uint32_t *index;
    size_t index_cap;
} MapCore;

// The slot of `key` in the index, or of the empty slot where it would go. The
// index is at most three quarters full, so the probe always ends.
static inline size_t map_core_slot(const MapCore *m, Str key, uint64_t h, bool *found) {
    MapIndex index = {m->index, m->index_cap};
    MapKeys keys = {m->keys, m->len};
    MapHashes hashes = {m->hashes, m->len};
    size_t mask = m->index_cap - 1;
    size_t slot = (size_t)h & mask;
    for (;;) {
        uint32_t e = at(index, slot);
        if (e == 0) {
            *found = false;
            return slot;
        }
        if (at(hashes, e - 1) == h && str_eq(at(keys, e - 1), key)) {
            *found = true;
            return slot;
        }
        slot = (slot + 1) & mask;
    }
}

// The place of `key` among the entries, or SIZE_MAX.
static inline size_t map_core_find(const MapCore *m, Str key) {
    if (m->len == 0) return SIZE_MAX;
    bool found;
    size_t slot = map_core_slot(m, key, map_hash(key), &found);
    MapIndex index = {m->index, m->index_cap};
    return found ? (size_t)at(index, slot) - 1 : SIZE_MAX;
}

// Every entry put back into the index, which has room for them all.
static inline void map_core_rebuild(MapCore *m) {
    MapIndex index = {m->index, m->index_cap};
    MapHashes hashes = {m->hashes, m->len};
    size_t mask = m->index_cap - 1;
    if (m->index_cap > 0) memset(m->index, 0, m->index_cap * sizeof(uint32_t));
    for (size_t e = 0; e < m->len; e++) {
        size_t slot = (size_t)at(hashes, e) & mask;
        while (at(index, slot) != 0) slot = (slot + 1) & mask;
        at(index, slot) = (uint32_t)(e + 1);
    }
}

// A fresh index of `index_cap` slots, a power of two with room for every
// entry.
static inline STATUS_MUST_USE Status map_core_reindex(MapCore *m, size_t index_cap) {
    uint32_t *fresh = ARENA_ARRAY(m->arena, uint32_t, index_cap);
    if (fresh == NULL) return STOP;
    if (m->index_cap > 0) arena_free(m->arena, m->index, m->index_cap * sizeof(uint32_t));
    m->index = fresh;
    m->index_cap = index_cap;
    map_core_rebuild(m);
    return OK;
}

// Room for one more entry: the keys and the hashes, and the index kept a
// quarter free.
static inline STATUS_MUST_USE Status map_core_reserve_one(MapCore *m) {
    // DRAGON: bounds. The index holds an entry's place plus one in 32 bits,
    // so past UINT32_MAX - 1 entries the cast in `_put` and map_core_rebuild
    // would wrap and a slot would point at the wrong entry.
    // REMEDY: this check refuses the put first. No test reaches it: the
    // arena budget stops a map long before.
    if (m->len >= UINT32_MAX - 1) {
        arena_set_hit(m->arena);
        return STOP;
    }
    void *k = m->keys;
    Status s = vec_grow_raw(m->arena, &k, &m->keys_cap, m->len, m->len + 1, sizeof(Str), _Alignof(Str));
    m->keys = k;
    TRY(s);
    void *h = m->hashes;
    s = vec_grow_raw(m->arena, &h, &m->hashes_cap, m->len, m->len + 1, sizeof(uint64_t), _Alignof(uint64_t));
    m->hashes = h;
    TRY(s);
    if ((m->len + 1) * 4 > m->index_cap * 3) {
        size_t want = m->index_cap == 0 ? 8 : m->index_cap * 2;
        TRY(map_core_reindex(m, want));
    }
    return OK;
}

// Removes the entry at `e`, keeping the others' order.
static inline void map_core_remove_at(MapCore *m, size_t e) {
    MapKeys keys = {m->keys, m->len};
    MapHashes hashes = {m->hashes, m->len};
    // `e` is checked first: at SIZE_MAX, `e + 1` wraps to 0 and the parts pass.
    (void)at(keys, e);
    MapKeys keys_after = SLICE_SUB(MapKeys, keys, e + 1, m->len);
    MapHashes hashes_after = SLICE_SUB(MapHashes, hashes, e + 1, m->len);
    if (keys_after.len > 0) {
        mem_move(&at(keys, e), keys_after.ptr, keys_after.len * sizeof(Str));
        mem_move(&at(hashes, e), hashes_after.ptr, hashes_after.len * sizeof(uint64_t));
    }
    m->len--;
    map_core_rebuild(m);
}

#define MAP_DEFINE(Name, V)                                                                  \
    SLICE_TYPE(Name##Values, V);                                                             \
    typedef struct {                                                                         \
        MapCore core;                                                                        \
        V *values;                                                                           \
        size_t values_cap;                                                                   \
    } Name;                                                                                  \
                                                                                             \
    static inline Name Name##_make(Arena *a) {                                               \
        Name m;                                                                              \
        memset(&m, 0, sizeof m);                                                             \
        m.core.arena = a;                                                                    \
        return m;                                                                            \
    }                                                                                        \
                                                                                             \
    static inline size_t Name##_len(const Name *m) {                                         \
        return m->core.len;                                                                  \
    }                                                                                        \
                                                                                             \
    /* Sets `key` to `v`: a new key goes last, a key already there keeps its place. */     \
    static inline STATUS_MUST_USE Status Name##_put(Name *m, Str key, V v) {                 \
        size_t e = map_core_find(&m->core, key);                                             \
        if (e != SIZE_MAX) {                                                                 \
            Name##Values vals = {m->values, m->core.len};                                    \
            at(vals, e) = v;                                                                 \
            return OK;                                                                       \
        }                                                                                    \
        TRY(map_core_reserve_one(&m->core));                                                 \
        void *p = m->values;                                                                 \
        Status s = vec_grow_raw(m->core.arena, &p, &m->values_cap, m->core.len, m->core.len + 1, sizeof(V), _Alignof(V)); \
        m->values = p;                                                                       \
        TRY(s);                                                                              \
        uint64_t h = map_hash(key);                                                          \
        bool found;                                                                          \
        size_t slot = map_core_slot(&m->core, key, h, &found);                               \
        size_t n = m->core.len;                                                              \
        MapKeys keys = {m->core.keys, m->core.keys_cap};                                     \
        MapHashes hashes = {m->core.hashes, m->core.hashes_cap};                             \
        Name##Values vals = {m->values, m->values_cap};                                      \
        MapIndex index = {m->core.index, m->core.index_cap};                                 \
        at(keys, n) = key;                                                                   \
        at(hashes, n) = h;                                                                   \
        at(vals, n) = v;                                                                     \
        at(index, slot) = (uint32_t)(n + 1);                                                 \
        m->core.len = n + 1;                                                                 \
        return OK;                                                                           \
    }                                                                                        \
                                                                                             \
    /* The value's place, or NULL when `key` is not there, until the next put or remove */   \
    /* (the DRAGON at arena_free). */                                                        \
    static inline V *Name##_ptr(Name *m, Str key) {                                          \
        size_t e = map_core_find(&m->core, key);                                             \
        if (e == SIZE_MAX) return NULL;                                                      \
        Name##Values vals = {m->values, m->core.len};                                        \
        return &at(vals, e);                                                                 \
    }                                                                                        \
                                                                                             \
    static inline bool Name##_get(const Name *m, Str key, V *out) {                          \
        size_t e = map_core_find(&m->core, key);                                             \
        if (e == SIZE_MAX) return false;                                                     \
        Name##Values vals = {m->values, m->core.len};                                        \
        *out = at(vals, e);                                                                  \
        return true;                                                                         \
    }                                                                                        \
                                                                                             \
    /* Removes `key`, keeping the others' order; false when it was not there. */           \
    static inline bool Name##_remove(Name *m, Str key) {                                     \
        size_t e = map_core_find(&m->core, key);                                             \
        if (e == SIZE_MAX) return false;                                                     \
        Name##Values vals = {m->values, m->core.len};                                        \
        Name##Values vals_after = SLICE_SUB(Name##Values, vals, e + 1, m->core.len);         \
        mem_move(&at(vals, e), vals_after.ptr, vals_after.len * sizeof(V));                  \
        map_core_remove_at(&m->core, e);                                                     \
        return true;                                                                         \
    }                                                                                        \
                                                                                             \
    /* The key and the value at place `i`, in the order first set. */                      \
    static inline Str Name##_key_at(const Name *m, size_t i) {                               \
        MapKeys keys = {m->core.keys, m->core.len};                                          \
        return at(keys, i);                                                                  \
    }                                                                                        \
                                                                                             \
    static inline V Name##_value_at(const Name *m, size_t i) {                               \
        Name##Values vals = {m->values, m->core.len};                                        \
        return at(vals, i);                                                                  \
    }

#endif
