// parse.h: parses tokens into a `Program`, resolving names as it goes, as Zig
// Shale's src/script/parse.zig does. Lua's constructs this language does not
// take are refused by name, and the depth is held to the nesting budget so
// that no source can run the parser off its stack. The preludes after the
// core's (the Linux and git preludes, a repo's, a user's) and the libraries a
// `use` names are indexed by their lines and each function parsed at its
// first call. Every stop's class, place and text is Zig's.
//
// Everything the parse makes, the tree and its own working lists alike, comes
// from one arena: the machine's. A STOP with `stopped` false is the arena's
// refusal, which the caller reads as Zig's `error.OutOfMemory`.

#ifndef SHALE_PARSE_H
#define SHALE_PARSE_H

#include "../foundation/map.h"
#include "ast.h"
#include "lex.h"

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

// What a library has beyond a prelude: the line of its `use`, and its
// `const`s, each from the start of its line.
typedef struct {
    uint32_t use_line;
    PreludeFunctions consts;
} Library;

SLICE_TYPE(LoadedSlots, uint32_t);

// A later prelude as a parse holds it: its functions, and the index in
// `functions` of each one loaded (PARSE_UNLOADED until its first call). A
// `use`d library is one too, with `is_library` set.
typedef struct {
    Prelude prelude;
    PreludeFunctions index;
    LoadedSlots loaded;
    bool is_library;
    Library library;
} Extra;

SLICE_TYPE(Extras, Extra);

// The host's reader of a `use`d library, Zig's `parse.Loader`: on OK, `*got`
// says whether `*out` holds the source; when it doesn't, `*why` says why.
// STOP only when the arena refuses.
typedef struct {
    void *ctx;
    STATUS_MUST_USE Status (*load)(void *ctx, Arena *into, Str path, Str *why, Str *out, bool *got);
} Loader;

typedef struct {
    Str name;
    uint16_t slot;
    uint16_t depth;
    bool constant;
} Local;

VEC_DEFINE(LocalVec, Local)
VEC_DEFINE(StmtVec, Stmt)
VEC_DEFINE(ExprVec, Expr *)
VEC_DEFINE(ArmVec, Arm)
VEC_DEFINE(EntryVec, MapEntry)
VEC_DEFINE(FunctionVec, Function)
VEC_DEFINE(StrVec, Str)
VEC_DEFINE(OwnerVec, size_t)
MAP_DEFINE(FunctionNames, uint32_t)

// One function's names: a stack of locals, each in the block it was declared in.
typedef struct {
    LocalVec locals;
    uint16_t depth;
    uint16_t slots;
    uint16_t loops;
} Frame;

enum {
    // The most libraries one script may `use`.
    PARSE_LIBRARIES_MOST = 16,
    // The most preludes and libraries one parse holds.
    PARSE_EXTRAS_MOST = 16 + PARSE_LIBRARIES_MOST,
    PARSE_UNLOADED = UINT32_MAX,
};

// `in_prelude` and a const's owner for the script itself; the core prelude is
// 0 and `extra`'s are 1 on.
#define PARSE_SCRIPT SIZE_MAX

typedef struct {
    Arena *arena;
    Lexer lexer;
    Token tok;
    bool has_ahead;
    Token ahead;
    bool stopped;
    Stop stop;
    // The long string just before `tok`, if there is one: where it opened, its
    // level, and where it closed.
    bool after_long;
    Pos after_long_open;
    LongString after_long_string;
    uint32_t max_nesting;
    uint32_t nesting;
    Frame frame;
    // Whether the frame is a function's, not the script's own top level.
    bool in_function;
    // The script's own frame while one of its functions is parsed, so a stop
    // can tell a script `local` the function can't see from a name that is
    // nowhere, and name the fix.
    const Frame *script_frame;
    // The lists under construction, one stack per kind, shared by every
    // level: a list is the stack above its mark.
    StmtVec stmts;
    ExprVec exprs;
    ArmVec arms;
    EntryVec entries;
    StrVec args_read;
    bool args_by_value;
    FunctionVec functions;
    // The script's own functions, by name.
    FunctionNames function_names;
    // The core prelude's source and its functions, and each one's index in
    // `functions` once loaded.
    Str core;
    PreludeFunctions core_index;
    LoadedSlots core_loaded;
    // The preludes after the core's, then the libraries `use`d, in order.
    Extras extra;
    size_t extra_cap;
    // Which prelude is being parsed (0 for the core's, then `extra`'s), or
    // PARSE_SCRIPT. A prelude calls only its own functions, earlier preludes'
    // and the builtins, never the script's or a later prelude's.
    size_t in_prelude;
    // The top-level `const`s declared so far, and who declared each:
    // PARSE_SCRIPT, or a library's place among the preludes.
    StrVec consts;
    OwnerVec const_owner;
    // Reads the library a `use` names; `load` is NULL where none is loaded.
    Loader loader;
} Parser;

// Lua's library tables and functions a writer reaches for, each with what to
// write instead.
typedef struct {
    Str name;
    Str why;
} LuaName;

SLICE_TYPE(LuaNames, const LuaName);

static inline Str parse_lua_name(Str name) {
    static const LuaName table[] = {
        {S("string"), S("the standard library is not in this language: call the builtin, `upper(s)`, not `string.upper(s)`")},
        {S("table"), S("the standard library is not in this language: call the builtin, `push(l, x)`, not `table.insert(l, x)`")},
        {S("math"), S("the standard library is not in this language")},
        {S("os"), S("the standard library is not in this language: `now()` is the only time")},
        {S("io"), S("the standard library is not in this language: `read`, `print`")},
        {S("utf8"), S("the standard library is not in this language")},
        {S("coroutine"), S("coroutines are not in this language")},
        {S("debug"), S("the standard library is not in this language")},
        {S("package"), S("the standard library is not in this language")},
        {S("require"), S("`require` is not in this language")},
        {S("pcall"), S("`pcall` is not in this language: `attempt(f, ...)` calls a function and gives its error as a value")},
        {S("xpcall"), S("`xpcall` is not in this language: `attempt(f, ...)` calls a function and gives its error as a value")},
        {S("error"), S("`error` is not in this language: `fail(msg)` stops the run with your message, and `attempt` catches it")},
        {S("pairs"), S("`pairs` is not in this language: `for k in keys(m) do ... m[k] ... end`")},
        {S("ipairs"), S("`ipairs` is not in this language: `for x in list do ... end`, or `for i = 1, #list do`")},
        {S("setmetatable"), S("metatables are not in this language")},
        {S("getmetatable"), S("metatables are not in this language")},
        {S("type"), S("`type` is not in this language")},
        {S("select"), S("varargs are not in this language")},
        {S("unpack"), S("multiple values are not in this language")},
        {S("load"), S("`load` is not in this language")},
        {S("dofile"), S("`dofile` is not in this language")},
        {S("rawget"), S("metatables are not in this language")},
        {S("rawset"), S("metatables are not in this language")},
    };
    LuaNames t = {table, sizeof table / sizeof(LuaName)};
    for (size_t i = 0; i < t.len; i++) {
        if (str_eq(at(t, i).name, name)) return at(t, i).why;
    }
    return S("");
}

// ── messages ──

// A message under construction: each piece appended while the arena allows,
// so a refusal is seen once, at the end.
typedef struct {
    Buf b;
    Status s;
} Msg;

static inline void msg_init(Msg *m, Arena *a) {
    m->b = buf_make(a);
    m->s = OK;
}

static inline void msg_str(Msg *m, Str s) {
    if (m->s == OK) m->s = buf_str(&m->b, s);
}

static inline void msg_u64(Msg *m, uint64_t n) {
    if (m->s == OK) m->s = buf_u64(&m->b, n);
}

static inline STATUS_MUST_USE Status parse_set_stop(Parser *p, Pos pos, StopClass cls, Str message) {
    Stop s = {0};
    memset(&s, 0, sizeof s);
    s.line = pos.line;
    s.column = pos.column;
    s.cls = cls;
    s.budget = BUDGET_NONE;
    s.message = message;
    s.function = S("");
    p->stop = s;
    p->stopped = true;
    return STOP;
}

// Zig's `failAt`: a syntax error right after a long string is most likely a
// closer inside its text that ended it early, so the stop names that closer
// and the level that would hold it.
static inline STATUS_MUST_USE Status parse_fail(Parser *p, Pos pos, StopClass cls, Str message) {
    if (cls == CLASS_SYNTAX && p->after_long && pos.line == p->tok.line && pos.column == p->tok.column) {
        Msg m;
        msg_init(&m, p->arena);
        msg_str(&m, message);
        msg_str(&m, S(" (the long string at "));
        msg_u64(&m, p->after_long_open.line);
        msg_str(&m, S(":"));
        msg_u64(&m, p->after_long_open.column);
        msg_str(&m, S(" closed at "));
        msg_u64(&m, p->after_long_string.close_line);
        msg_str(&m, S(":"));
        msg_u64(&m, p->after_long_string.close_column);
        msg_str(&m, S("; if it holds `"));
        if (m.s == OK) m.s = lex_closer_text(&m.b, p->after_long_string.level, 66);
        msg_str(&m, S("`, open it with `"));
        if (m.s == OK) m.s = lex_opener_text(&m.b, p->after_long_string.level + 1, 66);
        msg_str(&m, S("`)"));
        if (m.s == OK) message = buf_view(&m.b);
    }
    return parse_set_stop(p, pos, cls, message);
}

// Zig's `failf`: a stop whose message is made, the arena's refusal an
// OutOfMemory rather than a fallback.
static inline STATUS_MUST_USE Status parse_fail_msg(Parser *p, Pos pos, StopClass cls, const Msg *m) {
    if (m->s != OK) return STOP;
    return parse_fail(p, pos, cls, buf_view(&m->b));
}

// The pieces of a message, at most 16, as a literal: `(StrPieces){{a, b, c}}`.
// A struct, not C's varargs (TCC passes a struct through `...` wrongly) and
// not an array of unknown size (TCC fixes its size at its first use).
typedef struct {
    Str piece[16];
} StrPieces;

SLICE_TYPE(StrParts, const Str);

// A stop whose message is the first `n` of `parts` joined.
static inline STATUS_MUST_USE Status parse_fail_parts(Parser *p, Pos pos, StopClass cls, StrPieces parts, size_t n) {
    StrParts all = {parts.piece, n};
    Msg m;
    msg_init(&m, p->arena);
    for (size_t k = 0; k < all.len; k++) msg_str(&m, at(all, k));
    return parse_fail_msg(p, pos, cls, &m);
}

// A stop whose message is `n` pieces joined.
#define parse_fail_cat(p, pos, cls, n, ...) parse_fail_parts((p), (pos), (cls), (StrPieces){{__VA_ARGS__}}, (n))

