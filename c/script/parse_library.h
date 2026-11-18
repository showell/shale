// parse_library.h: a later prelude checked before the script, and a library a
// `use` names, read through the host's `ParseLoader`, checked and bound. Each
// check parses in a parser of its own, thrown away after, so the real parse's
// loaded slots are left alone.

#ifndef SHALE_PARSE_LIBRARY_H
#define SHALE_PARSE_LIBRARY_H

#include "parse_expr.h"

// ── checking a prelude or a library ──

// A parser of prelude or library `k`'s source alone, to check it and be thrown
// away: its own loaded slots, so the check leaves the real parse's alone.
static inline STATUS_MUST_USE Status parse_throwaway(Parser *p, size_t k, Parser *q) {
    if (p->extra.len > PARSE_EXTRAS_MAX) return parse_fail(p, parse_pos(1, 1), CLASS_ARGUMENT, S("more than 16 preludes"));
    parse_init(q, p->arena, at(p->extra, k - 1).prelude.source, p->max_nesting);
    q->in_prelude = k;
    q->core = p->core;
    q->core_index = p->core_index;
    TRY(parse_slots(p->arena, p->core_index.len, &q->core_loaded));
    ParseExtra *own = ARENA_ARRAY(p->arena, ParseExtra, p->extra.len);
    if (own == NULL) return STOP;
    ParseExtras extras = {own, p->extra.len};
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
// thrown away after. A failure is a stop naming the prelude, at its line. A
// fixed prelude is checked on every parse: one found sound once passes every
// time, so checking it again says the same and costs only time.
static inline STATUS_MUST_USE Status parse_check_prelude(Parser *p, size_t k) {
    const ParseExtra *x = &at(p->extra, k - 1);
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

// Whether `e` is a literal: a string, a number, a boolean, or a list or map of
// them.
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
    const ParseExtra *x = &at(q->extra, k - 1);
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
        TRY(ParseStrVec_push(&q->consts, name.text));
        TRY(ParseOwnerVec_push(&q->const_owner, k));
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
// after. A failure is a stop at the `use` (by its `path` as written), naming
// the library's line.
static inline STATUS_MUST_USE Status parse_check_library(Parser *p, size_t k, Pos pos, Str path) {
    Parser q;
    TRY(parse_throwaway(p, k, &q));
    if (parse_library_source(&q, k) == OK) return OK;
    if (!q.stopped) return STOP;
    Str name = at(p->extra, k - 1).prelude.name;
    bool refused = false;
    Str line = parse_num(p, q.stop.line, &refused);
    Str column = parse_num(p, q.stop.column, &refused);
    if (refused) return STOP;
    return parse_fail_cat(p, pos, q.stop.cls, 10, S("`use(\""), path, S("\")`: "), name, S(":"), line, S(":"), column, S(": "), q.stop.message);
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
    message_init(&m, p->arena);
    for (size_t k = 0; k < p->extra.len; k++) {
        const ParseExtra *e = &at(p->extra, k);
        if (parse_index_at(e->index, name) < 0 && !(e->is_library && parse_index_at(e->library.consts, name) >= 0)) continue;
        message_str(&m, e->prelude.name);
        message_str(&m, S("'s"));
        if (e->is_library) {
            message_str(&m, S(", by the `use` at line "));
            message_u64(&m, e->library.use_line);
        }
        if (m.s != OK) return STOP;
        *was = buf_view(&m.b);
        return OK;
    }
    uint32_t fi = 0;
    if (ParseFunctionNames_get(&p->function_names, name, &fi)) {
        message_str(&m, S("the script's function at line "));
        message_u64(&m, at(p->functions, fi).pos.line);
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
        message_str(&m, path);
        message_str(&m, S("'s own, twice"));
        if (m.s != OK) return STOP;
        *was = buf_view(&m.b);
    }
    return OK;
}

// Refuses a library's name that is already something here, naming both places:
// the `use` by its `path` as written, the library's line by its `name`.
static inline STATUS_MUST_USE Status parse_library_names(Parser *p, Pos pos, Str path, Str name, PreludeFunctions functions, PreludeFunctions consts) {
    for (int kind = 0; kind < 2; kind++) {
        PreludeFunctions list = kind == 0 ? functions : consts;
        for (size_t i = 0; i < list.len; i++) {
            Str was = {0};
            TRY(parse_library_name_was(p, name, functions, consts, kind == 1, i, &was));
            if (was.len == 0) continue;
            bool refused = false;
            Str line = parse_num(p, at(list, i).line, &refused);
            if (refused) return STOP;
            return parse_fail_cat(p, pos, CLASS_NAME, 13, S("`use(\""), path, S("\")`: its "), kind == 0 ? S("function") : S("const"), S(" `"), at(list, i).name, S("` ("), name, S(":"), line, S(") is "), was, S(" already"));
        }
    }
    return OK;
}

// Whether a `use`'s path names its library through a mount, `name:path`.
static inline bool parse_use_path_ok(Str path) {
    // An empty path may hold NULL, and memchr of NULL is undefined even for
    // a length of 0; it names no mount either way.
    if (path.len == 0) return false;
    const char *colon_at = mem_find_byte(path.ptr, ':', path.len);
    size_t colon = colon_at != NULL ? (size_t)(colon_at - path.ptr) : 0;
    if (colon == 0) return false;
    for (size_t i = 0; i < colon; i++) {
        uint8_t c = str_byte(path, i);
        if (!(lex_is_alpha(c) || lex_is_digit(c) || c == '_' || c == '-')) return false;
    }
    return colon + 1 < path.len && str_byte(path, colon + 1) != '/';
}

// Whether a `use`'s path is one beside the script's own file, `./` and then
// parts that are each a name: none empty, none `.` or `..`, no `:`. The host
// resolves it against the script's folder, so a recipe and its library load
// together wherever the recipe's file is.
static inline bool parse_use_beside_ok(Str path) {
    if (path.len <= 2 || !str_starts_with(path, S("./"))) return false;
    Str rest = str_sub(path, 2, path.len);
    size_t start = 0;
    for (size_t i = 0; i <= rest.len; i++) {
        if (i < rest.len && str_byte(rest, i) == ':') return false;
        if (i < rest.len && str_byte(rest, i) != '/') continue;
        Str part = str_sub(rest, start, i);
        if (part.len == 0 || str_eq(part, S(".")) || str_eq(part, S(".."))) return false;
        start = i + 1;
    }
    return true;
}

// Binds each of library `k`'s `const`s on the `use`'s line: its literal parsed
// from the library, its name the script's from here on.
static inline STATUS_MUST_USE Status parse_library_consts(Parser *p, size_t k, Pos pos) {
    const ParseExtra *x = &at(p->extra, k - 1);
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
        Status read = OK;
        for (int t = 0; t < 4 && read == OK; t++) read = parse_advance(p);
        Stmt s;
        parse_stmt_init(&s, pos, STMT_CONSTANT);
        if (read == OK) read = parse_expr(p, &s.as.constant.value);
        // The script's place, put back on every way out.
        p->in_prelude = PARSE_SCRIPT;
        p->lexer = saved_lexer;
        p->tok = saved_tok;
        p->has_ahead = saved_has_ahead;
        p->ahead = saved_ahead;
        TRY(read);
        TRY(parse_declare(p, c.name, pos, &s.as.constant.slot));
        at(p->frame.locals, p->frame.locals.len - 1).constant = true;
        if (p->consts.len == UINT16_MAX) return parse_fail(p, pos, CLASS_SYNTAX, S("too many constants"));
        s.as.constant.index = (uint16_t)p->consts.len;
        TRY(ParseStrVec_push(&p->consts, c.name));
        TRY(ParseOwnerVec_push(&p->const_owner, k));
        TRY(ParseStmtVec_push(&p->stmts, s));
    }
    return OK;
}

// `use("mount:path.shale")` or `use("./path.shale")`, beside the script's
// file: reads the library through the host's `ParseLoader`, checks it, and
// adds it after the preludes, so its functions load at their first call as a
// prelude's do. Its `const`s are bound here.
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
    bool beside = str_starts_with(path, S("./")) || str_starts_with(path, S("../"));
    if (beside && !parse_use_beside_ok(path))
        return parse_fail_cat(p, pos, CLASS_ARGUMENT, 3, S("`use(\""), path, S("\")`: a path beside the script's file is `./` and then names (`./items.shale`, `./sub/x.shale`), never `..`: a library outside the script's folder is named through a mount (`cookbook:c/items.shale`)"));
    if (!beside && !parse_use_path_ok(path))
        return parse_fail_cat(p, pos, CLASS_ARGUMENT, 3, S("`use(\""), path, S("\")`: a library is named through a mount, `name:path` (`cookbook:roc/pinned.shale`), or beside the script's own file, `./name.shale`; never by an absolute path or the root's: a script runs from other repositories' roots"));
    for (size_t k = 0; k < p->extra.len; k++) {
        const ParseExtra *x = &at(p->extra, k);
        if (!x->is_library || !str_eq(x->prelude.name, path)) continue;
        bool refused = false;
        Str line = parse_num(p, x->library.use_line, &refused);
        if (refused) return STOP;
        return parse_fail_cat(p, pos, CLASS_NAME, 4, S("`use(\""), path, S("\")` again: it is used at line "), line);
    }
    if (p->extra.len == p->extra_cap) {
        bool refused = false;
        Str most = parse_num(p, PARSE_LIBRARIES_MAX, &refused);
        if (refused) return STOP;
        return parse_fail_cat(p, pos, CLASS_ARGUMENT, 4, S("`use(\""), path, S("\")`: more than "), most, S(" libraries"));
    }
    Str why = S("");
    Str source = S("");
    Str name = S("");
    bool got = false;
    TRY(p->loader.load(p->loader.ctx, p->arena, path, &why, &source, &name, &got));
    if (!got) return parse_fail_cat(p, pos, CLASS_FILE_SYSTEM, 4, S("`use(\""), path, S("\")`: "), why);
    if (name.len == 0) name = path;
    // A `./` path and a mount's may name one file: the file read is the one
    // used again.
    for (size_t k = 0; k < p->extra.len; k++) {
        const ParseExtra *x = &at(p->extra, k);
        if (!x->is_library || !str_eq(x->prelude.name, name)) continue;
        bool refused = false;
        Str line = parse_num(p, x->library.use_line, &refused);
        if (refused) return STOP;
        return parse_fail_cat(p, pos, CLASS_NAME, 4, S("`use(\""), path, S("\")` again: it is used at line "), line);
    }
    // Indexed as a prelude is: each `function` and `const` from the start of
    // its line.
    PreludeFunctions functions;
    PreludeFunctions consts;
    TRY(parse_index(p->arena, source, parse_next_function, &functions));
    TRY(parse_index(p->arena, source, parse_next_const, &consts));
    TRY(parse_library_names(p, pos, path, name, functions, consts));
    ParseExtra x = {0};
    memset(&x, 0, sizeof x);
    x.prelude.name = name;
    x.prelude.source = source;
    x.index = functions;
    TRY(parse_slots(p->arena, functions.len, &x.loaded));
    x.is_library = true;
    x.library.use_line = pos.line;
    x.library.consts = consts;
    size_t k = p->extra.len + 1;
    p->extra.len = k;
    at(p->extra, k - 1) = x;
    TRY(parse_check_library(p, k, pos, path));
    size_t from = p->stmts.len;
    Stmt s;
    parse_stmt_init(&s, pos, STMT_DO);
    Status bound = parse_library_consts(p, k, pos);
    if (bound == OK) bound = parse_take_stmts(p, from, &s.as.do_);
    // The statements' stack, put back on every way out.
    p->stmts.len = from;
    TRY(bound);
    *out = s;
    return OK;
}

#endif
