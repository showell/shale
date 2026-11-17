// buf.h: a growable byte buffer in an arena, for building strings: where Zig
// Shale's `std.ArrayList(u8)` goes. It grows as vec.h's arrays do (Zig's
// policy), and every append either fits whole or returns STOP with the buffer
// as it was.

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
    // DRAGON: size overflow. `b->len + more` would wrap below `cap` and report room the buffer doesn't have, so the append's memcpy would run past it. test_buf's refused append plants it.
    if (more > SIZE_MAX - b->len) {
        b->arena->hit = true;
        return STOP;
    }
    void *p = b->ptr;
    Status s = vec_grow_raw(b->arena, &p, &b->cap, b->len, b->len + more, 1, 1);
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
    // DRAGON: use after free. `s` may be a view of this same buffer (`buf_str(b, buf_view(b))`, as a port of `s .. s` writes). The reserve moves the buffer, and past ARENA_BIG frees the old one to the system, so a copy from `s` after it would read freed memory. A view inside the buffer is kept as its offset and rebased after the reserve; test_buf appends 100000 bytes to themselves, and build.shale plants the rebase's loss.
    uintptr_t from = (uintptr_t)s.ptr;
    uintptr_t base = (uintptr_t)b->ptr;
    bool inside = b->ptr != NULL && from >= base && from < base + b->len;
    size_t offset = inside ? (size_t)(from - base) : 0;
    TRY(buf_reserve(b, s.len));
    if (inside) s = str_sub(((Str){b->ptr, b->len}), offset, offset + s.len);
    Bytes room = {b->ptr, b->cap};
    Bytes dest = SLICE_SUB(Bytes, room, b->len, b->len + s.len);
    memcpy(dest.ptr, s.ptr, s.len);
    b->len += s.len;
    return OK;
}

#define buf_lit(b, lit) buf_str((b), S(lit))

// `n` in decimal, as Zig's `{d}` prints it.
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

// What the buffer holds, valid until it next grows.
// DRAGON: use after free. A growth moves the bytes and frees a big buffer's old ones to the system, so a view kept across an append can dangle, not just go stale. Hold a view only until the buffer's next append.
static inline Str buf_view(const Buf *b) {
    Str s = {b->ptr, b->len};
    return s;
}

static inline void buf_clear(Buf *b) {
    b->len = 0;
}

#endif