// `n` in decimal, in the arena; "" when refused, which the message then
// carries as Zig's would carry nothing.
static inline Str parse_num(Parser *p, uint64_t n, bool *refused) {
    Buf b = buf_make(p->arena);
    if (buf_u64(&b, n) != OK) {
        *refused = true;
        return S("");
    }
    return buf_view(&b);
}

static inline Pos parse_here(const Parser *p) {
    Pos pos = {p->tok.line, p->tok.column};
    return pos;
}

static inline Pos parse_pos(uint32_t line, uint32_t column) {
    Pos pos = {line, column};
    return pos;
}

// A token as a message names it: words for the end, a string and a name or
// number shown, any other token quoted. Each made one falls back, as Zig's
// `describe` does, when the arena refuses.
static inline Str parse_describe(Parser *p, Token t) {
    switch (t.tag) {
    case TAG_EOF: return S("the end of the script");
    case TAG_STRING: return S("a string");
    case TAG_QUESTION: return S("`?`, which marks an optional parameter (`function f(a, b?)`) and nothing else");
    default: break;
    }
    Msg m;
    msg_init(&m, p->arena);
    if (t.tag == TAG_NAME) {
        msg_str(&m, S("the name `"));
        msg_str(&m, t.text);
        msg_str(&m, S("`"));
        return m.s == OK ? buf_view(&m.b) : S("a name");
    }
    if (t.tag == TAG_INT) {
        msg_str(&m, S("the number "));
        msg_str(&m, t.text);
        return m.s == OK ? buf_view(&m.b) : S("a number");
    }
    msg_str(&m, S("`"));
    msg_str(&m, t.text);
    msg_str(&m, S("`"));
    return m.s == OK ? buf_view(&m.b) : t.text;
}

// ── tokens ──

static inline STATUS_MUST_USE Status parse_lex_failed(Parser *p) {
    if (p->lexer.oom) return STOP;
    LexError e = p->lexer.err;
    return parse_fail(p, parse_pos(e.line, e.column), e.refused ? CLASS_REFUSED : CLASS_SYNTAX, e.message);
}

static inline STATUS_MUST_USE Status parse_advance(Parser *p) {
    p->after_long = p->tok.tag == TAG_STRING && p->tok.is_long;
    if (p->after_long) {
        p->after_long_open = parse_here(p);
        p->after_long_string = p->tok.long_string;
    }
    if (p->has_ahead) {
        p->tok = p->ahead;
        p->has_ahead = false;
        return OK;
    }
    if (lexer_next(&p->lexer, &p->tok) != OK) return parse_lex_failed(p);
    return OK;
}

static inline STATUS_MUST_USE Status parse_peek(Parser *p, Token *out) {
    if (!p->has_ahead) {
        if (lexer_next(&p->lexer, &p->ahead) != OK) return parse_lex_failed(p);
        p->has_ahead = true;
    }
    *out = p->ahead;
    return OK;
}

// The token, which must be `tag`, then the next; else "expected `what`".
static inline STATUS_MUST_USE Status parse_expect(Parser *p, Tag tag, Str what, Token *out) {
    if (p->tok.tag != tag) return parse_fail_cat(p, parse_here(p), CLASS_SYNTAX, 4, S("expected "), what, S(", found "), parse_describe(p, p->tok));
    *out = p->tok;
    return parse_advance(p);
}

// Charges one level of the nesting budget. Every recursive production calls
// it, which is what bounds the parser's stack.
// DRAGON: the C stack. A source nested deeper than the budget would recurse
// the parser off its stack and crash the process. Every recursive production
// (block, statement, binary, unary's operator, suffixed, table) enters here,
// and the budget (200 unset) bounds the depth; loading a prelude starts its
// count again, but a prelude's own nesting is fixed and small.
static inline STATUS_MUST_USE Status parse_enter(Parser *p) {
    p->nesting++;
    if (p->nesting > p->max_nesting) {
        Status s = parse_set_stop(p, parse_here(p), CLASS_BUDGET, S("the source nests deeper than the nesting budget"));
        p->stop.budget = BUDGET_NESTING;
        return s;
    }
    return OK;
}

static inline void parse_leave(Parser *p) {
    p->nesting--;
}

static inline Expr *parse_new_expr(Parser *p, Pos pos, ExprKind kind) {
    Expr *e = ARENA_NEW(p->arena, Expr);
    if (e == NULL) return NULL;
    memset(e, 0, sizeof *e);
    e->pos = pos;
    e->kind = kind;
    return e;
}

// A copy, at its exact size, of the `n` items from `from` of a working list.
static inline STATUS_MUST_USE Status parse_dup(Parser *p, const void *items, size_t from, size_t n, size_t elem, size_t align, void **out) {
    if (n == 0) {
        *out = arena_empty(align);
        return OK;
    }
    Bytes all = {(char *)items, (from + n) * elem};
    Bytes part = SLICE_SUB(Bytes, all, from * elem, (from + n) * elem);
    void *fresh = arena_alloc(p->arena, part.len, align);
    if (fresh == NULL) return STOP;
    memcpy(fresh, part.ptr, part.len);
    *out = fresh;
    return OK;
}

// ── names ──

// The slot of the innermost local `name`, or -1.
static inline int32_t parse_lookup(const Parser *p, Str name) {
    for (size_t i = p->frame.locals.len; i > 0; i--) {
        Local l = at(p->frame.locals, i - 1);
        if (str_eq(l.name, name)) return l.slot;
    }
    return -1;
}

// Declares a local in this block. A local may take a builtin's name: a value
// by that name is the local, while a call by it is still the builtin, since
// only functions are called.
static inline STATUS_MUST_USE Status parse_declare(Parser *p, Str name, Pos pos, uint16_t *slot) {
    for (size_t i = p->frame.locals.len; i > 0; i--) {
        Local l = at(p->frame.locals, i - 1);
        if (l.depth < p->frame.depth) break;
        if (str_eq(l.name, name)) return parse_fail_cat(p, pos, CLASS_NAME, 3, S("`"), name, S("` is already a local in this block"));
    }
    if (p->frame.slots == UINT16_MAX) return parse_fail(p, pos, CLASS_SYNTAX, S("too many locals in one function"));
    *slot = p->frame.slots;
    p->frame.slots++;
    Local l = {name, *slot, p->frame.depth, false};
    return LocalVec_push(&p->frame.locals, l);
}

// A top-level `const` by name, among those declared so far, or -1.
static inline int32_t parse_constant(const Parser *p, Str name) {
    for (size_t i = 0; i < p->consts.len; i++) {
        if (!str_eq(at(p->consts, i), name)) continue;
        if (p->in_prelude != PARSE_SCRIPT && at(p->const_owner, i) != p->in_prelude) continue;
        return (int32_t)i;
    }
    return -1;
}

// Whether the local in `slot` of this frame is a `const`. A name resolved as
// this frame's local is in this frame's list.
static inline bool parse_is_const(const Parser *p, uint16_t slot) {
    for (size_t i = 0; i < p->frame.locals.len; i++) {
        if (at(p->frame.locals, i).slot == slot) return at(p->frame.locals, i).constant;
    }
    fprintf(stderr, "parse: a local's slot that is in no frame\n");
    abort();
    return false;
}

static inline bool parse_frame_has(const Frame *f, Str name) {
    for (size_t i = 0; i < f->locals.len; i++) {
        if (str_eq(at(f->locals, i).name, name)) return true;
    }
    return false;
}

// Whether the script assigns `name` anywhere but its `local`'s own line:
// `name = ...`, not `local name = ...` or `x.name = ...`. A function's own
// local of that name counts too, so the answer leans to "assigned again",
// whose fix (a parameter) is right either way; so does a source that doesn't
// lex, or an arena that refuses.
static inline bool parse_assigned_again(Arena *a, Str src, Str name) {
    Lexer lx;
    lexer_init(&lx, a, src);
    Tag before = TAG_EOF;
    Token current = {0};
    if (lexer_next(&lx, &current) != OK) return true;
    while (current.tag != TAG_EOF) {
        Token after = {0};
        if (lexer_next(&lx, &after) != OK) return true;
        if (current.tag == TAG_NAME && str_eq(current.text, name) && after.tag == TAG_ASSIGN && before != TAG_KW_LOCAL && before != TAG_DOT) return true;
        before = current.tag;
        current = after;
    }
    return false;
}

static inline STATUS_MUST_USE Status parse_unknown_name(Parser *p, Str name, Pos pos) {
    Str why = parse_lua_name(name);
    if (why.len > 0) return parse_fail(p, pos, CLASS_REFUSED, why);
    // A script `local` the function can't see: the stop names the fix, `const`
    // for a value set once, a parameter for one that changes.
    if (p->in_function && p->in_prelude == PARSE_SCRIPT && p->script_frame != NULL && parse_frame_has(p->script_frame, name)) {
        if (parse_assigned_again(p->arena, p->lexer.src, name))
            return parse_fail_cat(p, pos, CLASS_NAME, 5, S("`"), name, S("` is a script `local` that is assigned again, and a function sees only its own parameters and locals: pass `"), name, S("` in as a parameter"));
        return parse_fail_cat(p, pos, CLASS_NAME, 5, S("`"), name, S("` is a script `local` set once, and a function sees only its own parameters and locals and the `const`s above it: declare it `const "), name, S(" = ...` where it is set"));
    }
    if (p->in_function) return parse_fail_cat(p, pos, CLASS_NAME, 3, S("no local `"), name, S("` here: a function sees only its own parameters and locals, and the script's `const`s declared above it (Shale's builtins and preludes: `shale --help`)"));
    return parse_fail_cat(p, pos, CLASS_NAME, 5, S("no local `"), name, S("`: declare it with `local "), name, S(" = ...` (there are no globals; Shale's builtins and preludes: `shale --help`)"));
}

// ── the preludes' indexes ──

// The next line from `*at` on that begins `function name(`, as Zig's
// `nextFunction`; false at the end.
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

typedef bool (*NextEntry)(Str src, size_t *at_, uint32_t *line, PreludeFunction *out);

