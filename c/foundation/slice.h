// slice.h: `Str` (a pointer and a length), typed slices, and the one way to
// index either. Every index goes through `at(s, i)`, which checks it against
// the length in every build: a bad index is a bug in the C, so it ends the
// process naming the file and line, never reading past the end. A loop that
// has proved its own bounds may opt out with `at_unchecked(s, i, "why")`,
// whose reason is a string literal the compiler requires.
//
// This is the only file allowed a raw `ptr[...]` index;
// `c/check_discipline.shale` holds the rest of the C to it.

#ifndef SHALE_SLICE_H
#define SHALE_SLICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Its bytes need not be UTF-8, and nothing relies on a NUL after them.
typedef struct {
    const char *ptr;
    size_t len;
} Str;

// A Str of a string literal; anything else is a compile error.
#define S(lit) ((Str){"" lit "", sizeof(lit) - 1})

// `S` as an item of a table sized by its items (`names[]`), a plain brace
// initializer: TCC 0.9.27 refuses an unsized array of compound literals,
// `S`'s form, with "index too large".
#define S_ITEM(lit) {"" lit "", sizeof(lit) - 1}

// A typed slice: `SLICE_TYPE(Ints, int64_t);` names a struct of `ptr` and
// `len`.
#define SLICE_TYPE(Name, T) \
    typedef struct {        \
        T *ptr;             \
        size_t len;         \
    } Name

SLICE_TYPE(Bytes, char);

// A bug in the C, never a script's stop: one line on stderr, `shale: a bug in
// the C: <what>`, and the process ends on SIGABRT (a death test's
// EXPECT_DIES). A script's mistake or a hostile input is a stop, with its
// message, and never comes here. TCC doesn't take `noreturn` as ending the
// function, so a caller that gives a value writes a return after the call,
// never reached.
static inline __attribute__((noreturn)) void c_bug(const char *what) { // cstr: a literal naming the bug
    fprintf(stderr, "shale: a bug in the C: %s\n", what);
    fflush(stderr);
    abort();
}

// `n - k`, a count of the C's own taken down: one below zero is a bug in the
// C, which Zig's checked arithmetic traps and C's unsigned arithmetic would
// wrap to a count near its type's most.
static inline uint32_t count_less_u32(uint32_t n, uint32_t k) {
    if (k > n) c_bug("a count of the C's own taken below zero");
    return n - k;
}

// c_bug, with what the bug was about after its words: `<what>: <detail>`.
static inline __attribute__((noreturn)) void c_bug_str(const char *what, Str detail) { // cstr: a literal naming the bug
    fprintf(stderr, "shale: a bug in the C: %s: %.*s\n", what, (int)detail.len, detail.len == 0 ? "" : detail.ptr);
    fflush(stderr);
    abort();
}

// What a bad index calls before the process ends. Null ends it at once; a
// test's child sets one that exits with a code of its own (`test.h`,
// `EXPECT_TRAPS`).
// The slot is per translation unit, which is all a test needs.
typedef void (*BoundsTrap)(const char *file, int line, uint64_t i, size_t len); // cstr: __FILE__

// DRAGON: bounds. A trap that returns still ends in abort, but one that
// longjmps resumes the program after a bad index, as if it had been in
// bounds.
// REMEDY: only test.h sets a trap, in a death test's child, and its trap
// calls `_exit`.
static inline BoundsTrap *slice_trap_slot(void) {
    static BoundsTrap trap;
    return &trap;
}

static inline void slice_bounds_failed(const char *file, int line, uint64_t i, size_t len) { // cstr: __FILE__
    BoundsTrap trap = *slice_trap_slot();
    if (trap != NULL) trap(file, line, i, len);
    fprintf(stderr, "%s:%d: index %llu is out of bounds for length %zu\n", file, line, (unsigned long long)i, len);
    abort();
}

// `i` is taken as 64 bits and compared with the length in 64 bits, so an
// int64_t index is never cut to a 32-bit size_t (the wasm build) before its
// check: one past 2^32 traps there as it does on x86-64.
static inline size_t slice_check(uint64_t i, size_t len, const char *file, int line) { // cstr: __FILE__
    if (i < (uint64_t)len) return (size_t)i;
    slice_bounds_failed(file, line, i, len);
    return 0;
}

// The element at `i`, an lvalue. `s` is evaluated twice, `i` once.
// DRAGON: bounds. This is every index's check, and a trap ends the process,
// not the run, so a script's bad index must never reach it.
// REMEDY: a negative index becomes a huge one and traps, never reading past
// the end; a script's index is checked first by its caller, as the `index`
// stop, and at() is the backstop.
#define at(s, i) ((s).ptr[slice_check((i), (s).len, __FILE__, __LINE__)])

// The element at `i` with no check. `reason` must be a non-empty string
// literal saying why the index is in bounds; it costs nothing at run time.
// DRAGON: bounds. No check at all. The reason is a comment the compiler
// insists on, and nothing holds it true.
// REMEDY: a search for `at_unchecked(` lists every use, each with its reason,
// and each is read by hand as a raw index. The uses are few: loops bounded
// by `len`, and one walk to the MSG_END that ends every list it walks.
#define at_unchecked(s, i, reason) \
    ((s).ptr[((void)sizeof(char[sizeof("" reason "") > 1 ? 1 : -1]), (i))])

