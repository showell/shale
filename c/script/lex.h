// lex.h: turns source into Lua's tokens, and refuses by name each of Lua's
// that this language does not take: a long string without a level, floats,
// `/`, `:`, `...`, bitwise operators, `goto`, `repeat`. It is Zig Shale's
// src/script/lex.zig, and every message is its text, byte for byte.

#ifndef SHALE_LEX_H
#define SHALE_LEX_H

#include "../foundation/buf.h"

typedef enum {
    TAG_EOF,
    TAG_NAME,
    TAG_INT,
    TAG_STRING,
    // Only `lexer_next_or_comment` gives one: `lexer_next` passes comments by.
    TAG_COMMENT,
    TAG_KW_AND,
    TAG_KW_BREAK,
    TAG_KW_CONST,
    TAG_KW_CONTINUE,
    TAG_KW_DO,
    TAG_KW_ELSE,
    TAG_KW_ELSEIF,
    TAG_KW_END,
    TAG_KW_FALSE,
    TAG_KW_FOR,
    TAG_KW_FUNCTION,
    TAG_KW_IF,
    TAG_KW_IN,
    TAG_KW_LOCAL,
    TAG_KW_NIL,
    TAG_KW_NOT,
    TAG_KW_OR,
    TAG_KW_RETURN,
    TAG_KW_THEN,
    TAG_KW_TRUE,
    TAG_KW_WHILE,
    TAG_PLUS,
    TAG_MINUS,
    TAG_STAR,
    TAG_SLASH_SLASH,
    TAG_PERCENT,
    TAG_DOT_DOT,
    TAG_EQ_EQ,
    TAG_NOT_EQ,
    TAG_LT,
    TAG_LE,
    TAG_GT,
    TAG_GE,
    TAG_ASSIGN,
    TAG_LPAREN,
    TAG_RPAREN,
    TAG_LBRACE,
    TAG_RBRACE,
    TAG_LBRACKET,
    TAG_RBRACKET,
    TAG_COMMA,
    TAG_DOT,
    TAG_HASH,
    // `?`: after a parameter's name, the parameter is optional.
    TAG_QUESTION,
    TAG_SEMICOLON,
} Tag;

// A long string's level and where its closer is, for a syntax error right
// after it to name.
typedef struct {
    uint32_t level;
    uint32_t close_line;
    uint32_t close_column;
} LongString;

typedef struct {
    Tag tag;
    // A name's or an int's source bytes; a string's bytes with its escapes
    // undone, in the lexer's arena.
    Str text;
    uint32_t line;
    uint32_t column;
    // The token in the source, `[start, stop)`: a string's quotes and its
    // escapes as written are in it.
    size_t start;
    size_t stop;
    int64_t value;
    // The literal is 9223372036854775808: no int64, but the magnitude of the
    // smallest one, which the parser makes of a `-` before it.
    bool min_magnitude;
    bool is_long;
    LongString long_string;
} Token;

typedef struct {
    uint32_t line;
    uint32_t column;
    bool refused;
    Str message;
} LexError;

typedef struct {
    Str src;
    size_t at;
    uint32_t line;
    uint32_t column;
    // Where a string's bytes and a message are made.
    Arena *arena;
    LexError err;
    // The arena refused: the caller reads it as Zig's `error.OutOfMemory`.
    bool oom;
} Lexer;

typedef struct {
    Str word;
    Tag tag;
} Keyword;

SLICE_TYPE(Keywords, const Keyword);

typedef struct {
    Str word;
    Str why;
} RefusedWord;

SLICE_TYPE(RefusedWords, const RefusedWord);

static inline void lexer_init(Lexer *lx, Arena *a, Str src) {
    memset(lx, 0, sizeof *lx);
    lx->src = src;
    lx->line = 1;
    lx->column = 1;
    lx->arena = a;
}