// Every function (or `const`) a source holds by its lines.
static inline STATUS_MUST_USE Status parse_index(Arena *a, Str src, NextEntry next, PreludeFunctions *out) {
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

static inline STATUS_MUST_USE Status parse_slots(Arena *a, size_t n, LoadedSlots *out) {
    uint32_t *slots = ARENA_ARRAY(a, uint32_t, n);
    if (slots == NULL) return STOP;
    LoadedSlots s = {slots, n};
    for (size_t i = 0; i < n; i++) at(s, i) = PARSE_UNLOADED;
    *out = s;
    return OK;
}

// The name a prelude's functions are shown under: "the prelude" for the
// core's, else its own ("the Linux prelude").
static inline Str parse_prelude_name(const Parser *p, size_t k) {
    return k == 0 ? S("the prelude") : at(p->extra, k - 1).prelude.name;
}

// ── statements ──

static inline STATUS_MUST_USE Status parse_block(Parser *p, Stmts *out);
static inline STATUS_MUST_USE Status parse_expr(Parser *p, Expr **out);
static inline STATUS_MUST_USE Status parse_suffixed(Parser *p, Expr **out);
static inline STATUS_MUST_USE Status parse_function(Parser *p, Pos pos);
static inline STATUS_MUST_USE Status parse_use_statement(Parser *p, Pos pos, Stmt *out);

static inline bool parse_block_ends(Tag tag) {
    return tag == TAG_EOF || tag == TAG_KW_END || tag == TAG_KW_ELSE || tag == TAG_KW_ELSEIF;
}

static inline void parse_stmt_init(Stmt *s, Pos pos, StmtKind kind) {
    memset(s, 0, sizeof *s);
    s->pos = pos;
    s->kind = kind;
}

// The statements at the stack's top from `from`, copied out.
static inline STATUS_MUST_USE Status parse_take_stmts(Parser *p, size_t from, Stmts *out) {
    void *items = NULL;
    TRY(parse_dup(p, p->stmts.ptr, from, p->stmts.len - from, sizeof(Stmt), _Alignof(Stmt), &items));
    Stmts s = {items, p->stmts.len - from};
    *out = s;
    p->stmts.len = from;
    return OK;
}

static inline STATUS_MUST_USE Status parse_take_exprs(Parser *p, size_t from, Exprs *out) {
    void *items = NULL;
    TRY(parse_dup(p, p->exprs.ptr, from, p->exprs.len - from, sizeof(Expr *), _Alignof(Expr *), &items));
    Exprs s = {items, p->exprs.len - from};
    *out = s;
    p->exprs.len = from;
    return OK;
}

static inline STATUS_MUST_USE Status parse_if_statement(Parser *p, Pos pos, Stmt *out) {
    TRY(parse_advance(p));
    size_t from = p->arms.len;
    Token t = {0};
    Expr *cond = NULL;
    TRY(parse_expr(p, &cond));
    TRY(parse_expect(p, TAG_KW_THEN, S("`then` after the `if` condition"), &t));
    Arm first = {0};
    first.cond = cond;
    TRY(parse_block(p, &first.body));
    TRY(ArmVec_push(&p->arms, first));
    Stmt s;
    parse_stmt_init(&s, pos, STMT_IF);
    for (;;) {
        if (p->tok.tag == TAG_KW_ELSEIF) {
            TRY(parse_advance(p));
            Arm arm = {0};
            TRY(parse_expr(p, &arm.cond));
            TRY(parse_expect(p, TAG_KW_THEN, S("`then` after the `elseif` condition"), &t));
            TRY(parse_block(p, &arm.body));
            TRY(ArmVec_push(&p->arms, arm));
        } else if (p->tok.tag == TAG_KW_ELSE) {
            TRY(parse_advance(p));
            s.as.if_.has_otherwise = true;
            TRY(parse_block(p, &s.as.if_.otherwise));
            TRY(parse_expect(p, TAG_KW_END, S("`end` to close the `if`"), &t));
            break;
        } else {
            TRY(parse_expect(p, TAG_KW_END, S("`end` to close the `if`"), &t));
            break;
        }
    }
    void *items = NULL;
    TRY(parse_dup(p, p->arms.ptr, from, p->arms.len - from, sizeof(Arm), _Alignof(Arm), &items));
    Arms arms = {items, p->arms.len - from};
    s.as.if_.arms = arms;
    p->arms.len = from;
    *out = s;
    return OK;
}

// A loop's body, with its name declared in a scope of its own.
static inline STATUS_MUST_USE Status parse_loop_body(Parser *p, Token name_tok, uint16_t *slot, Stmts *body) {
    p->frame.depth++;
    size_t mark = p->frame.locals.len;
    TRY(parse_declare(p, name_tok.text, parse_pos(name_tok.line, name_tok.column), slot));
    p->frame.loops++;
    TRY(parse_block(p, body));
    p->frame.loops--;
    p->frame.locals.len = mark;
    p->frame.depth--;
    Token t = {0};
    return parse_expect(p, TAG_KW_END, S("`end` to close the `for`"), &t);
}

static inline STATUS_MUST_USE Status parse_for_statement(Parser *p, Pos pos, Stmt *out) {
    TRY(parse_advance(p));
    Token name_tok = {0};
    Token t = {0};
    TRY(parse_expect(p, TAG_NAME, S("a name after `for`"), &name_tok));
    if (p->tok.tag == TAG_COMMA) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("several loop names are not in this language: `for k in keys(m) do ... m[k] ... end`"));
    if (p->tok.tag == TAG_ASSIGN) {
        Stmt s;
        parse_stmt_init(&s, pos, STMT_NUMERIC_FOR);
        TRY(parse_advance(p));
        TRY(parse_expr(p, &s.as.numeric_for.from));
        TRY(parse_expect(p, TAG_COMMA, S("`,` between the loop's start and end"), &t));
        TRY(parse_expr(p, &s.as.numeric_for.to));
        s.as.numeric_for.step = NULL;
        if (p->tok.tag == TAG_COMMA) {
            TRY(parse_advance(p));
            TRY(parse_expr(p, &s.as.numeric_for.step));
        }
        TRY(parse_expect(p, TAG_KW_DO, S("`do` after the `for`"), &t));
        TRY(parse_loop_body(p, name_tok, &s.as.numeric_for.slot, &s.as.numeric_for.body));
        *out = s;
        return OK;
    }
    Stmt s;
    parse_stmt_init(&s, pos, STMT_LIST_FOR);
    TRY(parse_expect(p, TAG_KW_IN, S("`=` or `in` after the loop's name"), &t));
    TRY(parse_expr(p, &s.as.list_for.list));
    TRY(parse_expect(p, TAG_KW_DO, S("`do` after the `for`"), &t));
    TRY(parse_loop_body(p, name_tok, &s.as.list_for.slot, &s.as.list_for.body));
    *out = s;
    return OK;
}

// A `local` or `const` declaration's value, refusing a second one.
static inline STATUS_MUST_USE Status parse_single_value(Parser *p, Expr **out) {
    TRY(parse_expr(p, out));
    if (p->tok.tag == TAG_COMMA) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("multiple values are not in this language"));
    return OK;
}

static inline STATUS_MUST_USE Status parse_statement_body(Parser *p, Stmt *out) {
    Pos pos = parse_here(p);
    Token t = {0};
    switch (p->tok.tag) {
    case TAG_KW_LOCAL: {
        TRY(parse_advance(p));
        if (p->tok.tag == TAG_KW_FUNCTION) return parse_fail(p, pos, CLASS_REFUSED, S("`local function` is not in this language: functions are declared at the top level with `function name(...)`"));
        TRY(parse_expect(p, TAG_NAME, S("a name after `local`"), &t));
        if (p->tok.tag == TAG_COMMA) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("declaring several locals at once is not in this language: one `local` each"));
        if (p->tok.tag == TAG_LT) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("attributes (`<const>`, `<close>`) are not in this language"));
        Stmt s;
        parse_stmt_init(&s, pos, STMT_LOCAL);
        s.as.local.value = NULL;
        if (p->tok.tag == TAG_ASSIGN) {
            TRY(parse_advance(p));
            TRY(parse_single_value(p, &s.as.local.value));
        }
        // Declared after its value: `local x = x` reads an outer x.
        TRY(parse_declare(p, t.text, pos, &s.as.local.slot));
        *out = s;
        return OK;
    }
    case TAG_KW_CONST: {
        if (p->in_function || p->frame.depth > 1) return parse_fail(p, pos, CLASS_REFUSED, S("`const` is a top-level statement only: a function or a block declares `local`s"));
        TRY(parse_advance(p));
        TRY(parse_expect(p, TAG_NAME, S("a name after `const`"), &t));
        if (p->tok.tag != TAG_ASSIGN) return parse_fail(p, parse_here(p), CLASS_SYNTAX, S("a `const` needs its value: `const NAME = ...`"));
        TRY(parse_advance(p));
        Stmt s;
        parse_stmt_init(&s, pos, STMT_CONSTANT);
        TRY(parse_single_value(p, &s.as.constant.value));
        TRY(parse_declare(p, t.text, pos, &s.as.constant.slot));
        at(p->frame.locals, p->frame.locals.len - 1).constant = true;
        if (p->consts.len == UINT16_MAX) return parse_fail(p, pos, CLASS_SYNTAX, S("too many constants"));
        s.as.constant.index = (uint16_t)p->consts.len;
        TRY(StrVec_push(&p->consts, t.text));
        TRY(OwnerVec_push(&p->const_owner, p->in_prelude));
        *out = s;
        return OK;
    }
    case TAG_KW_IF:
        return parse_if_statement(p, pos, out);
    case TAG_KW_WHILE: {
        TRY(parse_advance(p));
        Stmt s;
        parse_stmt_init(&s, pos, STMT_WHILE);
        TRY(parse_expr(p, &s.as.while_.cond));
        TRY(parse_expect(p, TAG_KW_DO, S("`do` after the `while` condition"), &t));
        p->frame.loops++;
        TRY(parse_block(p, &s.as.while_.body));
        p->frame.loops--;
        TRY(parse_expect(p, TAG_KW_END, S("`end` to close the `while`"), &t));
        *out = s;
        return OK;
    }
    case TAG_KW_FOR:
        return parse_for_statement(p, pos, out);
    case TAG_KW_DO: {
        TRY(parse_advance(p));
        Stmt s;
        parse_stmt_init(&s, pos, STMT_DO);
        TRY(parse_block(p, &s.as.do_));
        TRY(parse_expect(p, TAG_KW_END, S("`end` to close the `do`"), &t));
        *out = s;
        return OK;
    }
    case TAG_KW_RETURN: {
        TRY(parse_advance(p));
        Stmt s;
        parse_stmt_init(&s, pos, STMT_RETURN);
        s.as.return_ = NULL;
        if (!parse_block_ends(p->tok.tag) && p->tok.tag != TAG_SEMICOLON) {
            TRY(parse_expr(p, &s.as.return_));
            if (p->tok.tag == TAG_COMMA) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("multiple returns are not in this language: return a list or a map"));
        }
        *out = s;
        return OK;
    }
    case TAG_KW_BREAK:
        TRY(parse_advance(p));
        if (p->frame.loops == 0) return parse_fail(p, pos, CLASS_SYNTAX, S("`break` outside a loop"));
        parse_stmt_init(out, pos, STMT_BREAK);
        return OK;
    case TAG_KW_CONTINUE:
        TRY(parse_advance(p));
        if (p->frame.loops == 0) return parse_fail(p, pos, CLASS_SYNTAX, S("`continue` outside a loop"));
        parse_stmt_init(out, pos, STMT_CONTINUE);
        return OK;
    case TAG_KW_FUNCTION:
        if (p->in_function || p->frame.depth > 1) return parse_fail(p, pos, CLASS_REFUSED, S("functions are top-level statements only: nested functions and closures are not in this language"));
        TRY(parse_function(p, pos));
        parse_stmt_init(out, pos, STMT_DO);
        return OK;
    default:
        break;
    }
    if (p->tok.tag == TAG_NAME && str_eq(p->tok.text, S("use"))) {
        Token next = {0};
        TRY(parse_peek(p, &next));
        if (next.tag == TAG_LPAREN) return parse_use_statement(p, pos, out);
    }
    Expr *target = NULL;
    TRY(parse_suffixed(p, &target));
    if (p->tok.tag == TAG_ASSIGN) {
        if (target->kind == EXPR_LOCAL) {
            if (parse_is_const(p, target->as.local)) return parse_fail(p, target->pos, CLASS_NAME, S("a `const` is never assigned: declare a `local` to change it"));
        } else if (target->kind == EXPR_CONSTANT) {
            return parse_fail(p, target->pos, CLASS_NAME, S("a `const` is never assigned: declare a `local` to change it"));
        } else if (target->kind != EXPR_INDEX) {
            return parse_fail(p, target->pos, CLASS_SYNTAX, S("only a local or an element `t[k]`, `t.k` can be assigned"));
        }
        TRY(parse_advance(p));
        Stmt s;
        parse_stmt_init(&s, pos, STMT_ASSIGN);
        s.as.assign.target = target;
        TRY(parse_expr(p, &s.as.assign.value));
        if (p->tok.tag == TAG_COMMA) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("multiple values are not in this language"));
        *out = s;
        return OK;
    }
    if (p->tok.tag == TAG_COMMA) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("multiple assignment is not in this language: one assignment each"));
    if (target->kind != EXPR_CALL) return parse_fail_cat(p, pos, CLASS_SYNTAX, 2, S("a statement must be a call or an assignment, found "), parse_describe(p, p->tok));
    Stmt s;
    parse_stmt_init(&s, pos, STMT_CALL);
    s.as.call = target;
    *out = s;
    return OK;
}

