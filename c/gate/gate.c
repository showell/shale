// gate.c: shale-gate's decision, the PreToolUse hook on Bash and Monitor that
// holds Claude to Shale, a port of tools/shale_gate.zig that answers as it
// does. It reads the hook's JSON and passes (exit 0) exactly:
//
//   - the launch: optionally `cd WORD && `, then `shale` (or an absolute path
//     ending `/shale`) and its words (`--verbose`, `--undo-on-stop`,
//     `--args-stdin`, `--budget N=V`, a FILE, `NAME=VALUE`), each plain (no
//     quotes, `$`, backticks, `;|&<>()*?`, `\`), and optionally `<<'TAG'`
//     ending the first line (TAG `[A-Z_]+`), a body, and a last line that is
//     `TAG` (trailing spaces allowed) and that no other line is;
//   - the escape: a first line `# want: <tag>: <text>` or
//     `# out-of-scope: <tag>: <text>` (tag `[a-z0-9-]+`), run and logged to
//     `~/.local/state/shale/wants.tsv` with one append;
//   - any tool but Bash and Monitor (the hook's matcher names those two).
//
// Anything else is refused (exit 2), with one line naming both forms. The
// hook's JSON is read as Zig's std.json reads it into a `Value`: RFC 8259
// with nothing added, UTF-8 checked in every string, a `\u` surrogate half
// alone refused, and a key twice in any object refused. c/shale_gate.c is
// the program; tools/shale-gate.sh runs it and turns any other failure, a
// missing binary included, into a refusal.

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "buf.h"

// The two forms, as every refusal names them.
#define GATE_FORMS "a launch, `shale [--verbose] [--undo-on-stop] [FILE [--args-stdin]] [NAME=VALUE]... <<'TAG'` (after `cd DIR && ` if wanted; the script on stdin, the last line `TAG`), or the escape, a first line `# want: <tag>: <text>` or `# out-of-scope: <tag>: <text>` (Shale's docs: README.md, then docs/SHALE-AGENTS.md, in the shale repo, ~/showell_repos/shale on this box; its docs and this box's mounts: `shale --help`)"

enum {
    // Zig's refusal is formatted into a buffer of this many bytes, and one
    // that won't fit is the plain refusal instead.
    GATE_WHY_BYTES = 1024,
    // The first line's words and a heredoc's lines, Zig's fixed arrays.
    GATE_WORDS_MOST = 64,
    GATE_LINES_MOST = 4096,
    // A log line's bytes before its newline, and the parts of a field kept.
    GATE_LOG_BYTES = 4095,
    GATE_LOG_TEXT_MOST = 2048,
    GATE_LOG_COMMAND_MOST = 160,
    // The most of the hook's input read; one byte more is refused.
    GATE_INPUT_MOST = 1 << 20,
};

typedef enum { GATE_LAUNCH, GATE_ESCAPE, GATE_REFUSED } GateKind;

typedef struct {
    GateKind kind;
    // An escape's kind (`want` or `out-of-scope`), tag and text.
    Str escape_kind;
    Str tag;
    Str text;
    // A refusal's line, `shale-gate: ...`.
    Str why;
} GateVerdict;

SLICE_TYPE(GateStrs, Str);

// ── The command's grammar ────────────────────────────────────────────────

// A word the shell takes as it is: nothing it quotes, expands, globs,
// redirects or chains on.
static inline bool gate_plain(Str w) {
    if (w.len == 0) return false;
    for (size_t i = 0; i < w.len; i++) {
        switch (str_byte(w, i)) {
        case '\'': case '"': case '$': case '`': case ';': case '|': case '&': case '<':
        case '>': case '(': case ')': case '*': case '?': case '\\': case '\t': case '\r':
            return false;
        default:
            break;
        }
    }
    return true;
}

static inline bool gate_upper_tag(Str s) {
    if (s.len == 0) return false;
    for (size_t i = 0; i < s.len; i++) {
        uint8_t c = str_byte(s, i);
        if (!((c >= 'A' && c <= 'Z') || c == '_')) return false;
    }
    return true;
}

static inline bool gate_ends_with(Str s, Str suffix) {
    return s.len >= suffix.len && str_eq(str_sub(s, s.len - suffix.len, s.len), suffix);
}

// `s` without the bytes of `set` at its end.
static inline Str gate_trim_end(Str s, Str set) {
    size_t end = s.len;
    while (end > 0 && str_find_byte(set, (char)str_byte(s, end - 1)) != NULL) end--;
    return str_sub(s, 0, end);
}