// A word's keyword tag, or TAG_NAME.
static inline Tag lex_keyword(Str word) {
    static const Keyword table[] = {
        {S("and"), TAG_KW_AND}, {S("break"), TAG_KW_BREAK}, {S("do"), TAG_KW_DO},
        {S("const"), TAG_KW_CONST}, {S("else"), TAG_KW_ELSE}, {S("elseif"), TAG_KW_ELSEIF},
        {S("continue"), TAG_KW_CONTINUE}, {S("end"), TAG_KW_END}, {S("false"), TAG_KW_FALSE},
        {S("for"), TAG_KW_FOR}, {S("function"), TAG_KW_FUNCTION}, {S("if"), TAG_KW_IF},
        {S("in"), TAG_KW_IN}, {S("local"), TAG_KW_LOCAL}, {S("nil"), TAG_KW_NIL},
        {S("not"), TAG_KW_NOT}, {S("or"), TAG_KW_OR}, {S("return"), TAG_KW_RETURN},
        {S("then"), TAG_KW_THEN}, {S("true"), TAG_KW_TRUE}, {S("while"), TAG_KW_WHILE},
    };
    Keywords k = {table, sizeof table / sizeof(Keyword)};
    for (size_t i = 0; i < k.len; i++) {
        if (str_eq(at(k, i).word, word)) return at(k, i).tag;
    }
    return TAG_NAME;
}

// Why Lua's `goto`, `repeat` and `until` are refused, or an empty Str.
static inline Str lex_refused_word(Str word) {
    static const RefusedWord table[] = {
        {S("goto"), S("`goto` is not in this language: `break` and `continue` leave or go round a loop")},
        {S("repeat"), S("`repeat ... until` is not in this language: use `while`")},
        {S("until"), S("`repeat ... until` is not in this language: use `while`")},
    };
    RefusedWords r = {table, sizeof table / sizeof(RefusedWord)};
    for (size_t i = 0; i < r.len; i++) {
        if (str_eq(at(r, i).word, word)) return at(r, i).why;
    }
    return S("");
}

