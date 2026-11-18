// zig_wire.c: the wire between Shale's host and zig-ast (a repo of its own,
// github.com/showell/zig-ast), the C side of that repo's wire.zig, which is
// the format's one home. Pure
// bytes and no system call. A frame is `<length>\n` and that many bytes; its
// body is lines of fields joined by a tab, each field escaped (`\\`, `\t`,
// `\n`, `\r`); a line `src<TAB><n>` is followed by n raw bytes and a newline,
// and a `tb<TAB><n>` line by n fixed-width token records.

#include "buf.h"

// The first line zig-ast writes, before its Zig version: `wire.header`.
#define ZIG_WIRE_HEADER "zig-ast serve 1 "

// The bytes of one token record in a `tb` block: `wire.token_bytes`.
enum { ZIG_WIRE_TOKEN_BYTES = 13 };

// The most fields one line of an answer holds: the host's `into`.
enum { ZIG_WIRE_FIELDS_MAX = 16 };

SLICE_TYPE(ZigWireKeywords, const Str);

// Whether `s` is a Zig identifier, `wire.isValidId`: a letter or `_`, then
// letters, digits and `_`, and no keyword (std.zig.Token.keywords).
static bool zig_wire_valid_id(Str s) {
    static const Str keywords_all[] = {
        S_ITEM("addrspace"), S_ITEM("align"), S_ITEM("allowzero"), S_ITEM("and"), S_ITEM("anyframe"),
        S_ITEM("anytype"), S_ITEM("asm"), S_ITEM("break"), S_ITEM("callconv"), S_ITEM("catch"),
        S_ITEM("comptime"), S_ITEM("const"), S_ITEM("continue"), S_ITEM("defer"), S_ITEM("else"),
        S_ITEM("enum"), S_ITEM("errdefer"), S_ITEM("error"), S_ITEM("export"), S_ITEM("extern"),
        S_ITEM("fn"), S_ITEM("for"), S_ITEM("if"), S_ITEM("inline"), S_ITEM("noalias"),
        S_ITEM("noinline"), S_ITEM("nosuspend"), S_ITEM("opaque"), S_ITEM("or"), S_ITEM("orelse"),
        S_ITEM("packed"), S_ITEM("pub"), S_ITEM("resume"), S_ITEM("return"), S_ITEM("linksection"),
        S_ITEM("struct"), S_ITEM("suspend"), S_ITEM("switch"), S_ITEM("test"), S_ITEM("threadlocal"),
        S_ITEM("try"), S_ITEM("union"), S_ITEM("unreachable"), S_ITEM("var"), S_ITEM("volatile"),
        S_ITEM("while"),
    };
    _Static_assert(sizeof keywords_all / sizeof *keywords_all == 46, "Zig has 46 keywords");
    ZigWireKeywords keywords = {keywords_all, sizeof keywords_all / sizeof *keywords_all};
    if (s.len == 0) return false;
    for (size_t i = 0; i < s.len; i++) {
        char c = at(s, i);
        bool letter = c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool digit = c >= '0' && c <= '9';
        if (!letter && !(digit && i > 0)) return false;
    }
    for (size_t k = 0; k < keywords.len; k++) {
        if (str_eq(at(keywords, k), s)) return false;
    }
    return true;
}

// `s` escaped as a field, `wire.field`.
static STATUS_MUST_USE Status zig_wire_field(Buf *w, Str s) {
    for (size_t i = 0; i < s.len; i++) {
        char c = at(s, i);
        if (c == '\\') {
            TRY(buf_lit(w, "\\\\"));
        } else if (c == '\t') {
            TRY(buf_lit(w, "\\t"));
        } else if (c == '\n') {
            TRY(buf_lit(w, "\\n"));
        } else if (c == '\r') {
            TRY(buf_lit(w, "\\r"));
        } else {
            TRY(buf_byte(w, c));
        }
    }
    return OK;
}

static STATUS_MUST_USE Status zig_wire_tab(Buf *w) {
    return buf_byte(w, '\t');
}

static STATUS_MUST_USE Status zig_wire_end(Buf *w) {
    return buf_byte(w, '\n');
}

// A source block, `wire.source`: `src<TAB><n>`, the bytes, a newline.
static STATUS_MUST_USE Status zig_wire_source(Buf *w, Str bytes) {
    TRY(buf_lit(w, "src\t"));
    TRY(buf_u64(w, bytes.len));
    TRY(buf_byte(w, '\n'));
    TRY(buf_str(w, bytes));
    return buf_byte(w, '\n');
}

