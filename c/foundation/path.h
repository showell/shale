// path.h: a path's algebra and the strict name rule, with no call into
// the operating system: a path's last part, its folder and a join, as Zig's
// std.fs.path spells them; the names Shale holds every file to
// (`path_strict_name`); and the temporary name a replace writes beside its
// file (`path_temp_name`). c/host/file_system.c makes the calls. This is
// the strict name rule's one home: the CLI's library reader and `is_name`
// take it from here too.

#ifndef SHALE_PATH_H
#define SHALE_PATH_H

#include "buf.h"
#include "wyhash.h"

// The longest strict name.
enum {
    PATH_STRICT_NAME_MAX = 96,
};

// Whether `name` is a strict name, one Shale may make: 1 to 96 printable
// ASCII bytes, none of `" * / : < > ? \ |`, not `.` or `..`, not ending in
// a dot or a space. Stricter than Linux on purpose: a name that is one
// everywhere.
static inline bool path_strict_name(Str name) {
    if (name.len == 0 || name.len > PATH_STRICT_NAME_MAX) return false;
    if (str_eq(name, S(".")) || str_eq(name, S(".."))) return false;
    for (size_t i = 0; i < name.len; i++) {
        uint8_t c = str_byte(name, i);
        if (c < 0x20 || c > 0x7E) return false;
        if (mem_find_byte("\"*/:<>?\\|", c, 9) != NULL) return false;
    }
    uint8_t last = str_byte(name, name.len - 1);
    return last != '.' && last != ' ';
}

// The last part's start and end in `p`, trailing slashes passed by: Zig's
// component iterator's `last`. False when `p` has no part.
static inline bool path_last_part(Str p, size_t *from, size_t *to) {
    size_t end = p.len;
    while (end > 0 && str_byte(p, end - 1) == '/') end--;
    if (end == 0) return false;
    size_t start = end;
    while (start > 0 && str_byte(p, start - 1) != '/') start--;
    *from = start;
    *to = end;
    return true;
}

// `p`'s last part, "" when there is none.
static inline Str path_basename(Str p) {
    size_t from = 0;
    size_t to = 0;
    if (!path_last_part(p, &from, &to)) return S("");
    return str_sub(p, from, to);
}

// Zig's `dirnamePosix`: `p` up to the end of the part before the last; the
// root `/` for an absolute path with one part; false for none.
static inline bool path_dirname(Str p, Str *out) {
    size_t from = 0;
    size_t to = 0;
    if (!path_last_part(p, &from, &to)) return false;
    size_t end = from;
    while (end > 0 && str_byte(p, end - 1) == '/') end--;
    if (end == 0) {
        if (from > 0) {
            *out = str_sub(p, 0, 1);
            return true;
        }
        return false;
    }
    *out = str_sub(p, 0, end);
    return true;
}

// `dir` and `name` joined by one `/`, as Zig's `path.join` of two parts
// does when `name` has no leading slash.
static inline bool path_join(Arena *a, Str dir, Str name, Str *out) {
    Buf b = buf_make(a);
    bool slash = dir.len > 0 && str_byte(dir, dir.len - 1) != '/';
    if (buf_str(&b, dir) != OK) return false;
    if (slash && buf_byte(&b, '/') != OK) return false;
    if (buf_str(&b, name) != OK) return false;
    buf_fit(&b);
    *out = buf_view(&b);
    return true;
}

// The temporary name `replace` writes beside `name`: `~`, eight lowercase
// hex digits of the low half of a Wyhash of the name in lowercase (its
// first PATH_STRICT_NAME_MAX bytes), then `.tmp`. Thirteen bytes whatever
// the name, and one per name in any case.
static inline bool path_temp_name(Arena *a, Str name, Str *out) {
    char lower[PATH_STRICT_NAME_MAX];
    Bytes low = {lower, sizeof lower};
    size_t n = name.len < PATH_STRICT_NAME_MAX ? name.len : PATH_STRICT_NAME_MAX;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = str_byte(name, i);
        at(low, i) = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    uint32_t h = (uint32_t)wyhash(0, (Str){lower, n});
    char *p = arena_alloc(a, 13, 1);
    if (p == NULL) return false;
    Bytes t = {p, 13};
    at(t, 0) = '~';
    for (size_t k = 0; k < 8; k++) at(t, 1 + k) = "0123456789abcdef"[(h >> (28 - 4 * k)) & 0xf];
    mem_copy(&at(t, 9), ".tmp", 4);
    out->ptr = p;
    out->len = 13;
    return true;
}

#endif