static inline STATUS_MUST_USE Status parse_statement(Parser *p, Stmt *out) {
    TRY(parse_enter(p));
    TRY(parse_statement_body(p, out));
    parse_leave(p);
    return OK;
}

// Statements up to the end of a block, in a scope of their own.
static inline STATUS_MUST_USE Status parse_block(Parser *p, Stmts *out) {
    TRY(parse_enter(p));
    p->frame.depth++;
    size_t mark = p->frame.locals.len;
    size_t from = p->stmts.len;
    while (!parse_block_ends(p->tok.tag)) {
        if (p->tok.tag == TAG_SEMICOLON) {
            TRY(parse_advance(p));
            continue;
        }
        Stmt s;
        TRY(parse_statement(p, &s));
        TRY(StmtVec_push(&p->stmts, s));
        if (s.kind == STMT_RETURN) {
            while (p->tok.tag == TAG_SEMICOLON) TRY(parse_advance(p));
            if (!parse_block_ends(p->tok.tag)) return parse_fail(p, parse_here(p), CLASS_SYNTAX, S("`return` must be the last statement of its block"));
            break;
        }
    }
    TRY(parse_take_stmts(p, from, out));
    p->frame.locals.len = mark;
    p->frame.depth--;
    parse_leave(p);
    return OK;
}

// ── functions and the preludes they load ──

static inline STATUS_MUST_USE Status parse_load_prelude(Parser *p, size_t k, size_t i);

// The library that brought `name`, among those `use`d above, or NULL.
static inline const Extra *parse_library_having(const Parser *p, Str name) {
    for (size_t k = 0; k < p->extra.len; k++) {
        const Extra *x = &at(p->extra, k);
        if (!x->is_library) continue;
        if (parse_index_at(x->index, name) >= 0 || parse_index_at(x->library.consts, name) >= 0) return x;
    }
    return NULL;
}

// The function a call by `name` is to: the script's own, declared above (never
// from inside a prelude); else a prelude's, parsed now if this is its first
// call. `*found` is false when there is none.
static inline STATUS_MUST_USE Status parse_function_named(Parser *p, Str name, uint32_t *index, bool *found) {
    *found = false;
    if (p->in_prelude == PARSE_SCRIPT && FunctionNames_get(&p->function_names, name, index)) {
        *found = true;
        return OK;
    }
    int32_t i = parse_index_at(p->core_index, name);
    if (i >= 0) {
        if (at(p->core_loaded, (size_t)i) == PARSE_UNLOADED) TRY(parse_load_prelude(p, 0, (size_t)i));
        *index = at(p->core_loaded, (size_t)i);
        *found = true;
        return OK;
    }
    size_t last = p->in_prelude != PARSE_SCRIPT ? p->in_prelude : p->extra.len;
    if (last > p->extra.len) last = p->extra.len;
    for (size_t k = 1; k <= last; k++) {
        int32_t j = parse_index_at(at(p->extra, k - 1).index, name);
        if (j < 0) continue;
        // A library calls the preludes and itself, never another library.
        if (at(p->extra, k - 1).is_library && p->in_prelude != PARSE_SCRIPT && p->in_prelude != k) continue;
        if (at(at(p->extra, k - 1).loaded, (size_t)j) == PARSE_UNLOADED) TRY(parse_load_prelude(p, k, (size_t)j));
        *index = at(at(p->extra, k - 1).loaded, (size_t)j);
        *found = true;
        return OK;
    }
    return OK;
}

// A function index the parse's own index says is there; a miss is a bug in
// the C (Zig's `.?` on null).
static inline size_t parse_index_sure(PreludeFunctions index, Str name) {
    int32_t i = parse_index_at(index, name);
    if (i < 0) {
        fprintf(stderr, "parse: a prelude function its index does not hold: %.*s\n", (int)name.len, name.ptr);
        abort();
    }
    return (size_t)i;
}

static inline STATUS_MUST_USE Status parse_params(Parser *p, uint16_t *params, uint16_t *optional) {
    Token t = {0};
    TRY(parse_expect(p, TAG_LPAREN, S("`(` after the function's name"), &t));
    p->frame.depth = 1;
    Str first_optional = S("");
    if (p->tok.tag != TAG_RPAREN) {
        for (;;) {
            if (p->tok.tag == TAG_DOT_DOT) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("varargs are not in this language"));
            Token param = {0};
            TRY(parse_expect(p, TAG_NAME, S("a parameter's name"), &param));
            uint16_t slot = 0;
            TRY(parse_declare(p, param.text, parse_pos(param.line, param.column), &slot));
            (*params)++;
            if (p->tok.tag == TAG_QUESTION) {
                if (*optional == 0) first_optional = param.text;
                (*optional)++;
                TRY(parse_advance(p));
            } else if (*optional > 0) {
                return parse_fail_cat(p, parse_pos(param.line, param.column), CLASS_SYNTAX, 5, S("`"), param.text, S("` is required after an optional parameter: the optional ones (`"), first_optional, S("?`) come last"));
            }
            if (p->tok.tag != TAG_COMMA) break;
            TRY(parse_advance(p));
        }
    }
    return parse_expect(p, TAG_RPAREN, S("`)` after the parameters"), &t);
}

static inline STATUS_MUST_USE Status parse_function(Parser *p, Pos pos) {
    TRY(parse_advance(p));
    Token name_tok = {0};
    TRY(parse_expect(p, TAG_NAME, S("the function's name"), &name_tok));
    Str name = name_tok.text;
    if (p->tok.tag == TAG_DOT) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("functions in tables are not in this language: functions are not values"));
    // A builtin's name is allowed: the script's calls by it are then to this
    // function.
    if (str_eq(name, S("attempt"))) return parse_fail(p, pos, CLASS_NAME, S("`attempt` is the language's own: name the function otherwise"));
    if (str_eq(name, S("use"))) return parse_fail(p, pos, CLASS_NAME, S("`use` is the language's own: name the function otherwise"));
    // A name a library brought is the script's from its `use` on, and is not
    // declared again, whichever comes first.
    if (p->in_prelude == PARSE_SCRIPT) {
        const Extra *x = parse_library_having(p, name);
        if (x != NULL) {
            bool refused = false;
            Str line = parse_num(p, at(x->index, parse_index_sure(x->index, name)).line, &refused);
            Str use_line = parse_num(p, x->library.use_line, &refused);
            if (refused) return STOP;
            return parse_fail_cat(p, pos, CLASS_NAME, 9, S("a function `"), name, S("` is already `use`d: "), x->prelude.name, S("'s (its line "), line, S("), by the `use` at line "), use_line, S(""));
        }
    }
    if (parse_lua_name(name).len > 0) return parse_fail_cat(p, pos, CLASS_NAME, 3, S("`"), name, S("` is a name of Lua's library, kept to say what to write instead: name the function otherwise"));
    // A prelude function's name is allowed too: from this line on the script's
    // calls by it are to this function.
    uint32_t seen = 0;
    if (p->in_prelude == PARSE_SCRIPT && FunctionNames_get(&p->function_names, name, &seen))
        return parse_fail_cat(p, pos, CLASS_NAME, 3, S("a function `"), name, S("` is already declared"));
    uint32_t index = (uint32_t)p->functions.len;
    // An error inside a prelude function must say so: its line is the
    // prelude's, not the script's.
    Str shown = name;
    if (p->in_prelude != PARSE_SCRIPT) {
        Msg m;
        msg_init(&m, p->arena);
        msg_str(&m, parse_prelude_name(p, p->in_prelude));
        msg_str(&m, S("'s "));
        msg_str(&m, name);
        if (m.s != OK) return STOP;
        shown = buf_view(&m.b);
        if (p->in_prelude == 0) {
            at(p->core_loaded, parse_index_sure(p->core_index, name)) = index;
        } else {
            Extra *x = &at(p->extra, p->in_prelude - 1);
            at(x->loaded, parse_index_sure(x->index, name)) = index;
        }
    } else {
        TRY(FunctionNames_put(&p->function_names, name, index));
    }
    // Reserved now, so it may call itself; known before the body, so the body
    // may call it.
    Function f;
    memset(&f, 0, sizeof f);
    f.name = shown;
    f.pos = pos;
    TRY(FunctionVec_push(&p->functions, f));

    Frame saved = p->frame;
    bool saved_in_function = p->in_function;
    const Frame *saved_script_frame = p->script_frame;
    // A prelude's function, loaded at its first call, may be parsed in the
    // middle of the script's: only the script's own top level counts.
    if (!saved_in_function && p->in_prelude == PARSE_SCRIPT) p->script_frame = &saved;
    memset(&p->frame, 0, sizeof p->frame);
    p->frame.locals = LocalVec_make(p->arena);
    p->in_function = true;
    uint16_t params = 0;
    uint16_t optional = 0;
    TRY(parse_params(p, &params, &optional));
    at(p->functions, index).params = params;
    at(p->functions, index).optional = optional;
    Stmts body = {0};
    TRY(parse_block(p, &body));
    Token t = {0};
    TRY(parse_expect(p, TAG_KW_END, S("`end` to close the function"), &t));
    at(p->functions, index).frame = p->frame.slots;
    at(p->functions, index).body = body;
    p->frame = saved;
    p->in_function = saved_in_function;
    p->script_frame = saved_script_frame;
    return OK;
}