// `s` without the bytes of `set` at either end.
static inline Str gate_trim(Str s, Str set) {
    Str t = gate_trim_end(s, set);
    size_t from = 0;
    while (from < t.len && str_find_byte(set, (char)str_byte(t, from)) != NULL) from++;
    return str_sub(t, from, t.len);
}

// Where `needle` first is in `s`, or SIZE_MAX.
static inline size_t gate_index_of(Str s, Str needle) {
    if (needle.len > s.len) return SIZE_MAX;
    for (size_t i = 0; i + needle.len <= s.len; i++) {
        if (str_eq(str_sub(s, i, i + needle.len), needle)) return i;
    }
    return SIZE_MAX;
}

// The escape of `kind` that `first` is, filling `v`, or false.
static inline bool gate_escape_of(Str first, Str kind, GateVerdict *v) {
    if (!str_starts_with(first, S("# "))) return false;
    Str after = str_sub(first, 2, first.len);
    if (!str_starts_with(after, kind)) return false;
    if (!str_starts_with(str_sub(after, kind.len, after.len), S(": "))) return false;
    Str body = str_sub(after, kind.len + 2, after.len);
    size_t colon = gate_index_of(body, S(": "));
    if (colon == SIZE_MAX) return false;
    Str tag = str_sub(body, 0, colon);
    if (tag.len == 0) return false;
    for (size_t i = 0; i < tag.len; i++) {
        uint8_t c = str_byte(tag, i);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    Str text = gate_trim(str_sub(body, colon + 2, body.len), S(" \t\r"));
    if (text.len == 0) return false;
    v->kind = GATE_ESCAPE;
    v->escape_kind = kind;
    v->tag = tag;
    v->text = text;
    return true;
}

// `v` refused: `shale-gate: <before><word><after>only <the forms>`, or the
// plain refusal when that passes GATE_WHY_BYTES or the arena refuses it.
static inline void gate_refuse(Arena *a, GateVerdict *v, Str before, Str word, Str after) {
    v->kind = GATE_REFUSED;
    v->why = S("shale-gate: refused: only " GATE_FORMS);
    Buf b = buf_make(a);
    if (buf_lit(&b, "shale-gate: ") != OK) return;
    if (buf_str(&b, before) != OK) return;
    if (buf_str(&b, word) != OK) return;
    if (buf_str(&b, after) != OK) return;
    if (buf_lit(&b, "only " GATE_FORMS) != OK) return;
    if (b.len <= GATE_WHY_BYTES) v->why = buf_view(&b);
}

static inline void gate_refuse_plain(Arena *a, GateVerdict *v, Str why) {
    gate_refuse(a, v, why, S(""), S(""));
}

// A launch's words after `shale`, from `from`, each plain and each an option
// the gate passes; false when it refused `v`.
static inline bool gate_options_hold(Arena *a, GateStrs words, size_t from, size_t n, GateVerdict *v) {
    for (size_t i = from; i < n; i++) {
        Str w = at(words, i);
        if (!gate_plain(w)) {
            gate_refuse(a, v, S("`"), w, S("` is not a plain word; "));
            return false;
        }
        if (str_eq(w, S("--verbose")) || str_eq(w, S("--undo-on-stop")) || str_eq(w, S("--args-stdin")) || str_eq(w, S("--help")) || str_eq(w, S("--version"))) continue;
        if (str_eq(w, S("--budget"))) {
            if (i + 1 >= n || str_find_byte(at(words, i + 1), '=') == NULL || !gate_plain(at(words, i + 1))) {
                gate_refuse_plain(a, v, S("`--budget` takes `NAME=N`; "));
                return false;
            }
            i++;
            continue;
        }
        if (str_byte(w, 0) == '-') {
            gate_refuse(a, v, S("`"), w, S("` is not an option the gate passes; "));
            return false;
        }
    }
    return true;
}

// The piece of `rest` from `*from` to its next newline (or its end), with
// `*from` moved past that newline.
static inline Str gate_piece(Str rest, size_t *from) {
    Str tail = str_sub(rest, *from, rest.len);
    const char *nl = str_find_byte(tail, '\n');
    size_t len = nl == NULL ? tail.len : (size_t)(nl - tail.ptr);
    *from += len + 1;
    return str_sub(tail, 0, len);
}

// A heredoc's body, `rest`, held to its tag `t`: its last line `t` (trailing
// spaces allowed), then no line before it `t`. Its lines are `rest` split at
// each newline, at most GATE_LINES_MOST, the empty piece after a final
// newline not one.
static inline void gate_heredoc(Arena *a, Str rest, Str t, GateVerdict *v) {
    size_t pieces = 1;
    for (size_t i = 0; i < rest.len; i++) {
        if (str_byte(rest, i) == '\n') pieces++;
        if (pieces > GATE_LINES_MOST) {
            gate_refuse_plain(a, v, S("a heredoc of more than 4096 lines; "));
            return;
        }
    }
    size_t m = pieces;
    if (rest.len > 0 && str_byte(rest, rest.len - 1) == '\n') m--;
    Str last = {"", 0};
    size_t from = 0;
    for (size_t k = 0; k < m; k++) last = gate_piece(rest, &from);
    if (rest.len == 0 || m == 0 || !str_eq(gate_trim_end(last, S(" ")), t)) {
        gate_refuse(a, v, S("the heredoc has no last line `"), t, S("`; "));
        return;
    }
    from = 0;
    for (size_t k = 0; k + 1 < m; k++) {
        if (str_eq(gate_trim_end(gate_piece(rest, &from), S(" ")), t)) {
            gate_refuse(a, v, S("the heredoc meets `"), t, S("` twice: a line before its end would close it; "));
            return;
        }
    }
    v->kind = GATE_LAUNCH;
}

// Classifies a command: a launch, an escape, or refused (and why). The
// refusals' order is Zig's: the escape, the words, `cd`, the heredoc's
// opener, `shale`, its options, then the body.
static inline void gate_classify(Arena *a, Str cmd, GateVerdict *v) {
    GateVerdict none = {GATE_LAUNCH, {"", 0}, {"", 0}, {"", 0}, {"", 0}};
    *v = none;
    const char *nl = str_find_byte(cmd, '\n');
    size_t cut = nl == NULL ? cmd.len : (size_t)(nl - cmd.ptr);
    Str first = str_sub(cmd, 0, cut);
    Str rest = nl == NULL ? S("") : str_sub(cmd, cut + 1, cmd.len);
    if (gate_escape_of(first, S("want"), v)) return;
    if (gate_escape_of(first, S("out-of-scope"), v)) return;
    if (str_starts_with(first, S("#"))) {
        gate_refuse_plain(a, v, S("an escape is `# want: <tag>: <text>` or `# out-of-scope: <tag>: <text>`, the tag `[a-z0-9-]`; "));
        return;
    }

    Str word_items[GATE_WORDS_MOST];
    GateStrs words = {word_items, GATE_WORDS_MOST};
    size_t n = 0;
    size_t i = 0;
    while (i < first.len) {
        if (str_byte(first, i) == ' ') {
            i++;
            continue;
        }
        size_t start = i;
        while (i < first.len && str_byte(first, i) != ' ') i++;
        if (n == GATE_WORDS_MOST) {
            gate_refuse_plain(a, v, S("too many words; "));
            return;
        }
        at(words, n) = str_sub(first, start, i);
        n++;
    }
    size_t shale_at = 0;
    if (n >= 3 && str_eq(at(words, 0), S("cd")) && str_eq(at(words, 2), S("&&"))) {
        if (!gate_plain(at(words, 1))) {
            gate_refuse(a, v, S("`cd "), at(words, 1), S("` is not a plain folder; "));
            return;
        }
        shale_at = 3;
    }
    // The heredoc's opener, the first line's last word.
    bool heredoc = false;
    Str tag = {"", 0};
    if (n > shale_at && str_starts_with(at(words, n - 1), S("<<"))) {
        Str w = at(words, n - 1);
        if (w.len < 5 || str_byte(w, 2) != '\'' || str_byte(w, w.len - 1) != '\'' || !gate_upper_tag(str_sub(w, 3, w.len - 1))) {
            gate_refuse(a, v, S("`"), w, S("`: a heredoc is `<<'TAG'`, quoted, the TAG `[A-Z_]+`; "));
            return;
        }
        tag = str_sub(w, 3, w.len - 1);
        heredoc = true;
        n--;
    }
    if (n <= shale_at) {
        gate_refuse_plain(a, v, S("no `shale`; "));
        return;
    }
    Str prog = at(words, shale_at);
    bool by_path = str_byte(prog, 0) == '/' && gate_ends_with(prog, S("/shale")) && gate_plain(prog);
    if (!str_eq(prog, S("shale")) && !by_path) {
        gate_refuse(a, v, S("`"), prog, S("` is not `shale`; "));
        return;
    }
    if (!gate_options_hold(a, words, shale_at + 1, n, v)) return;
    if (!heredoc) {
        if (gate_trim(rest, S(" \t\r\n")).len != 0) {
            gate_refuse_plain(a, v, S("a launch is one line, or a heredoc's; "));
            return;
        }
        v->kind = GATE_LAUNCH;
        return;
    }
    gate_heredoc(a, rest, tag, v);
}

// ── The log line ─────────────────────────────────────────────────────────

// `s` appended to `b` as far as GATE_LOG_BYTES lets it: what passes it is
// cut, as Zig's fixed writer cuts it, mid-escape among them.
static inline STATUS_MUST_USE Status gate_log_put(Buf *b, Str s) {
    if (b->len >= GATE_LOG_BYTES) return OK;
    size_t room = GATE_LOG_BYTES - b->len;
    return buf_str(b, str_sub(s, 0, s.len < room ? s.len : room));
}

// A wants log line in `b`: date, session, kind, tag, text, and the command's
// first 160 bytes, tab-separated, each with its tabs, newlines and
// backslashes shown; at most GATE_LOG_BYTES and its newline, so one append
// never interleaves. A text past the line's room is cut, never the line.
static inline STATUS_MUST_USE Status gate_log_line(Buf *b, Str date, Str session, const GateVerdict *e, Str cmd) {
    Str fields[6] = {
        date, session, e->escape_kind, e->tag,
        str_sub(e->text, 0, e->text.len < GATE_LOG_TEXT_MOST ? e->text.len : GATE_LOG_TEXT_MOST),
        str_sub(cmd, 0, cmd.len < GATE_LOG_COMMAND_MOST ? cmd.len : GATE_LOG_COMMAND_MOST),
    };
    GateStrs all = {fields, 6};
    for (size_t i = 0; i < all.len; i++) {
        if (i > 0) TRY(gate_log_put(b, S("\t")));
        Str f = at(all, i);
        size_t plain_from = 0;
        for (size_t k = 0; k < f.len; k++) {
            uint8_t c = str_byte(f, k);
            if (c != '\t' && c != '\n' && c != '\\') continue;
            TRY(gate_log_put(b, str_sub(f, plain_from, k)));
            if (c == '\t') TRY(gate_log_put(b, S("\\t")));
            if (c == '\n') TRY(gate_log_put(b, S("\\n")));
            if (c == '\\') TRY(gate_log_put(b, S("\\\\")));
            plain_from = k + 1;
        }
        TRY(gate_log_put(b, str_sub(f, plain_from, f.len)));
    }
    return buf_byte(b, '\n');
}

// ── The hook's JSON ──────────────────────────────────────────────────────

// What the gate reads of a value: a string's text, an object, or anything
// else.
typedef enum { GATE_JSON_NONE, GATE_JSON_STRING, GATE_JSON_OBJECT, GATE_JSON_OTHER } GateJsonKind;

typedef struct {
    GateJsonKind kind;
    Str text;
} GateJsonValue;

// Which object a frame is: the top one, the top one's `tool_input`, or
// another, whose members the gate keeps nothing of.
typedef enum { GATE_ROLE_NONE, GATE_ROLE_TOP, GATE_ROLE_INPUT } GateRole;

VEC_DEFINE(GateKeys, Str);

typedef struct {
    bool object;
    GateRole role;
    // An object's keys so far, decoded, for the check that none is there
    // twice; and the member's key whose value comes next.
    GateKeys keys;
    Str key;
} GateFrame;

VEC_DEFINE(GateFrames, GateFrame);

typedef struct {
    Arena *arena;
    Str in;
    size_t pos;
    GateFrames frames;
    GateJsonValue session;
    GateJsonValue tool;
    GateJsonValue input;
    GateJsonValue command;
} GateJson;

static inline void gate_json_space(GateJson *j) {
    while (j->pos < j->in.len) {
        uint8_t c = str_byte(j->in, j->pos);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return;
        j->pos++;
    }
}

// The byte at the cursor, or -1 at the end.
static inline int gate_json_peek(const GateJson *j) {
    return j->pos < j->in.len ? (int)str_byte(j->in, j->pos) : -1;
}

static inline int gate_json_hex(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Four hex digits at the cursor, or -1.
static inline long gate_json_unit(GateJson *j) {
    long u = 0;
    for (int k = 0; k < 4; k++) {
        int h = gate_json_hex(gate_json_peek(j));
        if (h < 0) return -1;
        u = u * 16 + h;
        j->pos++;
    }
    return u;
}

// The code point `cp` as UTF-8.
static inline STATUS_MUST_USE Status gate_json_utf8(Buf *b, long cp) {
    if (cp < 0x80) return buf_byte(b, (char)cp);
    if (cp < 0x800) {
        TRY(buf_byte(b, (char)(0xC0 | (cp >> 6))));
        return buf_byte(b, (char)(0x80 | (cp & 0x3F)));
    }
    if (cp < 0x10000) {
        TRY(buf_byte(b, (char)(0xE0 | (cp >> 12))));
        TRY(buf_byte(b, (char)(0x80 | ((cp >> 6) & 0x3F))));
        return buf_byte(b, (char)(0x80 | (cp & 0x3F)));
    }
    TRY(buf_byte(b, (char)(0xF0 | (cp >> 18))));
    TRY(buf_byte(b, (char)(0x80 | ((cp >> 12) & 0x3F))));
    TRY(buf_byte(b, (char)(0x80 | ((cp >> 6) & 0x3F))));
    return buf_byte(b, (char)(0x80 | (cp & 0x3F)));
}

// How many continuation bytes a UTF-8 lead byte takes, and the range its
// first one must be in (std.json's Scanner, byte for byte); 0 for a byte
// that leads nothing.
static inline int gate_json_lead(uint8_t c, uint8_t *lo, uint8_t *hi) {
    *lo = 0x80;
    *hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) return 1;
    if (c == 0xE0) *lo = 0xA0;
    if (c == 0xED) *hi = 0x9F;
    if (c >= 0xE0 && c <= 0xEF) return 2;
    if (c == 0xF0) *lo = 0x90;
    if (c == 0xF4) *hi = 0x8F;
    if (c >= 0xF0 && c <= 0xF4) return 3;
    return 0;
}

// The string whose opening quote is at the cursor, decoded into `out`; false
// for one std.json refuses.
static inline STATUS_MUST_USE Status gate_json_string(GateJson *j, Str *out, bool *ok) {
    *ok = false;
    j->pos++;
    Buf b = buf_make(j->arena);
    while (true) {
        int c = gate_json_peek(j);
        if (c < 0 || c < 0x20) return OK;
        if (c == '"') {
            j->pos++;
            break;
        }
        if (c == '\\') {
            j->pos++;
            int e = gate_json_peek(j);
            j->pos++;
            if (e == '"' || e == '\\' || e == '/') {
                TRY(buf_byte(&b, (char)e));
            } else if (e == 'b') {
                TRY(buf_byte(&b, 0x08));
            } else if (e == 'f') {
                TRY(buf_byte(&b, 0x0C));
            } else if (e == 'n') {
                TRY(buf_byte(&b, '\n'));
            } else if (e == 'r') {
                TRY(buf_byte(&b, '\r'));
            } else if (e == 't') {
                TRY(buf_byte(&b, '\t'));
            } else if (e == 'u') {
                long u = gate_json_unit(j);
                if (u < 0 || (u >= 0xDC00 && u <= 0xDFFF)) return OK;
                if (u >= 0xD800 && u <= 0xDBFF) {
                    // A high half: a `\u` low half must follow at once.
                    if (gate_json_peek(j) != '\\') return OK;
                    j->pos++;
                    if (gate_json_peek(j) != 'u') return OK;
                    j->pos++;
                    long low = gate_json_unit(j);
                    if (low < 0xDC00 || low > 0xDFFF) return OK;
                    u = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
                }
                TRY(gate_json_utf8(&b, u));
            } else {
                return OK;
            }
            continue;
        }
        uint8_t lo = 0;
        uint8_t hi = 0;
        int more = c < 0x80 ? 0 : gate_json_lead((uint8_t)c, &lo, &hi);
        if (c >= 0x80 && more == 0) return OK;
        size_t start = j->pos;
        j->pos++;
        for (int k = 0; k < more; k++) {
            int d = gate_json_peek(j);
            if (d < lo || d > hi) return OK;
            lo = 0x80;
            hi = 0xBF;
            j->pos++;
        }
        TRY(buf_str(&b, str_sub(j->in, start, j->pos)));
    }
    *out = buf_view(&b);
    *ok = true;
    return OK;
}

static inline bool gate_json_digits(GateJson *j) {
    size_t start = j->pos;
    while (gate_json_peek(j) >= '0' && gate_json_peek(j) <= '9') j->pos++;
    return j->pos > start;
}

// A number at the cursor, by RFC 8259's grammar; false for a malformed one.
static inline bool gate_json_number(GateJson *j) {
    if (gate_json_peek(j) == '-') j->pos++;
    if (gate_json_peek(j) == '0') {
        j->pos++;
    } else if (!gate_json_digits(j)) {
        return false;
    }
    if (gate_json_peek(j) == '.') {
        j->pos++;
        if (!gate_json_digits(j)) return false;
    }
    if (gate_json_peek(j) == 'e' || gate_json_peek(j) == 'E') {
        j->pos++;
        if (gate_json_peek(j) == '+' || gate_json_peek(j) == '-') j->pos++;
        if (!gate_json_digits(j)) return false;
    }
    return true;
}

static inline bool gate_json_literal(GateJson *j, Str word) {
    if (!str_starts_with(str_sub(j->in, j->pos, j->in.len), word)) return false;
    j->pos += word.len;
    return true;
}

static inline int gate_key_order(const void *x, const void *y) {
    const Str *a = x;
    const Str *b = y;
    size_t n = a->len < b->len ? a->len : b->len;
    int c = mem_compare(a->ptr, b->ptr, n);
    if (c != 0) return c;
    return a->len < b->len ? -1 : a->len > b->len ? 1 : 0;
}

// Whether an object's keys hold one twice, which std.json refuses
// (`error.DuplicateField`).
static inline bool gate_keys_twice(GateKeys *keys) {
    if (keys->len < 2) return false;
    qsort(keys->ptr, keys->len, sizeof(Str), gate_key_order);
    for (size_t i = 1; i < keys->len; i++) {
        if (str_eq(at(*keys, i - 1), at(*keys, i))) return true;
    }
    return false;
}

// A member of the frame `f` complete, `v` its value: kept when the gate
// reads it.
static inline void gate_json_keep(GateJson *j, const GateFrame *f, GateJsonValue v) {
    if (f->role == GATE_ROLE_TOP) {
        if (str_eq(f->key, S("session_id"))) j->session = v;
        if (str_eq(f->key, S("tool_name"))) j->tool = v;
        if (str_eq(f->key, S("tool_input"))) j->input = v;
    } else if (f->role == GATE_ROLE_INPUT) {
        if (str_eq(f->key, S("command"))) j->command = v;
    }
}

// The key at the cursor of the object `f`, then its colon; false for a
// malformed one.
static inline STATUS_MUST_USE Status gate_json_key(GateJson *j, GateFrame *f, bool *ok) {
    *ok = false;
    gate_json_space(j);
    if (gate_json_peek(j) != '"') return OK;
    bool read = false;
    TRY(gate_json_string(j, &f->key, &read));
    if (!read) return OK;
    TRY(GateKeys_push(&f->keys, f->key));
    gate_json_space(j);
    if (gate_json_peek(j) != ':') return OK;
    j->pos++;
    *ok = true;
    return OK;
}

// The whole input read as std.json reads it into a Value, without recursion
// (a nesting a megabyte deep is JSON): `ok` false for input it refuses, or a
// value that is not an object at the top.
static inline STATUS_MUST_USE Status gate_json_read(GateJson *j, bool *ok) {
    *ok = false;
    j->frames = GateFrames_make(j->arena);
    gate_json_space(j);
    if (gate_json_peek(j) != '{') return OK;
    while (true) {
        // A value starts at the cursor.
        gate_json_space(j);
        int c = gate_json_peek(j);
        GateJsonValue v = {GATE_JSON_OTHER, {"", 0}};
        if (c == '{' || c == '[') {
            GateRole role = GATE_ROLE_NONE;
            if (j->frames.len == 0) {
                role = GATE_ROLE_TOP;
            } else {
                GateFrame *up = &at(j->frames, j->frames.len - 1);
                if (up->role == GATE_ROLE_TOP && str_eq(up->key, S("tool_input"))) role = GATE_ROLE_INPUT;
            }
            GateFrame f = {c == '{', c == '{' ? role : GATE_ROLE_NONE, GateKeys_make(j->arena), {"", 0}};
            j->pos++;
            gate_json_space(j);
            if ((c == '{' && gate_json_peek(j) != '}') || (c == '[' && gate_json_peek(j) != ']')) {
                TRY(GateFrames_push(&j->frames, f));
                if (c == '[') continue;
                bool keyed = false;
                TRY(gate_json_key(j, &at(j->frames, j->frames.len - 1), &keyed));
                if (!keyed) return OK;
                continue;
            }
            j->pos++;
            if (c == '{') v.kind = GATE_JSON_OBJECT;
        } else if (c == '"') {
            bool read = false;
            TRY(gate_json_string(j, &v.text, &read));
            if (!read) return OK;
            v.kind = GATE_JSON_STRING;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            if (!gate_json_number(j)) return OK;
        } else if (!(gate_json_literal(j, S("true")) || gate_json_literal(j, S("false")) || gate_json_literal(j, S("null")))) {
            return OK;
        }
        // A value is complete: it ends its containers as far as they close.
        while (true) {
            if (j->frames.len == 0) {
                gate_json_space(j);
                *ok = j->pos == j->in.len;
                return OK;
            }
            GateFrame *f = &at(j->frames, j->frames.len - 1);
            if (f->object) gate_json_keep(j, f, v);
            gate_json_space(j);
            int d = gate_json_peek(j);
            j->pos++;
            if (d == ',') {
                if (f->object) {
                    bool keyed = false;
                    TRY(gate_json_key(j, f, &keyed));
                    if (!keyed) return OK;
                }
                break;
            }
            if ((f->object && d == '}') || (!f->object && d == ']')) {
                if (f->object && gate_keys_twice(&f->keys)) return OK;
                GateJsonValue done = {f->object ? GATE_JSON_OBJECT : GATE_JSON_OTHER, {"", 0}};
                v = done;
                (void)GateFrames_pop(&j->frames);
                continue;
            }
            return OK;
        }
    }
}

// What the gate reads of the hook: its session (empty unless a string), and
// Bash's or Monitor's command, `has_command` false for any other tool.
typedef struct {
    Str session;
    bool has_command;
    Str command;
} GateHook;

// The hook's input read into `h`; false for input the gate cannot read
// (refused): not JSON, not an object, no `tool_name` string, or for Bash and
// Monitor no `tool_input` object holding a `command` string.
static inline STATUS_MUST_USE Status gate_read_hook(Arena *a, Str json, GateHook *h, bool *ok) {
    GateJson j;
    memset(&j, 0, sizeof j);
    j.arena = a;
    j.in = json;
    TRY(gate_json_read(&j, ok));
    if (!*ok) return OK;
    *ok = false;
    GateHook none = {{"", 0}, false, {"", 0}};
    *h = none;
    if (j.session.kind == GATE_JSON_STRING) h->session = j.session.text;
    if (j.tool.kind != GATE_JSON_STRING) return OK;
    if (!str_eq(j.tool.text, S("Bash")) && !str_eq(j.tool.text, S("Monitor"))) {
        *ok = true;
        return OK;
    }
    if (j.input.kind != GATE_JSON_OBJECT || j.command.kind != GATE_JSON_STRING) return OK;
    h->has_command = true;
    h->command = j.command.text;
    *ok = true;
    return OK;
}

// ── The decision and its effects ─────────────────────────────────────────

// What the gate does with one input: its exit code, the line it says on
// stderr (none when `say` is empty), and for an escape the verdict to log.
typedef struct {
    int code;
    Str say;
    bool log;
    GateVerdict escape;
    GateHook hook;
} GateDecision;

// The decision for the hook's input `input`, as Zig's `main` makes it before
// any effect; `read_ok` false when the input could not be read.
static inline void gate_decide(Arena *a, bool read_ok, Str input, GateDecision *d) {
    GateDecision none = {0, {"", 0}, false, {GATE_LAUNCH, {"", 0}, {"", 0}, {"", 0}, {"", 0}}, {{"", 0}, false, {"", 0}}};
    *d = none;
    if (!read_ok) {
        d->code = 2;
        d->say = S("shale-gate: the hook's input could not be read: refused");
        return;
    }
    bool ok = false;
    if (gate_read_hook(a, input, &d->hook, &ok) != OK || !ok) {
        d->code = 2;
        d->say = S("shale-gate: the hook's input is not the JSON it expects (tool_name, tool_input.command): refused");
        return;
    }
    if (!d->hook.has_command) return;
    GateVerdict v;
    gate_classify(a, d->hook.command, &v);
    if (v.kind == GATE_REFUSED) {
        d->code = 2;
        d->say = v.why;
    } else if (v.kind == GATE_ESCAPE) {
        d->log = true;
        d->escape = v;
    }
}

// The date as Zig's gate writes it, `2026-10-17T12:00:00Z`, in UTC.
static inline Str gate_iso_now(Arena *a) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm t;
    gmtime_r(&ts.tv_sec, &t);
    char *out = arena_alloc(a, 32, 1);
    if (out == NULL) return S("?");
    int n = snprintf(out, 32, "%d-%02d-%02dT%02d:%02d:%02dZ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    if (n < 0 || n >= 32) return S("?");
    Str s = {out, (size_t)n};
    return s;
}

// `s` with a NUL after it, for a system call; NULL when the arena refuses.
static inline const char *gate_cstr(Arena *a, Str s) { // cstr: a path for open(2) and mkdir(2)
    Buf b = buf_make(a);
    if (buf_str(&b, s) != OK || buf_byte(&b, '\0') != OK) return NULL;
    return b.ptr;
}

// One append of `line` to `<home>/.local/state/shale/wants.tsv`, outside
// every repo, its folders made first as Zig makes them: each prefix of the
// folder after its first byte, its errors not read. False when the file
// cannot be opened or the write is short.
static inline bool gate_append_log(Arena *a, Str home, Str line) {
    Buf dir = buf_make(a);
    if (buf_str(&dir, home) != OK || buf_lit(&dir, "/.local/state/shale") != OK) return false;
    Str d = buf_view(&dir);
    Str parts = str_sub(d, 1, d.len);
    Buf made = buf_make(a);
    size_t from = 0;
    while (true) {
        const char *slash = str_find_byte(str_sub(parts, from, parts.len), '/');
        size_t to = slash == NULL ? parts.len : from + (size_t)(slash - (parts.ptr + from));
        if (buf_byte(&made, '/') != OK || buf_str(&made, str_sub(parts, from, to)) != OK) return false;
        const char *p = gate_cstr(a, buf_view(&made));
        if (p == NULL) return false;
        (void)mkdir(p, 0755);
        if (slash == NULL) break;
        from = to + 1;
    }
    Buf file = buf_make(a);
    if (buf_str(&file, d) != OK || buf_lit(&file, "/wants.tsv") != OK) return false;
    const char *path = gate_cstr(a, buf_view(&file));
    if (path == NULL) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return false;
    ssize_t n = write(fd, line.ptr, line.len);
    close(fd);
    return n >= 0 && (size_t)n == line.len;
}

static inline void gate_say(Str msg) {
    ssize_t n = write(2, msg.ptr, msg.len);
    n = write(2, "\n", 1);
    (void)n;
}

// The HOME the environment gives, the last one when it names two, as Zig's
// environment map keeps it; false for none.
static inline bool gate_home(Str *home) {
    extern char **environ;
    bool found = false;
    for (char **e = environ; e != NULL && *e != NULL; e++) {
        Str entry = {*e, strlen(*e)};
        if (str_starts_with(entry, S("HOME="))) {
            *home = str_sub(entry, 5, entry.len);
            found = true;
        }
    }
    return found;
}

// The gate's whole run on `input` (`read_ok` false when it could not be
// read): the decision, its line said, and an escape logged under `home`
// (`has_home` false for none). Gives the exit code.
static inline int gate_run(Arena *a, bool read_ok, Str input, bool has_home, Str home) {
    GateDecision d;
    gate_decide(a, read_ok, input, &d);
    if (d.code != 0) {
        gate_say(d.say);
        return d.code;
    }
    if (!d.log) return 0;
    if (!has_home) {
        gate_say(S("shale-gate: no HOME, so the escape cannot be logged: refused"));
        return 2;
    }
    Buf line = buf_make(a);
    if (gate_log_line(&line, gate_iso_now(a), d.hook.session, &d.escape, d.hook.command) != OK || !gate_append_log(a, home, buf_view(&line))) {
        gate_say(S("shale-gate: the escape could not be logged to ~/.local/state/shale/wants.tsv: refused"));
        return 2;
    }
    return 0;
}