// A field as a count or an offset, `wire.int`: Zig's `parseInt(usize, s,
// 10)`, an optional `+`, then digits with `_` between them (never first or
// last). False on anything else, or past 64 bits.
static bool zig_wire_int(Str s, size_t *out) {
    size_t from = s.len > 0 && at(s, 0) == '+' ? 1 : 0;
    Str digits = str_sub(s, from, s.len);
    if (digits.len == 0) return false;
    if (at(digits, 0) == '_' || at(digits, digits.len - 1) == '_') return false;
    uint64_t n = 0;
    for (size_t i = 0; i < digits.len; i++) {
        char c = at(digits, i);
        if (c == '_') continue;
        if (c < '0' || c > '9') return false;
        uint64_t d = (uint64_t)(c - '0');
        if (n > (UINT64_MAX - d) / 10) return false;
        n = n * 10 + d;
    }
    *out = (size_t)n;
    return true;
}

// How reading a line of an answer went: Zig's `Lines.next` and its errors.
typedef enum {
    ZIG_WIRE_LINE,
    ZIG_WIRE_DONE,
    ZIG_WIRE_BAD_FRAME,
    ZIG_WIRE_BAD_ESCAPE,
    ZIG_WIRE_OUT_OF_MEMORY,
} ZigWireRead;

// A field's text, its escapes undone, `wire.unescape`; a field with no
// escape is given back as it is, copied nowhere.
static ZigWireRead zig_wire_unescape(Arena *a, Str s, Str *out) {
    if (str_find_byte(s, '\\') == NULL) {
        *out = s;
        return ZIG_WIRE_LINE;
    }
    Buf b = buf_make(a);
    for (size_t i = 0; i < s.len; i++) {
        char c = at(s, i);
        if (c == '\\') {
            i++;
            if (i == s.len) return ZIG_WIRE_BAD_ESCAPE;
            char e = at(s, i);
            if (e == '\\') {
                c = '\\';
            } else if (e == 't') {
                c = '\t';
            } else if (e == 'n') {
                c = '\n';
            } else if (e == 'r') {
                c = '\r';
            } else {
                return ZIG_WIRE_BAD_ESCAPE;
            }
        }
        if (buf_byte(&b, c) != OK) return ZIG_WIRE_OUT_OF_MEMORY;
    }
    *out = buf_view(&b);
    return ZIG_WIRE_LINE;
}

// An answer's body, read a line at a time from `at`.
typedef struct {
    Str body;
    size_t at;
} ZigWireLines;

SLICE_TYPE(ZigWireFields, Str);

// The next line's fields into `into`, `*n` of them, `wire.Lines.next`: DONE
// at the body's end, BAD_FRAME for a line with no newline or more fields
// than `into` holds, BAD_ESCAPE for an escape the wire has not.
static ZigWireRead zig_wire_next(ZigWireLines *l, Arena *a, ZigWireFields into, size_t *n) {
    *n = 0;
    if (l->at >= l->body.len) return ZIG_WIRE_DONE;
    Str rest = str_sub(l->body, l->at, l->body.len);
    const char *nl = str_find_byte(rest, '\n');
    if (nl == NULL) return ZIG_WIRE_BAD_FRAME;
    size_t end = (size_t)(nl - rest.ptr);
    Str text = str_sub(rest, 0, end);
    l->at += end + 1;
    size_t from = 0;
    for (;;) {
        size_t to = from;
        while (to < text.len && at(text, to) != '\t') to++;
        if (*n == into.len) return ZIG_WIRE_BAD_FRAME;
        ZigWireRead got = zig_wire_unescape(a, str_sub(text, from, to), &at(into, *n));
        if (got != ZIG_WIRE_LINE) return got;
        (*n)++;
        if (to == text.len) break;
        from = to + 1;
    }
    return ZIG_WIRE_LINE;
}

// The `n` bytes of the block whose line came last, and the newline after
// it, `wire.Lines.block`; false when the body does not hold them.
static bool zig_wire_block(ZigWireLines *l, size_t n, Str *out) {
    size_t left = l->body.len - l->at;
    if (left < n || left - n < 1 || at(l->body, l->at + n) != '\n') return false;
    *out = str_sub(l->body, l->at, l->at + n);
    l->at += n + 1;
    return true;
}

// One token of a `tb` block, `wire.Token`.
typedef struct {
    uint8_t tag;
    uint32_t start;
    uint32_t end;
    uint32_t line;
} ZigWireToken;

static uint32_t zig_wire_u32(Str s, size_t from) {
    uint32_t v = 0;
    for (size_t k = 4; k > 0; k--) v = v << 8 | (uint8_t)at(s, from + k - 1);
    return v;
}

// The `i`th record of a block of whole records, `wire.readToken`:
// little-endian, its tag's number, its start, its end and its line.
static void zig_wire_token(Str block, size_t i, ZigWireToken *out) {
    size_t from = i * ZIG_WIRE_TOKEN_BYTES;
    out->tag = (uint8_t)at(block, from);
    out->start = zig_wire_u32(block, from + 1);
    out->end = zig_wire_u32(block, from + 5);
    out->line = zig_wire_u32(block, from + 9);
}
