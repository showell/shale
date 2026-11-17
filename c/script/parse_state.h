// parse_state.h: the parser's state and the pieces every production uses: the
// preludes and libraries a parse holds, the frame of names, the working lists;
// a stop and its message, the next token, the nesting budget, and a name's
// lookup and declaration.
//
// Everything the parse makes, the tree and its own working lists alike, comes
// from one arena: the machine's. A STOP with `stopped` false is the arena's
// refusal, which the caller reads as running out of machine memory.

#ifndef SHALE_PARSE_STATE_H
#define SHALE_PARSE_STATE_H

#include "../foundation/map.h"
#include "ast.h"
#include "lex.h"
#include "prelude_index.h"

// What a library has beyond a prelude: the line of its `use`, and its
// `const`s, each from the start of its line.
typedef struct {
    uint32_t use_line;
    PreludeFunctions consts;
} ParseLibrary;

SLICE_TYPE(ParseLoadedSlots, uint32_t);

// A later prelude as a parse holds it: its functions, and the index in
// `functions` of each one loaded (PARSE_UNLOADED until its first call). A
// `use`d library is one too, with `is_library` set.
typedef struct {
    Prelude prelude;
    PreludeFunctions index;
    ParseLoadedSlots loaded;
    bool is_library;
    ParseLibrary library;
} ParseExtra;

SLICE_TYPE(ParseExtras, ParseExtra);

// The host's reader of a `use`d library: on OK, `*got` says whether `*out`
// holds the source; when it doesn't, `*why` says why. STOP only when the arena
// refuses.
typedef struct {
    void *ctx;
    STATUS_MUST_USE Status (*load)(void *ctx, Arena *into, Str path, Str *why, Str *out, bool *got);
} ParseLoader;

typedef struct {
    Str name;
    uint16_t slot;
    uint16_t depth;
    bool constant;
} ParseLocal;

VEC_DEFINE(ParseLocalVec, ParseLocal)
VEC_DEFINE(ParseStmtVec, Stmt)
VEC_DEFINE(ParseExprVec, Expr *)
VEC_DEFINE(ParseArmVec, Arm)
VEC_DEFINE(ParseEntryVec, MapEntry)
VEC_DEFINE(ParseFunctionVec, Function)
VEC_DEFINE(ParseStrVec, Str)
VEC_DEFINE(ParseOwnerVec, size_t)
MAP_DEFINE(ParseFunctionNames, uint32_t)

// One function's names: a stack of locals, each in the block it was declared
// in.
typedef struct {
    ParseLocalVec locals;
    uint16_t depth;
    uint16_t slots;
    uint16_t loops;
} ParseFrame;

enum {
    // The most libraries one script may `use`.
    PARSE_LIBRARIES_MOST = 16,
    // The most preludes and libraries one parse holds.
    PARSE_EXTRAS_MOST = 16 + PARSE_LIBRARIES_MOST,
};

// A loaded slot's value until its function is parsed. A macro of the slot's
// own type, not an enum: an enum's constants are `int`, and UINT32_MAX is
// past its range.
#define PARSE_UNLOADED ((uint32_t)UINT32_MAX)

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
    ParseFrame frame;
    // Whether the frame is a function's, not the script's own top level.
    bool in_function;
    // The script's own frame while one of its functions is parsed, so a stop
    // can tell a script `local` the function can't see from a name that is
    // nowhere, and name the fix.
    const ParseFrame *script_frame;
    // The lists under construction, one stack per kind, shared by every
    // level: a list is the stack above its mark.
    ParseStmtVec stmts;
    ParseExprVec exprs;
    ParseArmVec arms;
    ParseEntryVec entries;
    ParseStrVec args_read;
    bool args_by_value;
    ParseFunctionVec functions;
    // The script's own functions, by name.
    ParseFunctionNames function_names;
    // The core prelude's source and its functions, and each one's index in
    // `functions` once loaded.
    Str core;
    PreludeFunctions core_index;
    ParseLoadedSlots core_loaded;
    // The preludes after the core's, then the libraries `use`d, in order.
    ParseExtras extra;
    size_t extra_cap;
    // Which prelude is being parsed (0 for the core's, then `extra`'s), or
    // PARSE_SCRIPT. A prelude calls only its own functions, earlier preludes'
    // and the builtins, never the script's or a later prelude's.
    size_t in_prelude;
    // The top-level `const`s declared so far, and who declared each:
    // PARSE_SCRIPT, or a library's place among the preludes.
    ParseStrVec consts;
    ParseOwnerVec const_owner;
    // Reads the library a `use` names; `load` is NULL where none is loaded.
    ParseLoader loader;
} Parser;

