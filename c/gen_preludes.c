// gen_preludes.c: writes c/out/gen/preludes.h, the three preludes the C
// `shale` embeds (the core's, the Linux one and the git one), each a C string
// literal of its source's exact bytes, from c/preludes/. c/build.shale runs it
// before it compiles the command; the header is the build's, never tracked.
// It needs no `shale`, so a new machine's first build starts here (c/docs/
// building.md, "First install on a new machine").
//
// Run from the repository's root: tcc -run c/gen_preludes.c
// (or compile it with gcc and run that).

#include <errno.h>
#include <sys/stat.h>

#include "foundation/buf.h"

enum {
    // Far past the three preludes' bytes and their escapes.
    GEN_ARENA_LIMIT = 1 << 26,
    // The bytes a read asks for at a time.
    GEN_READ_CHUNK = 1 << 16,
};

typedef struct {
    Str fn;
    Str path;
} Source;

SLICE_TYPE(Sources, const Source);

// The preludes, each with its function's name in the header.
static const Source gen_sources[] = {
    {S_ITEM("preludes_core"), S_ITEM("c/preludes/core.shale")},
    {S_ITEM("preludes_linux"), S_ITEM("c/preludes/linux.shale")},
    {S_ITEM("preludes_git"), S_ITEM("c/preludes/git.shale")},
};

// The run's end on a failure: one line naming the step, the path and why.
static int gen_failed(const char *what, Str path) { // cstr: a literal naming the step
    fprintf(stderr, "gen_preludes: %s %.*s: %s\n", what, (int)path.len, path.len == 0 ? "" : path.ptr, strerror(errno));
    return 1;
}

// The whole file at `path` appended to `out`.
static STATUS_MUST_USE Status gen_read(Arena *a, Str path, Buf *out) {
    const char *c = arena_cstr(a, path); // cstr: the kernel's
    if (c == NULL) return STOP;
    FILE *f = fopen(c, "rb");
    if (f == NULL) return STOP;
    bool fits = true;
    while (true) {
        if (buf_reserve(out, GEN_READ_CHUNK) != OK) {
            fits = false;
            break;
        }
        Bytes room = {out->ptr, out->cap};
        size_t got = fread(&at(room, out->len), 1, out->cap - out->len, f);
        out->len += got;
        if (got == 0) break;
    }
    bool bad = ferror(f) != 0;
    fclose(f);
    return fits && !bad ? OK : STOP;
}

// One line of source as a C literal, its newline among it when it has one: a
// byte below ` ` or past `~` as a three-digit octal escape, which never runs
// into the next byte as a hexadecimal one can, and `?` escaped against
// trigraphs.
static STATUS_MUST_USE Status gen_line(Buf *b, Str line, bool newline) {
    TRY(buf_lit(b, "    \""));
    for (size_t i = 0; i < line.len; i++) {
        uint8_t c = str_byte(line, i);
        if (c == '\\') {
            TRY(buf_lit(b, "\\\\"));
        } else if (c == '"') {
            TRY(buf_lit(b, "\\\""));
        } else if (c == '?') {
            TRY(buf_lit(b, "\\?"));
        } else if (c >= ' ' && c <= '~') {
            TRY(buf_byte(b, (char)c));
        } else {
            TRY(buf_byte(b, '\\'));
            TRY(buf_byte(b, (char)('0' + (c >> 6))));
            TRY(buf_byte(b, (char)('0' + ((c >> 3) & 7))));
            TRY(buf_byte(b, (char)('0' + (c & 7))));
        }
    }
    if (newline) TRY(buf_lit(b, "\\n"));
    return buf_lit(b, "\"\n");
}

// A prelude's text as its function: every line ended by a newline a literal,
// and a last one without its newline when the file does not end in one.
static STATUS_MUST_USE Status gen_prelude(Buf *b, Str fn, Str text) {
    TRY(buf_lit(b, "static inline Str "));
    TRY(buf_str(b, fn));
    TRY(buf_lit(b, "(void) {\n    static const char text[] =\n"));
    Str rest = text;
    while (rest.len > 0) {
        const char *nl = str_find_byte(rest, '\n');
        if (nl == NULL) {
            TRY(gen_line(b, rest, false));
            break;
        }
        size_t n = (size_t)(nl - rest.ptr);
        TRY(gen_line(b, str_sub(rest, 0, n), true));
        rest = str_sub(rest, n + 1, rest.len);
    }
    return buf_lit(b, "    ;\n    Str s = {text, sizeof text - 1};\n    return s;\n}\n\n");
}

int main(void) {
    bool refused = false;
    Arena a;
    arena_init(&a, GEN_ARENA_LIMIT, &refused);
    Buf h = buf_make(&a);
    Sources all = {gen_sources, sizeof gen_sources / sizeof *gen_sources};
    if (buf_lit(&h, "// preludes.h: written by c/gen_preludes.c from c/preludes/; never edit it.\n\n#ifndef SHALE_PRELUDES_H\n#define SHALE_PRELUDES_H\n\n#include \"../../foundation/slice.h\"\n\n") != OK) return gen_failed("out of memory for", S("the header"));
    for (size_t k = 0; k < all.len; k++) {
        const Source *s = &at(all, k);
        Buf text = buf_make(&a);
        if (gen_read(&a, s->path, &text) != OK) return gen_failed("cannot read", s->path);
        if (gen_prelude(&h, s->fn, buf_view(&text)) != OK) return gen_failed("out of memory for", s->path);
    }
    if (buf_lit(&h, "#endif\n") != OK) return gen_failed("out of memory for", S("the header"));
    if (mkdir("c/out", 0777) != 0 && errno != EEXIST) return gen_failed("cannot make", S("c/out"));
    if (mkdir("c/out/gen", 0777) != 0 && errno != EEXIST) return gen_failed("cannot make", S("c/out/gen"));
    FILE *out = fopen("c/out/gen/preludes.h", "wb");
    if (out == NULL) return gen_failed("cannot write", S("c/out/gen/preludes.h"));
    Str whole = buf_view(&h);
    bool wrote = fwrite(whole.ptr, 1, whole.len, out) == whole.len;
    if (fclose(out) != 0 || !wrote) return gen_failed("cannot finish", S("c/out/gen/preludes.h"));
    printf("preludes.h: %zu preludes\n", all.len);
    arena_deinit(&a);
    return 0;
}