// Parses function `i` of prelude `k` at its first call, from its own line, as
// if the prelude had been parsed before the script: it sees no `const` or local
// of the script's and is charged none of the calling depth.
static inline STATUS_MUST_USE Status parse_load_prelude(Parser *p, size_t k, size_t i) {
    Lexer saved_lexer = p->lexer;
    Token saved_tok = p->tok;
    bool saved_has_ahead = p->has_ahead;
    Token saved_ahead = p->ahead;
    size_t saved_in_prelude = p->in_prelude;
    uint32_t saved_nesting = p->nesting;
    Str src = k == 0 ? p->core : at(p->extra, k - 1).prelude.source;
    PreludeFunction f = k == 0 ? at(p->core_index, i) : at(at(p->extra, k - 1).index, i);
    lexer_init(&p->lexer, p->arena, src);
    p->lexer.at = f.at;
    p->lexer.line = f.line;
    p->has_ahead = false;
    p->in_prelude = k;
    p->nesting = 0;
    TRY(parse_advance(p));
    TRY(parse_function(p, parse_here(p)));
    p->lexer = saved_lexer;
    p->tok = saved_tok;
    p->has_ahead = saved_has_ahead;
    p->ahead = saved_ahead;
    p->in_prelude = saved_in_prelude;
    p->nesting = saved_nesting;
    return OK;
}

// ── expressions ──

typedef struct {
    BinOp op;
    uint8_t prec;
    bool right;
} Level;

static inline bool parse_binop(Tag tag, Level *out) {
    Level l = {BINOP_ADD, 0, false};
    switch (tag) {
    case TAG_KW_OR: l.op = BINOP_OR; l.prec = 1; break;
    case TAG_KW_AND: l.op = BINOP_AND; l.prec = 2; break;
    case TAG_LT: l.op = BINOP_LT; l.prec = 3; break;
    case TAG_LE: l.op = BINOP_LE; l.prec = 3; break;
    case TAG_GT: l.op = BINOP_GT; l.prec = 3; break;
    case TAG_GE: l.op = BINOP_GE; l.prec = 3; break;
    case TAG_EQ_EQ: l.op = BINOP_EQ; l.prec = 3; break;
    case TAG_NOT_EQ: l.op = BINOP_NE; l.prec = 3; break;
    case TAG_DOT_DOT: l.op = BINOP_CONCAT; l.prec = 4; l.right = true; break;
    case TAG_PLUS: l.op = BINOP_ADD; l.prec = 5; break;
    case TAG_MINUS: l.op = BINOP_SUB; l.prec = 5; break;
    case TAG_STAR: l.op = BINOP_MUL; l.prec = 6; break;
    case TAG_SLASH_SLASH: l.op = BINOP_IDIV; l.prec = 6; break;
    case TAG_PERCENT: l.op = BINOP_MOD; l.prec = 6; break;
    default: return false;
    }
    *out = l;
    return true;
}

static inline STATUS_MUST_USE Status parse_unary(Parser *p, Expr **out);

// Precedence climbing: operators of at least `min` bind here; `..` binds to
// the right, every other to the left.
static inline STATUS_MUST_USE Status parse_binary(Parser *p, uint8_t min, Expr **out) {
    TRY(parse_enter(p));
    Expr *left = NULL;
    TRY(parse_unary(p, &left));
    Level lv = {0};
    while (parse_binop(p->tok.tag, &lv)) {
        if (lv.prec < min) break;
        Pos pos = parse_here(p);
        TRY(parse_advance(p));
        Expr *right = NULL;
        TRY(parse_binary(p, lv.right ? lv.prec : (uint8_t)(lv.prec + 1), &right));
        // Against a literal `nil`, `==` and `~=` become their own ops, which
        // any value may be in.
        BinOp op = lv.op;
        if (left->kind == EXPR_NIL || right->kind == EXPR_NIL) {
            if (op == BINOP_EQ) op = BINOP_EQ_NIL;
            if (op == BINOP_NE) op = BINOP_NE_NIL;
        }
        Expr *e = parse_new_expr(p, pos, EXPR_BINARY);
        if (e == NULL) return STOP;
        e->as.binary.op = op;
        e->as.binary.left = left;
        e->as.binary.right = right;
        left = e;
    }
    *out = left;
    parse_leave(p);
    return OK;
}

static inline STATUS_MUST_USE Status parse_expr(Parser *p, Expr **out) {
    return parse_binary(p, 0, out);
}

static inline STATUS_MUST_USE Status parse_unary(Parser *p, Expr **out) {
    Pos pos = parse_here(p);
    UnOp op;
    if (p->tok.tag == TAG_KW_NOT) {
        op = UNOP_NOT;
    } else if (p->tok.tag == TAG_MINUS) {
        op = UNOP_NEG;
    } else if (p->tok.tag == TAG_HASH) {
        op = UNOP_LEN;
    } else {
        return parse_suffixed(p, out);
    }
    TRY(parse_enter(p));
    TRY(parse_advance(p));
    // The magnitude of the smallest integer is no int64, so only a minus right
    // before it makes it one.
    if (op == UNOP_NEG && p->tok.tag == TAG_INT && p->tok.min_magnitude) {
        TRY(parse_advance(p));
        Expr *e = parse_new_expr(p, pos, EXPR_INT);
        if (e == NULL) return STOP;
        e->as.integer = INT64_MIN;
        *out = e;
        parse_leave(p);
        return OK;
    }
    Expr *operand = NULL;
    TRY(parse_unary(p, &operand));
    // A minus folds into its literal, unless that overflows
    // (`-(-9223372036854775808)`), which is left to fail at run time.
    if (op == UNOP_NEG && operand->kind == EXPR_INT && operand->as.integer != INT64_MIN) {
        operand->as.integer = -operand->as.integer;
        operand->pos = pos;
        *out = operand;
        parse_leave(p);
        return OK;
    }
    Expr *e = parse_new_expr(p, pos, EXPR_UNARY);
    if (e == NULL) return STOP;
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    *out = e;
    parse_leave(p);
    return OK;
}

static inline STATUS_MUST_USE Status parse_call(Parser *p, Str name, Pos pos, Expr **out);
static inline STATUS_MUST_USE Status parse_table(Parser *p, Pos pos, Expr **out);

static inline STATUS_MUST_USE Status parse_primary(Parser *p, Expr **out) {
    Pos pos = parse_here(p);
    Token t = p->tok;
    Expr *e = NULL;
    switch (t.tag) {
    case TAG_KW_NIL:
        TRY(parse_advance(p));
        e = parse_new_expr(p, pos, EXPR_NIL);
        break;
    case TAG_KW_TRUE:
    case TAG_KW_FALSE:
        TRY(parse_advance(p));
        e = parse_new_expr(p, pos, EXPR_BOOLEAN);
        if (e != NULL) e->as.boolean = t.tag == TAG_KW_TRUE;
        break;
    case TAG_INT:
        if (t.min_magnitude) return parse_fail(p, pos, CLASS_SYNTAX, S("an integer literal too big for 64 bits (only -9223372036854775808, with its minus, is the smallest integer)"));
        TRY(parse_advance(p));
        e = parse_new_expr(p, pos, EXPR_INT);
        if (e != NULL) e->as.integer = t.value;
        break;
    case TAG_STRING:
        TRY(parse_advance(p));
        e = parse_new_expr(p, pos, EXPR_STRING);
        if (e != NULL) e->as.string = t.text;
        break;
    case TAG_LPAREN: {
        TRY(parse_advance(p));
        TRY(parse_expr(p, &e));
        Token close = {0};
        TRY(parse_expect(p, TAG_RPAREN, S("`)`"), &close));
        break;
    }
    case TAG_LBRACE:
        return parse_table(p, pos, out);
    case TAG_KW_FUNCTION:
        return parse_fail(p, pos, CLASS_REFUSED, S("functions are not values in this language: declare one at the top level with `function name(...)`"));
    case TAG_NAME: {
        TRY(parse_advance(p));
        if (p->tok.tag == TAG_LPAREN) return parse_call(p, t.text, pos, out);
        if (p->tok.tag == TAG_STRING || p->tok.tag == TAG_LBRACE) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("a call needs its parentheses: `f(\"x\")`, not `f \"x\"`"));
        int32_t slot = parse_lookup(p, t.text);
        int32_t ci = p->in_function ? parse_constant(p, t.text) : -1;
        if (slot >= 0) {
            e = parse_new_expr(p, pos, EXPR_LOCAL);
            if (e != NULL) e->as.local = (uint16_t)slot;
        } else if (ci >= 0) {
            e = parse_new_expr(p, pos, EXPR_CONSTANT);
            if (e != NULL) e->as.constant = (uint16_t)ci;
        } else {
            // `table.insert` is Lua's library, though `table` is a builtin.
            Str why = parse_lua_name(t.text);
            if (p->tok.tag == TAG_DOT && why.len > 0) return parse_fail(p, pos, CLASS_REFUSED, why);
            Builtin b = {0};
            if (ast_builtin_named(t.text, &b)) return parse_fail_cat(p, pos, CLASS_REFUSED, 5, S("`"), t.text, S("` is a builtin, not a value: call it, `"), t.text, S("(...)`"));
            return parse_unknown_name(p, t.text, pos);
        }
        break;
    }
    default:
        return parse_fail_cat(p, pos, CLASS_SYNTAX, 2, S("expected a value, found "), parse_describe(p, t));
    }
    if (e == NULL) return STOP;
    *out = e;
    return OK;
}