// Lua's library tables and functions a writer reaches for, each with what to
// write instead.
typedef struct {
    Str name;
    Str why;
} ParseLuaName;

SLICE_TYPE(ParseLuaNames, const ParseLuaName);

static inline Str parse_lua_name(Str name) {
    static const ParseLuaName table[] = {
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
    ParseLuaNames t = {table, sizeof table / sizeof(ParseLuaName)};
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

// A syntax error right after a long string is most likely a closer inside its
// text that ended it early, so the stop names that closer and the level that
// would hold it.
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

// A stop whose message is made, the arena's refusal a STOP with `stopped` false
// rather than a fallback message.
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

// `n` in decimal, in the arena; "" when refused, and the message then carries
// nothing in its place.
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
// number shown, any other token quoted. Each made one falls back to its plain
// words when the arena refuses.
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

// Charges one level of the nesting budget. A refusal counts no level, so a
// production that fails to enter has nothing to leave: every other way out of
// one calls `parse_leave`.
// DRAGON: the C stack. A source nested without bound would recurse the parser
// off its stack and crash the process.
// REMEDY: every recursive production (block, statement, binary, unary's
// operator, suffixed, table) enters here, and the budget (200 unset; no command
// line or header sets it) bounds the depth. Loading a prelude starts its count
// again, and a prelude's own nesting is fixed and small.
static inline STATUS_MUST_USE Status parse_enter(Parser *p) {
    p->nesting++;
    if (p->nesting > p->max_nesting) {
        p->nesting--;
        Status s = parse_set_stop(p, parse_here(p), CLASS_BUDGET, S("the source nests deeper than the nesting budget"));
        p->stop.budget = BUDGET_NESTING;
        return s;
    }
    return OK;
}

static inline void parse_leave(Parser *p) {
    p->nesting = count_less_u32(p->nesting, 1);
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
        ParseLocal l = at(p->frame.locals, i - 1);
        if (str_eq(l.name, name)) return l.slot;
    }
    return -1;
}

// Declares a local in this block. A local may take a builtin's name: a value
// by that name is the local, while a call by it is still the builtin, since
// only functions are called.
static inline STATUS_MUST_USE Status parse_declare(Parser *p, Str name, Pos pos, uint16_t *slot) {
    for (size_t i = p->frame.locals.len; i > 0; i--) {
        ParseLocal l = at(p->frame.locals, i - 1);
        if (l.depth < p->frame.depth) break;
        if (str_eq(l.name, name)) return parse_fail_cat(p, pos, CLASS_NAME, 3, S("`"), name, S("` is already a local in this block"));
    }
    if (p->frame.slots == UINT16_MAX) return parse_fail(p, pos, CLASS_SYNTAX, S("too many locals in one function"));
    *slot = p->frame.slots;
    p->frame.slots++;
    ParseLocal l = {name, *slot, p->frame.depth, false};
    return ParseLocalVec_push(&p->frame.locals, l);
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
    c_bug("parse: a local's slot that is in no frame");
    // Never reached: TCC refuses a function that might return no value.
    return false;
}

static inline bool parse_frame_has(const ParseFrame *f, Str name) {
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

static inline STATUS_MUST_USE Status parse_slots(Arena *a, size_t n, ParseLoadedSlots *out) {
    uint32_t *slots = ARENA_ARRAY(a, uint32_t, n);
    if (slots == NULL) return STOP;
    ParseLoadedSlots s = {slots, n};
    for (size_t i = 0; i < n; i++) at(s, i) = PARSE_UNLOADED;
    *out = s;
    return OK;
}

// The name a prelude's functions are shown under: "the prelude" for the
// core's, else its own ("the Linux prelude").
static inline Str parse_prelude_name(const Parser *p, size_t k) {
    return k == 0 ? S("the prelude") : at(p->extra, k - 1).prelude.name;
}

// A parser of `source`, its working lists in `a`.
static inline void parse_init(Parser *p, Arena *a, Str source, uint32_t max_nesting) {
    memset(p, 0, sizeof *p);
    p->arena = a;
    lexer_init(&p->lexer, a, source);
    p->max_nesting = max_nesting;
    p->frame.locals = ParseLocalVec_make(a);
    p->stmts = ParseStmtVec_make(a);
    p->exprs = ParseExprVec_make(a);
    p->arms = ParseArmVec_make(a);
    p->entries = ParseEntryVec_make(a);
    p->args_read = ParseStrVec_make(a);
    p->functions = ParseFunctionVec_make(a);
    p->function_names = ParseFunctionNames_make(a);
    p->consts = ParseStrVec_make(a);
    p->const_owner = ParseOwnerVec_make(a);
    p->in_prelude = PARSE_SCRIPT;
}

#endif
