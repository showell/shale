// slice.h: `Str` (a pointer and a length), typed slices, and the one way to
// index either. Every index goes through `at(s, i)`, which checks it against
// the length in every build: a bad index is a bug in the C, so it ends the
// process naming the file and line, never reading past the end. A loop that
// has proved its own bounds may opt out with `at_unchecked(s, i, "why")`,
// whose reason is a string literal the compiler requires.
//
// This is the only file allowed a raw `ptr[...]` index; `c/check_discipline.shale`
// holds the rest of the C to it.

#ifndef SHALE_SLICE_H
#define SHALE_SLICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Bytes, not necessarily UTF-8, and never NUL-terminated by contract.
typedef struct {
    const char *ptr;
    size_t len;
} Str;

// A Str of a string literal; anything else is a compile error.
#define S(lit) ((Str){"" lit "", sizeof(lit) - 1})

// A typed slice: `SLICE_TYPE(Ints, int64_t);` names a struct of `ptr` and `len`.
#define SLICE_TYPE(Name, T) \
    typedef struct {        \
        T *ptr;             \
        size_t len;         \
    } Name

SLICE_TYPE(Bytes, char);

// What a bad index calls before the process ends. Null ends it at once; a
// test's child sets one that exits with a code of its own (`test.h`,
// `EXPECT_TRAPS`).
// The slot is per translation unit, which is all a test needs.
typedef void (*BoundsTrap)(const char *file, int line, size_t i, size_t len); // cstr: __FILE__

static inline BoundsTrap *slice_trap_slot(void) {
    static BoundsTrap trap;
    return &trap;
}

static inline void slice_bounds_failed(const char *file, int line, size_t i, size_t len) { // cstr: __FILE__
    BoundsTrap trap = *slice_trap_slot();
    if (trap != NULL) trap(file, line, i, len);
    fprintf(stderr, "%s:%d: index %zu is out of bounds for length %zu\n", file, line, i, len);
    abort();
}

static inline size_t slice_check(size_t i, size_t len, const char *file, int line) { // cstr: __FILE__
    if (i < len) return i;
    slice_bounds_failed(file, line, i, len);
    return 0;
}

// The element at `i`, an lvalue. `s` is evaluated twice, `i` once.
#define at(s, i) ((s).ptr[slice_check((i), (s).len, __FILE__, __LINE__)])

// The element at `i` with no check. `reason` must be a non-empty string
// literal saying why the index is in bounds; it costs nothing at run time.
#define at_unchecked(s, i, reason) \
    ((s).ptr[((void)sizeof(char[sizeof("" reason "") > 1 ? 1 : -1]), (i))])

// `from` and `to` checked against `len`, so a part can never reach past it.
static inline void slice_range_check(size_t from, size_t to, size_t len, const char *file, int line) { // cstr: __FILE__
    if (from <= to && to <= len) return;
    slice_bounds_failed(file, line, from > to ? from : to, len);
}

// The part `[from, to)` of a typed slice, as the same type.
#define SLICE_SUB(Type, s, from, to)                                            \
    (slice_range_check((from), (to), (s).len, __FILE__, __LINE__),              \
     (Type){(s).ptr + (from), (size_t)(to) - (size_t)(from)})

// The part `[from, to)` of a Str.
#define str_sub(s, from, to) SLICE_SUB(Str, s, from, to)

static inline uint8_t str_byte(Str s, size_t i) {
    return (uint8_t)at(s, i);
}

static inline bool str_eq(Str a, Str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

static inline bool str_starts_with(Str s, Str prefix) {
    return s.len >= prefix.len && (prefix.len == 0 || memcmp(s.ptr, prefix.ptr, prefix.len) == 0);
}

#endif