// `from` and `to` checked against `len`, so a part can never reach past it.
static inline void slice_range_check(uint64_t from, uint64_t to, size_t len, const char *file, int line) { // cstr: __FILE__
    if (from <= to && to <= (uint64_t)len) return;
    slice_bounds_failed(file, line, from > to ? from : to, len);
}

// The part `[from, to)` of a typed slice, as the same type. A part from 0
// keeps `ptr` as it is: an empty slice may hold NULL, and `NULL + 0` is
// undefined in C11 (neither GCC's UBSan nor clang 22's flags it).
// DRAGON: bounds. `from` is evaluated four times and `to` twice, so an
// argument with a side effect would be checked as one value and used as
// another (`k++` with k = 2 on "abc" makes a part from 3 of length SIZE_MAX,
// and GCC says nothing).
// REMEDY: check_discipline's `slice args` rule refuses `++`, `--`, `=` and a
// call in its arguments; `str_sub`, a function, takes any argument.
#define SLICE_SUB(Type, s, from, to)                                            \
    (slice_range_check((from), (to), (s).len, __FILE__, __LINE__),              \
     (Type){(from) == 0 ? (s).ptr : (s).ptr + (from),                          \
            (size_t)(to) - (size_t)(from)})

// The part `[from, to)` of a Str. A function, so each argument is evaluated
// once; the macro only hands it the caller's place for a trap. A part from 0
// keeps `ptr`, as SLICE_SUB's does.
static inline Str str_sub_at(Str s, uint64_t from, uint64_t to, const char *file, int line) { // cstr: __FILE__
    slice_range_check(from, to, s.len, file, line);
    Str part = {from == 0 ? s.ptr : s.ptr + (size_t)from, (size_t)(to - from)};
    return part;
}

#define str_sub(s, from, to) str_sub_at((s), (from), (to), __FILE__, __LINE__)

static inline uint8_t str_byte(Str s, size_t i) {
    return (uint8_t)at(s, i);
}

// ── The mem* calls, guarded ───────────────────────────────────────────
// A mem* call on a NULL pointer is undefined in C17 even for 0 bytes, and
// glibc marks the arguments nonnull, so GCC may take the pointer as not NULL
// and delete a later check of it. An empty Str or slice may hold NULL, so
// no code outside c/foundation calls memcpy, memmove, memcmp or memchr
// (c/check_discipline.shale's `raw mem` rule): it calls these, which make no
// call for 0 bytes, and each raw call left in c/foundation has a DRAGON.

// Copies `n` bytes from `src` to `dst`, which do not overlap; nothing for 0.
static inline void mem_copy(void *dst, const void *src, size_t n) {
    if (n == 0) return;
    // DRAGON: NULL pointers. memcpy is undefined on a NULL pointer.
    // REMEDY: a call for 0 bytes returns above, and a pointer to one byte or
    // more is never NULL: the arena never hands out NULL for a length past
    // 0, and a slice or Str of a length past 0 points at its bytes.
    memcpy(dst, src, n);
}

// Copies `n` bytes from `src` to `dst`, which may overlap; nothing for 0.
static inline void mem_move(void *dst, const void *src, size_t n) {
    if (n == 0) return;
    // DRAGON: NULL pointers. memmove is undefined on a NULL pointer.
    // REMEDY: as mem_copy's, a call for 0 bytes returns above.
    memmove(dst, src, n);
}

// memcmp's order of `n` bytes at `a` and `b`; 0 for 0 bytes.
static inline int mem_compare(const void *a, const void *b, size_t n) {
    if (n == 0) return 0;
    // DRAGON: NULL pointers. memcmp is undefined on a NULL pointer.
    // REMEDY: as mem_copy's, a call for 0 bytes returns above.
    return memcmp(a, b, n);
}

// The first byte `c` in the `n` bytes at `p`, or NULL; NULL for 0 bytes.
static inline const char *mem_find_byte(const void *p, int c, size_t n) {
    if (n == 0) return NULL;
    // DRAGON: NULL pointers. memchr is undefined on a NULL pointer.
    // REMEDY: as mem_copy's, a call for 0 bytes returns above.
    return memchr(p, c, n);
}

static inline bool str_eq(Str a, Str b) {
    // DRAGON: NULL pointers. An empty Str may hold NULL, which memcmp may
    // not be handed even for 0 bytes.
    // REMEDY: the `len == 0` test makes no call for an empty Str, and a Str
    // of a length past 0 points at its bytes.
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

static inline bool str_starts_with(Str s, Str prefix) {
    // DRAGON: NULL pointers. As str_eq's.
    // REMEDY: an empty prefix makes no call, and `s` is at least as long as
    // a prefix past 0, so neither is NULL.
    return s.len >= prefix.len && (prefix.len == 0 || memcmp(s.ptr, prefix.ptr, prefix.len) == 0);
}

// The first `c` in `s`, or NULL when there is none. Every search of a Str's
// bytes for one byte comes here.
static inline const char *str_find_byte(Str s, char c) {
    // DRAGON: NULL pointers. memchr is undefined on a NULL pointer, and an
    // empty Str may hold NULL.
    // REMEDY: an empty Str makes no call; test_slice catches its plant.
    return s.len == 0 ? NULL : memchr(s.ptr, c, s.len);
}

#endif
