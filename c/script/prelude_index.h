// prelude_index.h: a prelude's and a library's index by their lines: where each
// `function name(` and each `const NAME` begins, found from the start of its
// line without a parse, so that a function is parsed only at its first call. It
// parses nothing, and parse.h includes it first.

#ifndef SHALE_PRELUDE_INDEX_H
#define SHALE_PRELUDE_INDEX_H

#include "../foundation/vec.h"

// Where a prelude's function (or a library's `const`) begins: the start of
// its line.
typedef struct {
    Str name;
    size_t at;
    uint32_t line;
} PreludeFunction;

SLICE_TYPE(PreludeFunctions, PreludeFunction);
VEC_DEFINE(PreludeFunctionVec, PreludeFunction)

// A prelude after the core's: its name as a stop shows it ("the Linux
// prelude"), and its source, which holds only functions, each from the start
// of a line.
typedef struct {
    Str name;
    Str source;
} Prelude;

SLICE_TYPE(Preludes, const Prelude);

// ── the preludes' indexes ──

// The next line from `*at` on that begins `function name(`; false at the end.
static inline bool parse_next_function(Str src, size_t *at_, uint32_t *line, PreludeFunction *out) {
    while (*at_ < src.len) {
        size_t start = *at_;
        Str rest_of = str_sub(src, start, src.len);
        const char *nl = memchr(rest_of.ptr, '\n', rest_of.len);
        size_t end = nl != NULL ? (size_t)(nl - src.ptr) : src.len;
        Str text = str_sub(src, start, end);
        uint32_t this_line = *line;
        *at_ = end + 1;
        (*line)++;
        if (!str_starts_with(text, S("function "))) continue;
        const char *paren = memchr(text.ptr, '(', text.len);
        if (paren == NULL) continue;
        Str name = str_sub(text, 9, (size_t)(paren - text.ptr));
        size_t a = 0;
        size_t b = name.len;
        while (a < b && str_byte(name, a) == ' ') a++;
        while (b > a && str_byte(name, b - 1) == ' ') b--;
        PreludeFunction f = {str_sub(name, a, b), start, this_line};
        *out = f;
        return true;
    }
    return false;
}

// The next line that begins `const NAME`: a library's `const`s.
static inline bool parse_next_const(Str src, size_t *at_, uint32_t *line, PreludeFunction *out) {
    while (*at_ < src.len) {
        size_t start = *at_;
        Str rest_of = str_sub(src, start, src.len);
        const char *nl = memchr(rest_of.ptr, '\n', rest_of.len);
        size_t end = nl != NULL ? (size_t)(nl - src.ptr) : src.len;
        Str text = str_sub(src, start, end);
        uint32_t this_line = *line;
        *at_ = end + 1;
        (*line)++;
        if (!str_starts_with(text, S("const "))) continue;
        Str rest = str_sub(text, 6, text.len);
        size_t stop = 0;
        while (stop < rest.len && str_byte(rest, stop) != ' ' && str_byte(rest, stop) != '=') stop++;
        PreludeFunction c = {str_sub(rest, 0, stop), start, this_line};
        *out = c;
        return true;
    }
    return false;
}

typedef bool (*ParseNextEntry)(Str src, size_t *at_, uint32_t *line, PreludeFunction *out);

// Every function (or `const`) a source holds by its lines.
static inline STATUS_MUST_USE Status parse_index(Arena *a, Str src, ParseNextEntry next, PreludeFunctions *out) {
    PreludeFunctionVec v = PreludeFunctionVec_make(a);
    size_t at_ = 0;
    uint32_t line = 1;
    PreludeFunction f;
    while (next(src, &at_, &line, &f)) TRY(PreludeFunctionVec_push(&v, f));
    PreludeFunctions s = {v.ptr, v.len};
    *out = s;
    return OK;
}

// The place of `name` in `index`, or -1.
static inline int32_t parse_index_at(PreludeFunctions index, Str name) {
    for (size_t k = 0; k < index.len; k++) {
        if (str_eq(at(index, k).name, name)) return (int32_t)k;
    }
    return -1;
}

static inline int32_t parse_index_before(PreludeFunctions index, size_t n, Str name) {
    for (size_t k = 0; k < n && k < index.len; k++) {
        if (str_eq(at(index, k).name, name)) return (int32_t)k;
    }
    return -1;
}

#endif