static inline bool lex_is_alpha(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline bool lex_is_digit(uint8_t c) {
    return c >= '0' && c <= '9';
}

// Zig's `std.ascii.isPrint`: a space through `~`.
static inline bool lex_is_print(uint8_t c) {
    return c >= 0x20 && c <= 0x7e;
}

// A hexadecimal digit's value, or -1.
static inline int lex_hex_digit(uint8_t c) {
    if (lex_is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static inline STATUS_MUST_USE Status lex_fail(Lexer *lx, uint32_t line, uint32_t column, bool refused, Str message) {
    LexError e = {line, column, refused, message};
    lx->err = e;
    return STOP;
}

// The arena refused while a token or a message was made.
static inline STATUS_MUST_USE Status lex_oom(Lexer *lx) {
    lx->oom = true;
    return STOP;
}

// The byte at `at + k`, or 0 past the end.
static inline uint8_t lex_peek_at(const Lexer *lx, size_t k) {
    return lx->at + k < lx->src.len ? str_byte(lx->src, lx->at + k) : 0;
}

static inline uint8_t lex_bump(Lexer *lx) {
    uint8_t c = str_byte(lx->src, lx->at);
    lx->at++;
    if (c == '\n') {
        lx->line++;
        lx->column = 1;
    } else {
        lx->column++;
    }
    return c;
}

static inline void lex_skip_space(Lexer *lx) {
    while (lx->at < lx->src.len) {
        uint8_t c = str_byte(lx->src, lx->at);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return;
        lex_bump(lx);
    }
}

static inline bool lex_at_comment(const Lexer *lx) {
    return lex_peek_at(lx, 0) == '-' && lex_peek_at(lx, 1) == '-';
}

// A bracket, `level` `=`s, then the bracket again: `]==]` or `[==[`, as much
// as fits in `cap` bytes, as Zig's `closerText` and `openerText` cut it.
static inline STATUS_MUST_USE Status lex_brackets(Buf *b, char bracket, uint32_t level, size_t cap) {
    size_t n = level < cap - 2 ? level : cap - 2;
    TRY(buf_byte(b, bracket));
    for (size_t i = 0; i < n; i++) TRY(buf_byte(b, '='));
    return buf_byte(b, bracket);
}

// A long string's closer at `level`, cut to Zig's buffer of `cap` bytes.
static inline STATUS_MUST_USE Status lex_closer_text(Buf *b, uint32_t level, size_t cap) {
    return lex_brackets(b, ']', level, cap);
}

static inline STATUS_MUST_USE Status lex_opener_text(Buf *b, uint32_t level, size_t cap) {
    return lex_brackets(b, '[', level, cap);
}

// A long string, `[=[ ... ]=]`: a level of one or more `=`, closed by the same
// level. Its bytes are the source's, raw (no escapes, a CRLF kept), except
// that a newline right after the opener is dropped. The caller has taken the
// opening `[`.
static inline STATUS_MUST_USE Status lex_long_string(Lexer *lx, uint32_t line, uint32_t column, Token *out) {
    uint32_t level = 0;
    while (lex_peek_at(lx, 0) == '=') {
        lex_bump(lx);
        level++;
    }
    if (lex_peek_at(lx, 0) != '[') return lex_fail(lx, line, column, true, S("`[=` opens a long string, `[=[ ... ]=]`: a `[` must follow its `=`s"));
    lex_bump(lx);
    if (lex_peek_at(lx, 0) == '\r' && lex_peek_at(lx, 1) == '\n') {
        lex_bump(lx);
        lex_bump(lx);
    } else if (lex_peek_at(lx, 0) == '\n') {
        lex_bump(lx);
    }
    size_t start = lx->at;
    while (lx->at < lx->src.len) {
        if (str_byte(lx->src, lx->at) == ']') {
            uint32_t n = 0;
            while (lex_peek_at(lx, 1 + n) == '=') n++;
            if (n == level && lex_peek_at(lx, 1 + n) == ']') {
                Token t;
                memset(&t, 0, sizeof t);
                t.tag = TAG_STRING;
                t.text = str_sub(lx->src, start, lx->at);
                t.line = line;
                t.column = column;
                t.is_long = true;
                LongString l = {level, lx->line, lx->column};
                t.long_string = l;
                for (uint32_t k = 0; k < level + 2; k++) lex_bump(lx);
                *out = t;
                return OK;
            }
        }
        lex_bump(lx);
    }
    Buf b = buf_make(lx->arena);
    Status s = buf_lit(&b, "a long string with no `");
    if (s == OK) s = lex_closer_text(&b, level, 64);
    if (s == OK) s = buf_lit(&b, "` to close it");
    return lex_fail(lx, line, column, false, s == OK ? buf_view(&b) : S("a long string that is never closed"));
}

// Passes the comment at `at`. A line comment stops before its newline.
static inline STATUS_MUST_USE Status lex_comment(Lexer *lx) {
    if (lex_peek_at(lx, 2) == '[' && lex_peek_at(lx, 3) == '[')
        return lex_fail(lx, lx->line, lx->column, true, S("a long comment needs a level: `--[=[ ... ]=]` (code is full of `]]`), or start each line with `--`"));
    if (lex_peek_at(lx, 2) == '[' && lex_peek_at(lx, 3) == '=') {
        // A block comment is `--` and a long string's opener; `--[=x` is a
        // line comment.
        size_t n = 3;
        while (lex_peek_at(lx, n) == '=') n++;
        if (lex_peek_at(lx, n) == '[') {
            uint32_t line = lx->line;
            uint32_t column = lx->column;
            lex_bump(lx);
            lex_bump(lx);
            lex_bump(lx);
            Token ignored;
            if (lex_long_string(lx, line, column, &ignored) == OK) return OK;
            Buf b = buf_make(lx->arena);
            Status s = buf_lit(&b, "a long comment with no `");
            if (s == OK) s = lex_closer_text(&b, (uint32_t)(n - 3), 64);
            if (s == OK) s = buf_lit(&b, "` to close it");
            return lex_fail(lx, line, column, false, s == OK ? buf_view(&b) : S("a long comment that is never closed"));
        }
    }
    while (lx->at < lx->src.len && str_byte(lx->src, lx->at) != '\n') lex_bump(lx);
    return OK;
}

static inline STATUS_MUST_USE Status lex_skip_space_and_comments(Lexer *lx) {
    lex_skip_space(lx);
    while (lex_at_comment(lx)) {
        TRY(lex_comment(lx));
        lex_skip_space(lx);
    }
    return OK;
}

static inline void lex_token(Token *t, Tag tag, Str text, uint32_t line, uint32_t column) {
    memset(t, 0, sizeof *t);
    t->tag = tag;
    t->text = text;
    t->line = line;
    t->column = column;
}

static inline STATUS_MUST_USE Status lex_number(Lexer *lx, uint32_t line, uint32_t column, Token *out) {
    size_t start = lx->at;
    if (str_byte(lx->src, lx->at) == '0' && (lex_peek_at(lx, 1) == 'x' || lex_peek_at(lx, 1) == 'X'))
        return lex_fail(lx, line, column, true, S("hexadecimal literals are not in this language: write the number in decimal"));
    while (lx->at < lx->src.len && lex_is_digit(str_byte(lx->src, lx->at))) lex_bump(lx);
    uint8_t after = lex_peek_at(lx, 0);
    if ((after == '.' && lex_peek_at(lx, 1) != '.') || after == 'e' || after == 'E')
        return lex_fail(lx, line, column, true, S("float literals are not in this language: integers only"));
    if (lex_is_alpha(after) || after == '_') return lex_fail(lx, line, column, false, S("a number runs into a name"));
    Str text = str_sub(lx->src, start, lx->at);
    uint64_t v = 0;
    bool over = false;
    for (size_t i = 0; i < text.len; i++) {
        uint64_t d = (uint64_t)(str_byte(text, i) - '0');
        if (v > (UINT64_MAX - d) / 10) {
            over = true;
            break;
        }
        v = v * 10 + d;
    }
    lex_token(out, TAG_INT, text, line, column);
    if (!over && v <= (uint64_t)INT64_MAX) {
        out->value = (int64_t)v;
        return OK;
    }
    // Zig's `parseInt` fails on the smallest integer's magnitude; only its
    // exact spelling, with no leading zero, is taken as it.
    if (str_eq(text, S("9223372036854775808"))) {
        out->value = INT64_MIN;
        out->min_magnitude = true;
        return OK;
    }
    return lex_fail(lx, line, column, false, S("an integer literal too big for 64 bits: they run from -9223372036854775808 to 9223372036854775807"));
}

static inline STATUS_MUST_USE Status lex_string(Lexer *lx, uint32_t line, uint32_t column, Token *out) {
    uint8_t quote = lex_bump(lx);
    Buf b = buf_make(lx->arena);
    for (;;) {
        if (lx->at >= lx->src.len) return lex_fail(lx, line, column, false, S("a string with no closing quote"));
        uint8_t c = lex_bump(lx);
        if (c == quote) break;
        if (c == '\n') return lex_fail(lx, line, column, false, S("a string runs past the end of its line: write \\n"));
        if (c != '\\') {
            if (buf_byte(&b, (char)c) != OK) return lex_oom(lx);
            continue;
        }
        if (lx->at >= lx->src.len) return lex_fail(lx, line, column, false, S("a string with no closing quote"));
        uint32_t e_line = lx->line;
        uint32_t e_col = lx->column - 1;
        uint8_t e = lex_bump(lx);
        uint8_t byte = 0;
        switch (e) {
        case 'n': byte = '\n'; break;
        case 't': byte = '\t'; break;
        case 'r': byte = '\r'; break;
        case '0':
            if (lex_is_digit(lex_peek_at(lx, 0))) return lex_fail(lx, e_line, e_col, true, S("decimal escapes (`\\ddd`) are not in this language: write a byte as \\xHH"));
            byte = 0;
            break;
        case 'x': {
            int hi = lex_hex_digit(lex_peek_at(lx, 0));
            if (hi < 0) return lex_fail(lx, e_line, e_col, false, S("`\\x` takes two hexadecimal digits: \\x0a"));
            int lo = lex_hex_digit(lex_peek_at(lx, 1));
            if (lo < 0) return lex_fail(lx, e_line, e_col, false, S("`\\x` takes two hexadecimal digits: \\x0a"));
            lex_bump(lx);
            lex_bump(lx);
            byte = (uint8_t)(hi * 16 + lo);
            break;
        }
        case '\\': byte = '\\'; break;
        case '"': byte = '"'; break;
        case '\'': byte = '\''; break;
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            return lex_fail(lx, e_line, e_col, true, S("decimal escapes (`\\ddd`) are not in this language: write a byte as \\xHH"));
        case 'u':
            return lex_fail(lx, e_line, e_col, true, S("`\\u{...}` is not in this language: strings are bytes; write UTF-8's bytes as \\xHH"));
        case 'z':
            return lex_fail(lx, e_line, e_col, true, S("`\\z` is not in this language: a string ends on its line"));
        default:
            return lex_fail(lx, e_line, e_col, true, S("an escape this language does not have: only \\n, \\t, \\r, \\0, \\xHH, \\\\, \\\" and \\'"));
        }
        if (buf_byte(&b, (char)byte) != OK) return lex_oom(lx);
    }
    lex_token(out, TAG_STRING, buf_view(&b), line, column);
    return OK;
}

// A byte no token starts with, as the message shows it: quoted when it prints,
// as `\xHH` when not. Zig's `shown` falls back to "" when memory runs out.
static inline STATUS_MUST_USE Status lex_fail_stray(Lexer *lx, uint32_t line, uint32_t column, uint8_t c) {
    static const char hex[] = "0123456789ABCDEF";
    Str digits = {hex, 16};
    Buf b = buf_make(lx->arena);
    Status s = buf_lit(&b, "a character no token starts with, ");
    if (lex_is_print(c)) {
        if (s == OK) s = buf_byte(&b, '`');
        if (s == OK) s = buf_byte(&b, (char)c);
        if (s == OK) s = buf_byte(&b, '`');
    } else {
        if (s == OK) s = buf_lit(&b, "the byte \\x");
        if (s == OK) s = buf_byte(&b, at(digits, c >> 4));
        if (s == OK) s = buf_byte(&b, at(digits, c & 15));
    }
    if (s != OK) return lex_oom(lx);
    return lex_fail(lx, line, column, false, buf_view(&b));
}

// The two-byte symbol `c` then `two` makes, `single` when `two` is not `want`.
static inline Tag lex_pair(Lexer *lx, uint8_t two, uint8_t want, Tag pair, Tag single) {
    if (two != want) return single;
    lex_bump(lx);
    return pair;
}

// The token at `at`, with space and comments already passed.
static inline STATUS_MUST_USE Status lex_scan(Lexer *lx, Token *out) {
    uint32_t line = lx->line;
    uint32_t column = lx->column;
    if (lx->at >= lx->src.len) {
        lex_token(out, TAG_EOF, S(""), line, column);
        return OK;
    }
    size_t start = lx->at;
    uint8_t c = str_byte(lx->src, lx->at);
    if (lex_is_alpha(c) || c == '_') {
        while (lx->at < lx->src.len) {
            uint8_t d = str_byte(lx->src, lx->at);
            if (!(lex_is_alpha(d) || lex_is_digit(d) || d == '_')) break;
            lex_bump(lx);
        }
        Str word = str_sub(lx->src, start, lx->at);
        Str why = lex_refused_word(word);
        if (why.len > 0) return lex_fail(lx, line, column, true, why);
        lex_token(out, lex_keyword(word), word, line, column);
        return OK;
    }
    if (lex_is_digit(c)) return lex_number(lx, line, column, out);
    if (c == '"' || c == '\'') return lex_string(lx, line, column, out);

    lex_bump(lx);
    uint8_t two = lex_peek_at(lx, 0);
    Tag tag;
    switch (c) {
    case '+': tag = TAG_PLUS; break;
    case '-': tag = TAG_MINUS; break;
    case '*': tag = TAG_STAR; break;
    case '%': tag = TAG_PERCENT; break;
    case '#': tag = TAG_HASH; break;
    case '?': tag = TAG_QUESTION; break;
    case '(': tag = TAG_LPAREN; break;
    case ')': tag = TAG_RPAREN; break;
    case '{': tag = TAG_LBRACE; break;
    case '}': tag = TAG_RBRACE; break;
    case ']': tag = TAG_RBRACKET; break;
    case ',': tag = TAG_COMMA; break;
    case ';': tag = TAG_SEMICOLON; break;
    case '[':
        if (two == '[') return lex_fail(lx, line, column, true, S("a long string needs a level: `[=[ ... ]=]` (code being edited is full of `]]`)"));
        if (two == '=') return lex_long_string(lx, line, column, out);
        tag = TAG_LBRACKET;
        break;
    case '/':
        if (two != '/') return lex_fail(lx, line, column, true, S("`/` divides into a float, and this language has no floats: use `//` (integer division)"));
        lex_bump(lx);
        tag = TAG_SLASH_SLASH;
        break;
    case '.':
        if (two == '.') {
            lex_bump(lx);
            if (lex_peek_at(lx, 0) == '.') return lex_fail(lx, line, column, true, S("`...` (varargs) is not in this language"));
            tag = TAG_DOT_DOT;
            break;
        }
        if (lex_is_digit(two)) return lex_fail(lx, line, column, true, S("float literals are not in this language: integers only"));
        tag = TAG_DOT;
        break;
    case '=': tag = lex_pair(lx, two, '=', TAG_EQ_EQ, TAG_ASSIGN); break;
    case '~':
        if (two != '=') return lex_fail(lx, line, column, true, S("bitwise operators (`~`) are not in this language"));
        lex_bump(lx);
        tag = TAG_NOT_EQ;
        break;
    case '<':
        if (two == '<') return lex_fail(lx, line, column, true, S("bitwise operators (`<<`) are not in this language"));
        tag = lex_pair(lx, two, '=', TAG_LE, TAG_LT);
        break;
    case '>':
        if (two == '>') return lex_fail(lx, line, column, true, S("bitwise operators (`>>`) are not in this language"));
        tag = lex_pair(lx, two, '=', TAG_GE, TAG_GT);
        break;
    case ':':
        if (two == ':') return lex_fail(lx, line, column, true, S("labels (`::name::`) are not in this language"));
        return lex_fail(lx, line, column, true, S("`:` method calls are not in this language: call a builtin, `f(x, ...)`"));
    case '&':
    case '|':
        return lex_fail(lx, line, column, true, S("bitwise operators are not in this language"));
    case '^':
        return lex_fail(lx, line, column, true, S("`^` (exponent) gives a float, and this language has no floats: multiply, `x * x`"));
    default:
        return lex_fail_stray(lx, line, column, c);
    }
    lex_token(out, tag, str_sub(lx->src, start, lx->at), line, column);
    return OK;
}

static inline STATUS_MUST_USE Status lex_placed_token(Lexer *lx, Token *out) {
    size_t start = lx->at;
    TRY(lex_scan(lx, out));
    out->start = start;
    out->stop = lx->at;
    return OK;
}

// The next token, comments passed by. STOP with `err` set (or `oom`) when the
// source does not lex.
static inline STATUS_MUST_USE Status lexer_next(Lexer *lx, Token *out) {
    TRY(lex_skip_space_and_comments(lx));
    return lex_placed_token(lx, out);
}

// `lexer_next`, but a comment comes back as a TAG_COMMENT token, its text its
// source bytes, so that a reader of the source sees every byte but space.
static inline STATUS_MUST_USE Status lexer_next_or_comment(Lexer *lx, Token *out) {
    lex_skip_space(lx);
    if (!lex_at_comment(lx)) return lex_placed_token(lx, out);
    size_t start = lx->at;
    uint32_t line = lx->line;
    uint32_t column = lx->column;
    TRY(lex_comment(lx));
    lex_token(out, TAG_COMMENT, str_sub(lx->src, start, lx->at), line, column);
    out->start = start;
    out->stop = lx->at;
    return OK;
}

#endif
