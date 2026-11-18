// wyhash.h: Wyhash, the 64-bit hash of Zig's `std.hash.Wyhash`, its
// one-shot form, step for step. path.c names a replace's temporary file by
// it, so the name is the one Zig Shale gave.

#ifndef SHALE_WYHASH_H
#define SHALE_WYHASH_H

#include <stdint.h>

#include "slice.h"

// The 128-bit product of `*a` and `*b`, its low half to `*a` and its high
// to `*b`, in 64-bit halves: TCC has no 128-bit integer.
static inline void wyhash_mum(uint64_t *a, uint64_t *b) {
    uint64_t x = *a;
    uint64_t y = *b;
    uint64_t xl = x & 0xffffffffu;
    uint64_t xh = x >> 32;
    uint64_t yl = y & 0xffffffffu;
    uint64_t yh = y >> 32;
    uint64_t ll = xl * yl;
    uint64_t lh = xl * yh;
    uint64_t hl = xh * yl;
    uint64_t hh = xh * yh;
    uint64_t mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
    *a = (ll & 0xffffffffu) | (mid << 32);
    *b = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

static inline uint64_t wyhash_mix(uint64_t a, uint64_t b) {
    wyhash_mum(&a, &b);
    return a ^ b;
}

// `bytes` (4 or 8) little-endian bytes of `s` from `from`.
static inline uint64_t wyhash_read(Str s, size_t from, size_t bytes) {
    uint64_t v = 0;
    for (size_t k = bytes; k > 0; k--) v = (v << 8) | str_byte(s, from + k - 1);
    return v;
}

// Zig's `std.hash.Wyhash.hash(seed, input)`.
static inline uint64_t wyhash(uint64_t seed, Str in) {
    static const uint64_t secret_words[4] = {0xa0761d6478bd642full, 0xe7037ed1a0b428dbull, 0x8ebc6af09c88c6e3ull, 0x589965cc75374cc3ull};
    SLICE_TYPE(Words, const uint64_t);
    Words secret = {secret_words, 4};
    uint64_t state_words[3];
    SLICE_TYPE(State, uint64_t);
    State state = {state_words, 3};
    at(state, 0) = seed ^ wyhash_mix(seed ^ at(secret, 0), at(secret, 1));
    at(state, 1) = at(state, 0);
    at(state, 2) = at(state, 0);
    uint64_t a = 0;
    uint64_t b = 0;
    if (in.len <= 16) {
        if (in.len >= 4) {
            size_t end = in.len - 4;
            size_t quarter = (in.len >> 3) << 2;
            a = (wyhash_read(in, 0, 4) << 32) | wyhash_read(in, quarter, 4);
            b = (wyhash_read(in, end, 4) << 32) | wyhash_read(in, end - quarter, 4);
        } else if (in.len > 0) {
            a = ((uint64_t)str_byte(in, 0) << 16) | ((uint64_t)str_byte(in, in.len >> 1) << 8) | str_byte(in, in.len - 1);
        }
    } else {
        size_t i = 0;
        if (in.len >= 48) {
            while (i + 48 < in.len) {
                for (size_t r = 0; r < 3; r++) {
                    uint64_t x = wyhash_read(in, i + 16 * r, 8);
                    uint64_t y = wyhash_read(in, i + 16 * r + 8, 8);
                    at(state, r) = wyhash_mix(x ^ at(secret, r + 1), y ^ at(state, r));
                }
                i += 48;
            }
            at(state, 0) ^= at(state, 1) ^ at(state, 2);
        }
        Str rest = str_sub(in, i, in.len);
        size_t j = 0;
        while (j + 16 < rest.len) {
            at(state, 0) = wyhash_mix(wyhash_read(rest, j, 8) ^ at(secret, 1), wyhash_read(rest, j + 8, 8) ^ at(state, 0));
            j += 16;
        }
        a = wyhash_read(in, in.len - 16, 8);
        b = wyhash_read(in, in.len - 8, 8);
    }
    a ^= at(secret, 1);
    b ^= at(state, 0);
    wyhash_mum(&a, &b);
    return wyhash_mix(a ^ at(secret, 0) ^ (uint64_t)in.len, b ^ at(secret, 1));
}

#endif