static inline STATUS_MUST_USE Status parse_suffixed(Parser *p, Expr **out) {
    TRY(parse_enter(p));
    Expr *e = NULL;
    TRY(parse_primary(p, &e));
    for (;;) {
        Pos pos = parse_here(p);
        if (p->tok.tag == TAG_DOT) {
            TRY(parse_advance(p));
            Token key = {0};
            TRY(parse_expect(p, TAG_NAME, S("a name after `.`"), &key));
            Expr *k = parse_new_expr(p, parse_pos(key.line, key.column), EXPR_STRING);
            if (k == NULL) return STOP;
            k->as.string = key.text;
            Expr *ix = parse_new_expr(p, pos, EXPR_INDEX);
            if (ix == NULL) return STOP;
            ix->as.index.obj = e;
            ix->as.index.key = k;
            e = ix;
        } else if (p->tok.tag == TAG_LBRACKET) {
            TRY(parse_advance(p));
            Expr *k = NULL;
            TRY(parse_expr(p, &k));
            Token close = {0};
            TRY(parse_expect(p, TAG_RBRACKET, S("`]`"), &close));
            Expr *ix = parse_new_expr(p, pos, EXPR_INDEX);
            if (ix == NULL) return STOP;
            ix->as.index.obj = e;
            ix->as.index.key = k;
            e = ix;
        } else if (p->tok.tag == TAG_LPAREN) {
            return parse_fail(p, pos, CLASS_REFUSED, S("only a name can be called: functions are not values"));
        } else if ((p->tok.tag == TAG_STRING || p->tok.tag == TAG_LBRACE) && e->kind != EXPR_CALL) {
            return parse_fail(p, pos, CLASS_REFUSED, S("a call needs its parentheses: `f(\"x\")`, not `f \"x\"`"));
        } else {
            break;
        }
    }
    *out = e;
    parse_leave(p);
    return OK;
}

// `attempt(f, ...)`'s function, by name, and the `,` or `)` after it.
static inline STATUS_MUST_USE Status parse_attempt_target(Parser *p, CallExpr *call, Str *shown, int32_t *params, uint16_t *optional) {
    TRY(parse_advance(p));
    Token f = {0};
    TRY(parse_expect(p, TAG_NAME, S("the name of the function `attempt` calls"), &f));
    if (p->tok.tag == TAG_LPAREN) return parse_fail(p, parse_here(p), CLASS_SYNTAX, S("`attempt(f, a, b)` calls `f` itself: name it, without its own parentheses"));
    uint32_t i = 0;
    bool found = 0;
    TRY(parse_function_named(p, f.text, &i, &found));
    if (!found) return parse_fail_cat(p, parse_pos(f.line, f.column), CLASS_NAME, 3, S("`attempt` calls a function of the script declared above it, and there is no function `"), f.text, S("` above this line"));
    call->target = TARGET_ATTEMPT;
    call->function = i;
    *params = at(p->functions, i).params;
    *optional = at(p->functions, i).optional;
    *shown = f.text;
    if (p->tok.tag == TAG_COMMA) {
        TRY(parse_advance(p));
        if (p->tok.tag == TAG_RPAREN) return parse_fail(p, parse_here(p), CLASS_SYNTAX, S("expected a value after `,`, found `)`"));
    } else if (p->tok.tag != TAG_RPAREN) {
        return parse_fail_cat(p, parse_here(p), CLASS_SYNTAX, 2, S("expected `,` or `)` after the function's name, found "), parse_describe(p, p->tok));
    }
    return OK;
}

// A call by `name`, resolved where it stands. A name that is a value here (a
// local, a parameter, a `const`) is refused; otherwise it is the script's own
// function declared above (or the one being declared), then a prelude's, then
// the builtin, then Lua's library, refused by name.
static inline STATUS_MUST_USE Status parse_call_target(Parser *p, Str name, Pos pos, CallExpr *call, int32_t *params, uint16_t *optional) {
    if (str_eq(name, S("use"))) return parse_fail(p, pos, CLASS_REFUSED, S("`use` is a statement of its own at the top level, `use(\"mount:path.shale\")`, never a value or a call inside a function"));
    if (parse_lookup(p, name) >= 0 || (p->in_function && parse_constant(p, name) >= 0))
        return parse_fail_cat(p, pos, CLASS_REFUSED, 3, S("`"), name, S("` is a value here, not a function: a local, a parameter or a `const` of that name hides the function or builtin; name it otherwise"));
    uint32_t i = 0;
    bool found = 0;
    TRY(parse_function_named(p, name, &i, &found));
    Builtin b = {0};
    if (found) {
        call->target = TARGET_FUNCTION;
        call->function = i;
        *params = at(p->functions, i).params;
        *optional = at(p->functions, i).optional;
    } else if (ast_builtin_named(name, &b)) {
        call->target = TARGET_BUILTIN;
        call->builtin = b;
    } else if (parse_lua_name(name).len > 0) {
        return parse_fail(p, pos, CLASS_REFUSED, parse_lua_name(name));
    } else {
        return parse_fail_cat(p, pos, CLASS_NAME, 3, S("no function `"), name, S("` above this line: a function is called only after it is declared"));
    }
    return parse_advance(p);
}

static inline STATUS_MUST_USE Status parse_call(Parser *p, Str name, Pos pos, Expr **out) {
    CallExpr call;
    memset(&call, 0, sizeof call);
    int32_t params = -1;
    uint16_t optional = 0;
    Str shown = name;
    if (str_eq(name, S("attempt"))) {
        TRY(parse_attempt_target(p, &call, &shown, &params, &optional));
    } else {
        TRY(parse_call_target(p, name, pos, &call, &params, &optional));
    }
    size_t from = p->exprs.len;
    if (p->tok.tag != TAG_RPAREN) {
        for (;;) {
            Expr *arg = NULL;
            TRY(parse_expr(p, &arg));
            TRY(ExprVec_push(&p->exprs, arg));
            if (p->tok.tag != TAG_COMMA) break;
            TRY(parse_advance(p));
        }
    }
    Token close = {0};
    TRY(parse_expect(p, TAG_RPAREN, S("`)` to close the call"), &close));
    size_t given = p->exprs.len - from;
    if (call.target == TARGET_BUILTIN && call.builtin == BUILTIN_ARG) {
        Expr *first = given > 0 ? at(p->exprs, from) : NULL;
        if (first != NULL && first->kind == EXPR_STRING) {
            bool seen = false;
            for (size_t k = 0; k < p->args_read.len; k++) {
                if (str_eq(at(p->args_read, k), first->as.string)) seen = true;
            }
            if (!seen) TRY(StrVec_push(&p->args_read, first->as.string));
        } else {
            p->args_by_value = true;
        }
    }
    if (params >= 0) {
        size_t n = (size_t)params;
        bool refused = false;
        if (optional > 0 && (given < n - optional || given > n)) {
            Str lo = parse_num(p, n - optional, &refused);
            Str hi = parse_num(p, n, &refused);
            Str g = parse_num(p, given, &refused);
            if (refused) return STOP;
            return parse_fail_cat(p, pos, CLASS_ARGUMENT, 8, S("`"), shown, S("` takes "), lo, S(" to "), hi, S(" arguments, given "), g);
        }
        if (optional == 0 && given != n) {
            Str want = parse_num(p, n, &refused);
            Str g = parse_num(p, given, &refused);
            if (refused) return STOP;
            return parse_fail_cat(p, pos, CLASS_ARGUMENT, 7, S("`"), shown, S("` takes "), want, n == 1 ? S(" argument") : S(" arguments"), S(", given "), g);
        }
        // Optional parameters left out are passed nil.
        for (size_t k = given; k < n; k++) {
            Expr *nil = parse_new_expr(p, pos, EXPR_NIL);
            if (nil == NULL) return STOP;
            TRY(ExprVec_push(&p->exprs, nil));
        }
    }
    TRY(parse_take_exprs(p, from, &call.args));
    Expr *e = parse_new_expr(p, pos, EXPR_CALL);
    if (e == NULL) return STOP;
    e->as.call = call;
    *out = e;
    return OK;
}

// One entry of a table literal: a key's (`name =` or `["key"] =`) and its value.
static inline STATUS_MUST_USE Status parse_table_entry(Parser *p, size_t items_from, size_t entries_from) {
    Pos here = parse_here(p);
    bool keyed = false;
    Str key = S("");
    Token t = {0};
    if (p->tok.tag == TAG_NAME) {
        Token next = {0};
        TRY(parse_peek(p, &next));
        if (next.tag == TAG_ASSIGN) {
            keyed = true;
            key = p->tok.text;
            TRY(parse_advance(p));
            TRY(parse_advance(p));
        }
    } else if (p->tok.tag == TAG_LBRACKET) {
        TRY(parse_advance(p));
        if (p->tok.tag != TAG_STRING) return parse_fail(p, parse_here(p), CLASS_REFUSED, S("a map literal's keys are names or string literals: `{a = 1}` or `{[\"a b\"] = 1}`"));
        keyed = true;
        key = p->tok.text;
        TRY(parse_advance(p));
        TRY(parse_expect(p, TAG_RBRACKET, S("`]`"), &t));
        TRY(parse_expect(p, TAG_ASSIGN, S("`=` after the key"), &t));
    }
    Expr *value = NULL;
    TRY(parse_expr(p, &value));
    size_t items = p->exprs.len - items_from;
    size_t entries = p->entries.len - entries_from;
    if (keyed) {
        if (items > 0) return parse_fail(p, here, CLASS_TYPE, S("a table literal mixes positional and keyed entries: a list or a map, not both"));
        for (size_t k = entries_from; k < p->entries.len; k++) {
            if (str_eq(at(p->entries, k).key, key)) return parse_fail_cat(p, here, CLASS_INDEX, 3, S("the key `"), key, S("` is given twice"));
        }
        if (value->kind == EXPR_NIL) return parse_fail(p, here, CLASS_TYPE, S("a map cannot hold nil"));
        MapEntry en = {key, value, here};
        return EntryVec_push(&p->entries, en);
    }
    if (entries > 0) return parse_fail(p, here, CLASS_TYPE, S("a table literal mixes positional and keyed entries: a list or a map, not both"));
    if (value->kind == EXPR_NIL) return parse_fail(p, here, CLASS_TYPE, S("a list cannot hold nil"));
    return ExprVec_push(&p->exprs, value);
}

