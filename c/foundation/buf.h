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
static inline Status buf_reserve(Buf *b, size_t more) {
    if (more > SIZE_MAX - b->len) {
        b->arena->hit = true;
        return STOP;
    }
    void *p = b->ptr;
    Status s = vec_grow_raw(b->arena, &p, &b->cap, b->len, b->len + more, 1, 1);
    b->ptr = p;
    return s;
}

static inline Status buf_byte(Buf *b, char c) {
    TRY(buf_reserve(b, 1));
    Bytes room = {b->ptr, b->cap};
    at(room, b->len) = c;
    b->len++;
    return OK;
}

static inline Status buf_str(Buf *b, Str s) {
    if (s.len == 0) return OK;
    TRY(buf_reserve(b, s.len));
    memcpy(b->ptr + b->len, s.ptr, s.len);
    b->len += s.len;
    return OK;
}

#define buf_lit(b, lit) buf_str((b), S(lit))

// `n` in decimal, as Zig's `{d}` prints it.
static inline Status buf_u64(Buf *b, uint64_t n) {
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

static inline Status buf_i64(Buf *b, int64_t n) {
    if (n >= 0) return buf_u64(b, (uint64_t)n);
    TRY(buf_byte(b, '-'));
    // -(n + 1) + 1 never overflows, INT64_MIN among them.
    return buf_u64(b, (uint64_t)(-(n + 1)) + 1);
}

// What the buffer holds, valid until it next grows.
static inline Str buf_view(const Buf *b) {
    Str s = {b->ptr, b->len};
    return s;
}

static inline void buf_clear(Buf *b) {
    b->len = 0;
}

#endif
