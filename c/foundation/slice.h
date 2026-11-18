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
// no code outside c/foundation calls memcpy, memmove, memcmp, memchr or memmem
// (c/check_discipline.shale's `raw mem` rule): it calls these, which make no
// call for 0 bytes. Inside c/foundation too, these five are the only raw
// calls, each under its DRAGON.

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

// memmem is a GNU extension, declared only to a unit that defines
// _GNU_SOURCE before its first include, as every unit that searches for a
// run of bytes does (c/shale.c, c/tests/ran_tests.c, the host's tests).
#ifdef _GNU_SOURCE
// Where the `needle_len` bytes at `needle` first are in the `hay_len` bytes
// at `hay`, as an offset from `hay`, or SIZE_MAX when they are not there. An
// empty needle is at 0, in an empty haystack too, as memmem finds one; a
// needle longer than the haystack is nowhere. Either may be NULL when its
// length is 0.
static inline size_t mem_find(const void *hay, size_t hay_len, const void *needle, size_t needle_len) {
    if (needle_len == 0) return 0;
    if (needle_len > hay_len) return SIZE_MAX;
    // DRAGON: NULL pointers. memmem is undefined on a NULL pointer, and glibc
    // marks its arguments nonnull.
    // REMEDY: both lengths are past 0 here, as mem_copy's are, so neither
    // pointer is NULL.
    const char *hit = memmem(hay, hay_len, needle, needle_len);
    return hit == NULL ? SIZE_MAX : (size_t)(hit - (const char *)hay);
}
#endif

static inline bool str_eq(Str a, Str b) {
    return a.len == b.len && mem_compare(a.ptr, b.ptr, a.len) == 0;
}

static inline bool str_starts_with(Str s, Str prefix) {
    return s.len >= prefix.len && mem_compare(s.ptr, prefix.ptr, prefix.len) == 0;
}

// The first `c` in `s`, or NULL when there is none. Every search of a Str's
// bytes for one byte comes here.
static inline const char *str_find_byte(Str s, char c) {
    return mem_find_byte(s.ptr, c, s.len);
}

// ── OS strings, for the kernel ────────────────────────────────────────

// A Str known to hold no NUL: the only string the C hands the kernel (a
// path, a program's argument, its folder, an environment entry). Only the
// two makers below make one, so a call that takes an OsStr was handed bytes
// the kernel will read whole.
typedef struct {
    Str s;
} OsStr;

// `s` as an OsStr into `*out`, or false when it holds a NUL. The one search
// for a NUL in a string bound for the kernel: a script's string comes here
// through the host's `os_str` (host_run.c), which refuses it naming the
// argument, and a config file's through its parser (mounts.c's
// `mounts_parse`), which names the line.
// DRAGON: a NUL inside. A NUL inside `s` would end the kernel's copy early,
// so the call would act on a shorter path or word than the one every check
// judged (`a\0/../x` checked as one, opened as `a`).
// REMEDY: an OsStr is made only here and by os_str_own, which both search
// for one; test_slice holds both sides, and test_run_guards plants a
// script's NUL in each of `run`'s arguments.
static inline bool os_str_of(Str s, OsStr *out) {
    if (str_find_byte(s, 0) != NULL) return false; // nul: the maker's search
    out->s = s;
    return true;
}

// Shale's own string as an OsStr: one built of others' OsStr parts and
// fixed words, or a name the kernel gave (a folder's entry, `realpath`'s
// answer), which hold no NUL by how they were made. A NUL in one is a bug
// in the C.
static inline OsStr os_str_own(Str s) {
    OsStr o;
    if (!os_str_of(s, &o)) c_bug("an OS string of Shale's own holds a NUL");
    return o;
}

// `s` with a NUL after it, written at the start of `room`, for a call into
// the kernel; false, with nothing written, when it and its NUL don't fit.
// Every C string the C makes is made here: arena.h's arena_cstr in an
// arena, file_system.c's file_system_cstr on the caller's stack.
static inline bool str_cstr_into(Bytes room, OsStr s) {
    if (s.s.len >= room.len) return false;
    mem_copy(room.ptr, s.s.ptr, s.s.len);
    at(room, s.s.len) = 0;
    return true;
}

#endif