static inline STATUS_MUST_USE Status parse_table(Parser *p, Pos pos, Expr **out) {
    TRY(parse_enter(p));
    TRY(parse_advance(p));
    size_t items_from = p->exprs.len;
    size_t entries_from = p->entries.len;
    while (p->tok.tag != TAG_RBRACE) {
        TRY(parse_table_entry(p, items_from, entries_from));
        if (p->tok.tag != TAG_COMMA && p->tok.tag != TAG_SEMICOLON) break;
        TRY(parse_advance(p));
    }
    Token t = {0};
    TRY(parse_expect(p, TAG_RBRACE, S("`}` to close the table"), &t));
    Expr *e = NULL;
    if (p->entries.len > entries_from) {
        e = parse_new_expr(p, pos, EXPR_MAP);
        if (e == NULL) return STOP;
        void *items = NULL;
        size_t n = p->entries.len - entries_from;
        TRY(parse_dup(p, p->entries.ptr, entries_from, n, sizeof(MapEntry), _Alignof(MapEntry), &items));
        MapEntries m = {items, n};
        e->as.map = m;
        p->entries.len = entries_from;
    } else if (p->exprs.len > items_from) {
        e = parse_new_expr(p, pos, EXPR_LIST);
        if (e == NULL) return STOP;
        TRY(parse_take_exprs(p, items_from, &e->as.list));
    } else {
        e = parse_new_expr(p, pos, EXPR_EMPTY_TABLE);
        if (e == NULL) return STOP;
    }
    p->exprs.len = items_from;
    *out = e;
    parse_leave(p);
    return OK;
}

// ── checking a prelude or a library ──

// A parser of `source`, its working lists in `a`.
static inline void parse_init(Parser *p, Arena *a, Str source, uint32_t max_nesting) {
    memset(p, 0, sizeof *p);
    p->arena = a;
    lexer_init(&p->lexer, a, source);
    p->max_nesting = max_nesting;
    p->frame.locals = LocalVec_make(a);
    p->stmts = StmtVec_make(a);
    p->exprs = ExprVec_make(a);
    p->arms = ArmVec_make(a);
    p->entries = EntryVec_make(a);
    p->args_read = StrVec_make(a);
    p->functions = FunctionVec_make(a);
    p->function_names = FunctionNames_make(a);
    p->consts = StrVec_make(a);
    p->const_owner = OwnerVec_make(a);
    p->in_prelude = PARSE_SCRIPT;
}

// A parser of prelude or library `k`'s source alone, to check it and be thrown
// away: its own loaded slots, so the check leaves the real parse's alone.
static inline STATUS_MUST_USE Status parse_throwaway(Parser *p, size_t k, Parser *q) {
    if (p->extra.len > PARSE_EXTRAS_MOST) return parse_fail(p, parse_pos(1, 1), CLASS_ARGUMENT, S("more than 16 preludes"));
    parse_init(q, p->arena, at(p->extra, k - 1).prelude.source, p->max_nesting);
    q->in_prelude = k;
    q->core = p->core;
    q->core_index = p->core_index;
    TRY(parse_slots(p->arena, p->core_index.len, &q->core_loaded));
    Extra *own = ARENA_ARRAY(p->arena, Extra, p->extra.len);
    if (own == NULL) return STOP;
    Extras extras = {own, p->extra.len};
    for (size_t e = 0; e < p->extra.len; e++) {
        at(extras, e) = at(p->extra, e);
        TRY(parse_slots(p->arena, at(p->extra, e).loaded.len, &at(extras, e).loaded));
    }
    q->extra = extras;
    return OK;
}

// A prelude's source from its start: function after function to its end.
static inline STATUS_MUST_USE Status parse_only_functions(Parser *q, Str name) {
    TRY(parse_advance(q));
    while (q->tok.tag != TAG_EOF) {
        if (q->tok.tag != TAG_KW_FUNCTION) return parse_fail_cat(q, parse_here(q), CLASS_SYNTAX, 3, name, S(" holds only functions, found "), parse_describe(q, q->tok));
        TRY(parse_function(q, parse_here(q)));
    }
    return OK;
}

// Checks prelude `k` before the script: it takes no name an earlier prelude
// has, none twice, and holds only functions that parse, in a parse of its own
// thrown away after. A failure is a stop naming the prelude, at its line.
// Zig skips a list of fixed preludes it has already found sound in this
// process; such a list passes every time, so checking it again says the same.
static inline STATUS_MUST_USE Status parse_check_prelude(Parser *p, size_t k) {
    const Extra *x = &at(p->extra, k - 1);
    for (size_t i = 0; i < x->index.len; i++) {
        PreludeFunction f = at(x->index, i);
        Pos pos = parse_pos(f.line, 1);
        if (parse_index_at(p->core_index, f.name) >= 0)
            return parse_fail_cat(p, pos, CLASS_NAME, 5, x->prelude.name, S("'s `"), f.name, S("` is the prelude's already: a later prelude may not redefine an earlier one's function"), S(""));
        for (size_t e = 0; e + 1 < k; e++) {
            if (parse_index_at(at(p->extra, e).index, f.name) >= 0)
                return parse_fail_cat(p, pos, CLASS_NAME, 6, x->prelude.name, S("'s `"), f.name, S("` is "), at(p->extra, e).prelude.name, S("'s already: a later prelude may not redefine an earlier one's function"));
        }
        if (parse_index_before(x->index, i, f.name) >= 0)
            return parse_fail_cat(p, pos, CLASS_NAME, 4, x->prelude.name, S(" declares `"), f.name, S("` twice"));
    }
    Parser q;
    TRY(parse_throwaway(p, k, &q));
    if (parse_only_functions(&q, x->prelude.name) == OK) return OK;
    if (!q.stopped) return STOP;
    Stop s = q.stop;
    Str message = s.message;
    Str name = x->prelude.name;
    bool names_it = false;
    if (name.len == 0) names_it = true;
    for (size_t i = 0; name.len > 0 && i + name.len <= message.len; i++) {
        if (str_eq(str_sub(message, i, i + name.len), name)) names_it = true;
    }
    if (names_it) {
        Str copy = {0};
        if (!arena_dup(p->arena, message, &copy)) return STOP;
        return parse_fail(p, parse_pos(s.line, s.column), s.cls, copy);
    }
    return parse_fail_cat(p, parse_pos(s.line, s.column), s.cls, 3, name, S(": "), message);
}

// Whether `e` is a literal: a string, a number, a boolean, or a list or map of them.
static inline bool parse_literal(const Expr *e) {
    switch (e->kind) {
    case EXPR_BOOLEAN:
    case EXPR_INT:
    case EXPR_STRING:
    case EXPR_EMPTY_TABLE:
        return true;
    case EXPR_LIST:
        for (size_t i = 0; i < e->as.list.len; i++) {
            if (!parse_literal(at(e->as.list, i))) return false;
        }
        return true;
    case EXPR_MAP:
        for (size_t i = 0; i < e->as.map.len; i++) {
            if (!parse_literal(at(e->as.map, i).value)) return false;
        }
        return true;
    default:
        return false;
    }
}

// A library's source from its start: functions and `const`s to its end.
static inline STATUS_MUST_USE Status parse_library_source(Parser *q, size_t k) {
    const Extra *x = &at(q->extra, k - 1);
    size_t functions = 0;
    size_t consts = 0;
    TRY(parse_advance(q));
    while (q->tok.tag != TAG_EOF) {
        Pos pos = parse_here(q);
        if (q->tok.tag == TAG_NAME && str_eq(q->tok.text, S("use"))) return parse_fail(q, pos, CLASS_REFUSED, S("a library uses no other library: its user `use`s each one it needs"));
        if (q->tok.tag != TAG_KW_FUNCTION && q->tok.tag != TAG_KW_CONST) return parse_fail_cat(q, pos, CLASS_SYNTAX, 2, S("a library holds only functions and `const`s, found "), parse_describe(q, q->tok));
        if (pos.column != 1) return parse_fail(q, pos, CLASS_SYNTAX, S("a library's functions and `const`s each begin at the start of a line"));
        if (q->tok.tag == TAG_KW_FUNCTION) {
            TRY(parse_function(q, pos));
            functions++;
            continue;
        }
        TRY(parse_advance(q));
        Token name = {0};
        TRY(parse_expect(q, TAG_NAME, S("a name after `const`"), &name));
        if (q->tok.tag != TAG_ASSIGN) return parse_fail(q, parse_here(q), CLASS_SYNTAX, S("a `const` needs its value: `const NAME = ...`"));
        TRY(parse_advance(q));
        Expr *value = NULL;
        TRY(parse_expr(q, &value));
        if (!parse_literal(value)) return parse_fail_cat(q, pos, CLASS_REFUSED, 3, S("a library's `const "), name.text, S("` is a literal (a string, a number, a boolean, or a list or map of them): a library runs nothing and reads no launch value where it is used"));
        TRY(StrVec_push(&q->consts, name.text));
        TRY(OwnerVec_push(&q->const_owner, k));
        consts++;
    }
    // The index found each by the start of its line; a long string's line
    // that begins `function ` or `const ` would be one it finds and the parse
    // does not.
    if (functions != x->index.len || consts != x->library.consts.len)
        return parse_fail(q, parse_pos(1, 1), CLASS_SYNTAX, S("a line inside a long string begins `function ` or `const `, which marks a library's own: begin it otherwise"));
    return OK;
}

// Checks library `k` where it is used, in a parse of its own thrown away
// after. A failure is a stop at the `use`, naming the library's line.
static inline STATUS_MUST_USE Status parse_check_library(Parser *p, size_t k, Pos pos) {
    Parser q;
    TRY(parse_throwaway(p, k, &q));
    if (parse_library_source(&q, k) == OK) return OK;
    if (!q.stopped) return STOP;
    Str name = at(p->extra, k - 1).prelude.name;
    bool refused = false;
    Str line = parse_num(p, q.stop.line, &refused);
    Str column = parse_num(p, q.stop.column, &refused);
    if (refused) return STOP;
    return parse_fail_cat(p, pos, q.stop.cls, 10, S("`use(\""), name, S("\")`: "), name, S(":"), line, S(":"), column, S(": "), q.stop.message);
}

// What a name of a library's already is here, or "" when it is nothing yet:
// a builtin, the language's own, a prelude's function, another library's, the
// script's function or local, or the library's own twice.
static inline STATUS_MUST_USE Status parse_library_name_was(Parser *p, Str path, PreludeFunctions functions, PreludeFunctions consts, bool is_const, size_t i, Str *was) {
    Str name = is_const ? at(consts, i).name : at(functions, i).name;
    Builtin b = {0};
    *was = S("");
    if (ast_builtin_named(name, &b)) {
        *was = S("a builtin");
        return OK;
    }
    if (str_eq(name, S("attempt")) || str_eq(name, S("use")) || parse_lua_name(name).len > 0) {
        *was = S("the language's own");
        return OK;
    }
    if (parse_index_at(p->core_index, name) >= 0) {
        *was = S("the prelude's");
        return OK;
    }
    Msg m;
    msg_init(&m, p->arena);
    for (size_t k = 0; k < p->extra.len; k++) {
        const Extra *e = &at(p->extra, k);
        if (parse_index_at(e->index, name) < 0 && !(e->is_library && parse_index_at(e->library.consts, name) >= 0)) continue;
        msg_str(&m, e->prelude.name);
        msg_str(&m, S("'s"));
        if (e->is_library) {
            msg_str(&m, S(", by the `use` at line "));
            msg_u64(&m, e->library.use_line);
        }
        if (m.s != OK) return STOP;
        *was = buf_view(&m.b);
        return OK;
    }
    uint32_t fi = 0;
    if (FunctionNames_get(&p->function_names, name, &fi)) {
        msg_str(&m, S("the script's function at line "));
        msg_u64(&m, at(p->functions, fi).pos.line);
        if (m.s != OK) return STOP;
        *was = buf_view(&m.b);
        return OK;
    }
    if (parse_lookup(p, name) >= 0) {
        *was = S("a local or `const` of the script above this line");
        return OK;
    }
    bool twice = parse_index_before(functions, is_const ? functions.len : i, name) >= 0 || (is_const && parse_index_before(consts, i, name) >= 0);
    if (twice) {
        msg_str(&m, path);
        msg_str(&m, S("'s own, twice"));
        if (m.s != OK) return STOP;
        *was = buf_view(&m.b);
    }
    return OK;
}

