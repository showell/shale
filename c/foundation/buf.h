// buf.h: a growable byte buffer in an arena, for building strings. It grows
// as vec.h's arrays do, and every append either fits whole or returns STOP
// with the buffer as it was.

#ifndef SHALE_BUF_H
#define SHALE_BUF_H

#include "vec.h"

typedef struct {
    char *ptr;
    size_t len;
    size_t cap;
    Arena *arena;
} Buf;

static inline Buf buf_make(Arena *a) {
    Buf b = {NULL, 0, 0, a};
    return b;
}

// Room for `more` bytes past `len`.
static inline STATUS_MUST_USE Status buf_reserve(Buf *b, size_t more) {
    // A wrapped `b->len + more` would report room the buffer doesn't have.
    size_t need = 0;
    if (!arena_size_add(b->arena, b->len, more, &need)) return STOP;
    void *p = b->ptr;
    Status s = vec_grow_raw(b->arena, &p, &b->cap, b->len, need, 1, 1);
    b->ptr = p;
    return s;
}

static inline STATUS_MUST_USE Status buf_byte(Buf *b, char c) {
    TRY(buf_reserve(b, 1));
    Bytes room = {b->ptr, b->cap};
    at(room, b->len) = c;
    b->len++;
    return OK;
}

static inline STATUS_MUST_USE Status buf_str(Buf *b, Str s) {
    if (s.len == 0) return OK;
    // DRAGON: use after free. `s` may be a view of this buffer
    // (`buf_str(b, buf_view(b))`, as `s .. s` makes). The reserve can move
    // the buffer and free the old one to the system, so a copy from `s`
    // after it would read freed memory.
    // REMEDY: a view inside the buffer is kept as an offset and rebased after
    // the reserve; test_buf catches its plant under ASan.
    uintptr_t from = (uintptr_t)s.ptr;
    uintptr_t base = (uintptr_t)b->ptr;
    bool inside = b->ptr != NULL && from >= base && from < base + b->len;
    size_t offset = inside ? (size_t)(from - base) : 0;
    TRY(buf_reserve(b, s.len));
    if (inside) s = str_sub(((Str){b->ptr, b->len}), offset, offset + s.len);
    Bytes room = {b->ptr, b->cap};
    Bytes dest = SLICE_SUB(Bytes, room, b->len, b->len + s.len);
    // memmove, not memcpy: once a caller's path is inlined, GCC at -O2 cannot
    // prove a fresh buffer apart from the text and warns (-Wrestrict); the
    // two never overlap, the rebase above sees to it, and the cost is the
    // same.
    mem_move(dest.ptr, s.ptr, s.len);
    b->len += s.len;
    return OK;
}

#define buf_lit(b, lit) buf_str((b), S(lit))

// `n` in decimal, with no sign, padding or separator.
static inline STATUS_MUST_USE Status buf_u64(Buf *b, uint64_t n) {
    char digits[20];
    Bytes d = {digits, sizeof digits};
    size_t k = d.len;
    do {
        k--;
        at(d, k) = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0);
    return buf_str(b, str_sub(((Str){digits, sizeof digits}), k, sizeof digits));
}

static inline STATUS_MUST_USE Status buf_i64(Buf *b, int64_t n) {
    if (n >= 0) return buf_u64(b, (uint64_t)n);
    TRY(buf_byte(b, '-'));
    // -(n + 1) + 1 never overflows, INT64_MIN among them.
    return buf_u64(b, (uint64_t)(-(n + 1)) + 1);
}

// The code point `cp` as UTF-8, one to four bytes appended whole. The caller
// has refused a surrogate half, and `cp` is at most U+10FFFF.
static inline STATUS_MUST_USE Status buf_utf8(Buf *b, uint32_t cp) {
    char seq[4];
    Bytes s = {seq, sizeof seq};
    size_t n = 4;
    if (cp < 0x80) {
        at(s, 0) = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        at(s, 0) = (char)(0xC0 | (cp >> 6));
        n = 2;
    } else if (cp < 0x10000) {
        at(s, 0) = (char)(0xE0 | (cp >> 12));
        n = 3;
    } else {
        at(s, 0) = (char)(0xF0 | (cp >> 18));
    }
    // Each continuation byte carries six bits, the last the lowest.
    for (size_t k = 1; k < n; k++) at(s, k) = (char)(0x80 | ((cp >> (6 * (n - 1 - k))) & 0x3F));
    return buf_str(b, str_sub(((Str){seq, sizeof seq}), 0, n));
}

// What the buffer holds.
// DRAGON: use after free. A growth moves the bytes and can free the old ones
// to the system, so a view kept across an append can dangle, not just go
// stale.
// REMEDY: a caller holds a view only until the buffer's next append, which
// nothing checks; buf_str rebases a view of its own buffer, and the full
// tier's ASan fails a test that reads a freed one.
static inline Str buf_view(const Buf *b) {
    Str s = {b->ptr, b->len};
    return s;
}

// Gives back the room past `len` when the buffer is its arena's last
// allocation, as Zig's `toOwnedSlice` and `allocPrint` hold a string at its
// length: a path built and kept (a join, a mount's path) keeps no growth's
// slack, half its length and a line again. Anything else is left as it is.
static inline void buf_fit(Buf *b) {
    if (b->cap > b->len && arena_is_last(b->arena, b->ptr, b->cap) && arena_resize(b->arena, b->ptr, b->cap, b->len)) b->cap = b->len;
}

static inline void buf_clear(Buf *b) {
    b->len = 0;
}

#endif