// Refuses a library's name that is already something here, naming both places.
static inline STATUS_MUST_USE Status parse_library_names(Parser *p, Pos pos, Str path, PreludeFunctions functions, PreludeFunctions consts) {
    for (int kind = 0; kind < 2; kind++) {
        PreludeFunctions list = kind == 0 ? functions : consts;
        for (size_t i = 0; i < list.len; i++) {
            Str was = {0};
            TRY(parse_library_name_was(p, path, functions, consts, kind == 1, i, &was));
            if (was.len == 0) continue;
            bool refused = false;
            Str line = parse_num(p, at(list, i).line, &refused);
            if (refused) return STOP;
            return parse_fail_cat(p, pos, CLASS_NAME, 13, S("`use(\""), path, S("\")`: its "), kind == 0 ? S("function") : S("const"), S(" `"), at(list, i).name, S("` ("), path, S(":"), line, S(") is "), was, S(" already"));
        }
    }
    return OK;
}

// Whether a `use`'s path names its library through a mount, `name:path`.
static inline bool parse_use_path_ok(Str path) {
    // An empty path may hold NULL, and memchr of NULL is undefined even for
    // a length of 0; it names no mount either way.
    if (path.len == 0) return false;
    const char *colon_at = memchr(path.ptr, ':', path.len);
    size_t colon = colon_at != NULL ? (size_t)(colon_at - path.ptr) : 0;
    if (colon == 0) return false;
    for (size_t i = 0; i < colon; i++) {
        uint8_t c = str_byte(path, i);
        if (!(lex_is_alpha(c) || lex_is_digit(c) || c == '_' || c == '-')) return false;
    }
    return colon + 1 < path.len && str_byte(path, colon + 1) != '/';
}

// Binds each of library `k`'s `const`s on the `use`'s line: its literal parsed
// from the library, its name the script's from here on.
static inline STATUS_MUST_USE Status parse_library_consts(Parser *p, size_t k, Pos pos) {
    const Extra *x = &at(p->extra, k - 1);
    for (size_t i = 0; i < x->library.consts.len; i++) {
        PreludeFunction c = at(x->library.consts, i);
        Lexer saved_lexer = p->lexer;
        Token saved_tok = p->tok;
        bool saved_has_ahead = p->has_ahead;
        Token saved_ahead = p->ahead;
        lexer_init(&p->lexer, p->arena, x->prelude.source);
        p->lexer.at = c.at;
        p->lexer.line = c.line;
        p->has_ahead = false;
        p->in_prelude = k;
        // `const`, its name, `=`, then the value's first token.
        for (int t = 0; t < 4; t++) TRY(parse_advance(p));
        Stmt s;
        parse_stmt_init(&s, pos, STMT_CONSTANT);
        TRY(parse_expr(p, &s.as.constant.value));
        p->in_prelude = PARSE_SCRIPT;
        p->lexer = saved_lexer;
        p->tok = saved_tok;
        p->has_ahead = saved_has_ahead;
        p->ahead = saved_ahead;
        TRY(parse_declare(p, c.name, pos, &s.as.constant.slot));
        at(p->frame.locals, p->frame.locals.len - 1).constant = true;
        if (p->consts.len == UINT16_MAX) return parse_fail(p, pos, CLASS_SYNTAX, S("too many constants"));
        s.as.constant.index = (uint16_t)p->consts.len;
        TRY(StrVec_push(&p->consts, c.name));
        TRY(OwnerVec_push(&p->const_owner, k));
        TRY(StmtVec_push(&p->stmts, s));
    }
    return OK;
}

// `use("mount:path.shale")`: reads the library through the host's Loader,
// checks it, and adds it after the preludes, so its functions load at their
// first call as a prelude's do. Its `const`s are bound here.
static inline STATUS_MUST_USE Status parse_use_statement(Parser *p, Pos pos, Stmt *out) {
    if (p->in_function || p->frame.depth > 1) return parse_fail(p, pos, CLASS_REFUSED, S("`use` is a top-level statement only: a library's names are the whole script's, from its line on"));
    TRY(parse_advance(p));
    TRY(parse_advance(p));
    if (p->tok.tag != TAG_STRING) return parse_fail(p, parse_here(p), CLASS_SYNTAX, S("`use` takes one string literal, a library's path through a mount: `use(\"cookbook:roc/pinned.shale\")`"));
    Str path = p->tok.text;
    TRY(parse_advance(p));
    Token t = {0};
    TRY(parse_expect(p, TAG_RPAREN, S("`)` after the library's path: `use` takes one"), &t));
    if (p->loader.load == NULL) return parse_fail(p, pos, CLASS_HOST, S("`use`: this host loads no library"));
    if (!parse_use_path_ok(path))
        return parse_fail_cat(p, pos, CLASS_ARGUMENT, 3, S("`use(\""), path, S("\")`: a library is named through a mount, `name:path` (`cookbook:roc/pinned.shale`), never by an absolute path or the root's: a script runs from other repositories' roots"));
    for (size_t k = 0; k < p->extra.len; k++) {
        const Extra *x = &at(p->extra, k);
        if (!x->is_library || !str_eq(x->prelude.name, path)) continue;
        bool refused = false;
        Str line = parse_num(p, x->library.use_line, &refused);
        if (refused) return STOP;
        return parse_fail_cat(p, pos, CLASS_NAME, 4, S("`use(\""), path, S("\")` again: it is used at line "), line);
    }
    if (p->extra.len == p->extra_cap) {
        bool refused = false;
        Str most = parse_num(p, PARSE_LIBRARIES_MOST, &refused);
        if (refused) return STOP;
        return parse_fail_cat(p, pos, CLASS_ARGUMENT, 4, S("`use(\""), path, S("\")`: more than "), most, S(" libraries"));
    }
    Str why = S("");
    Str source = S("");
    bool got = false;
    TRY(p->loader.load(p->loader.ctx, p->arena, path, &why, &source, &got));
    if (!got) return parse_fail_cat(p, pos, CLASS_STORE, 4, S("`use(\""), path, S("\")`: "), why);
    // Indexed as a prelude is: each `function` and `const` from the start of
    // its line.
    PreludeFunctions functions;
    PreludeFunctions consts;
    TRY(parse_index(p->arena, source, parse_next_function, &functions));
    TRY(parse_index(p->arena, source, parse_next_const, &consts));
    TRY(parse_library_names(p, pos, path, functions, consts));
    Extra x = {0};
    memset(&x, 0, sizeof x);
    x.prelude.name = path;
    x.prelude.source = source;
    x.index = functions;
    TRY(parse_slots(p->arena, functions.len, &x.loaded));
    x.is_library = true;
    x.library.use_line = pos.line;
    x.library.consts = consts;
    size_t k = p->extra.len + 1;
    p->extra.len = k;
    at(p->extra, k - 1) = x;
    TRY(parse_check_library(p, k, pos));
    size_t from = p->stmts.len;
    TRY(parse_library_consts(p, k, pos));
    Stmt s;
    parse_stmt_init(&s, pos, STMT_DO);
    TRY(parse_take_stmts(p, from, &s.as.do_));
    *out = s;
    return OK;
}

// ── the whole source ──

// The whole source, parsed, after the core prelude (`core`) and the
// preludes after it, each checked first. On STOP, `p->stopped` says the parse
// stopped and `p->stop` where and why; otherwise the arena refused.
static inline STATUS_MUST_USE Status parse_program(Parser *p, Str core, Preludes preludes, Loader loader, Program *out) {
    p->core = core;
    p->loader = loader;
    TRY(parse_index(p->arena, core, parse_next_function, &p->core_index));
    TRY(parse_slots(p->arena, p->core_index.len, &p->core_loaded));
    p->extra_cap = preludes.len + PARSE_LIBRARIES_MOST;
    Extra *extras = ARENA_ARRAY(p->arena, Extra, p->extra_cap);
    if (extras == NULL) return STOP;
    p->extra.ptr = extras;
    p->extra.len = 0;
    for (size_t i = 0; i < preludes.len; i++) {
        Extra x = {0};
        memset(&x, 0, sizeof x);
        x.prelude = at(preludes, i);
        TRY(parse_index(p->arena, x.prelude.source, parse_next_function, &x.index));
        TRY(parse_slots(p->arena, x.index.len, &x.loaded));
        p->extra.len++;
        Extras room = {extras, p->extra_cap};
        at(room, p->extra.len - 1) = x;
        TRY(parse_check_prelude(p, p->extra.len));
    }
    TRY(parse_advance(p));
    Stmts body = {0};
    TRY(parse_block(p, &body));
    if (p->tok.tag != TAG_EOF) return parse_fail_cat(p, parse_here(p), CLASS_SYNTAX, 2, parse_describe(p, p->tok), S(" with nothing open to close"));
    Program prog = {0};
    memset(&prog, 0, sizeof prog);
    void *items = NULL;
    TRY(parse_dup(p, p->functions.ptr, 0, p->functions.len, sizeof(Function), _Alignof(Function), &items));
    Functions fs = {items, p->functions.len};
    prog.functions = fs;
    TRY(parse_dup(p, p->consts.ptr, 0, p->consts.len, sizeof(Str), _Alignof(Str), &items));
    StrList cs = {items, p->consts.len};
    prog.const_names = cs;
    Function main_fn = {S("the script"), parse_pos(1, 1), 0, 0, p->frame.slots, body};
    prog.main = main_fn;
    prog.args_by_value = p->args_by_value;
    if (!p->args_by_value) {
        TRY(parse_dup(p, p->args_read.ptr, 0, p->args_read.len, sizeof(Str), _Alignof(Str), &items));
        StrList rs = {items, p->args_read.len};
        prog.args_read = rs;
    }
    *out = prog;
    return OK;
}

#endif
