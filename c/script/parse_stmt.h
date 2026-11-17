// parse_stmt.h: statements and blocks, and the functions they declare, as
// Zig Shale's src/script/parse.zig's `block`, `statement`, `ifStatement`,
// `forStatement`, `function` and `loadPrelude` do: a prelude's function is
// parsed at its first call, from its own line. Each production puts the
// parser's state back on every way out, as Zig's `defer`s do. Its calls into
// expressions and `use` are declared here and defined in parse_expr.h and
// parse_library.h, which parse.h includes after it.

#ifndef SHALE_PARSE_STMT_H
#define SHALE_PARSE_STMT_H

#include "parse_state.h"

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

// An `if`'s arms, pushed on the arms' stack from `from` on.
static inline STATUS_MUST_USE Status parse_if_arms(Parser *p, Pos pos, size_t from, Stmt *out) {
    TRY(parse_advance(p));
    Token t = {0};
    Expr *cond = NULL;
    TRY(parse_expr(p, &cond));
    TRY(parse_expect(p, TAG_KW_THEN, S("`then` after the `if` condition"), &t));
    Arm first = {0};
    first.cond = cond;
    TRY(parse_block(p, &first.body));
    TRY(ParseArmVec_push(&p->arms, first));
    Stmt s;
    parse_stmt_init(&s, pos, STMT_IF);
    for (;;) {
        if (p->tok.tag == TAG_KW_ELSEIF) {
            TRY(parse_advance(p));
            Arm arm = {0};
            TRY(parse_expr(p, &arm.cond));
            TRY(parse_expect(p, TAG_KW_THEN, S("`then` after the `elseif` condition"), &t));
            TRY(parse_block(p, &arm.body));
            TRY(ParseArmVec_push(&p->arms, arm));
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
    *out = s;
    return OK;
}

// An `if`, its arms' stack put back on every way out.
static inline STATUS_MUST_USE Status parse_if_statement(Parser *p, Pos pos, Stmt *out) {
    size_t from = p->arms.len;
    Status s = parse_if_arms(p, pos, from, out);
    p->arms.len = from;
    return s;
}

// A loop's body, with its name declared in a scope of its own, which is put
// back on every way out.
static inline STATUS_MUST_USE Status parse_loop_body(Parser *p, Token name_tok, uint16_t *slot, Stmts *body) {
    p->frame.depth++;
    size_t mark = p->frame.locals.len;
    Status s = parse_declare(p, name_tok.text, parse_pos(name_tok.line, name_tok.column), slot);
    if (s == OK) {
        p->frame.loops++;
        s = parse_block(p, body);
        p->frame.loops--;
    }
    p->frame.locals.len = mark;
    p->frame.depth--;
    TRY(s);
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
        TRY(ParseStrVec_push(&p->consts, t.text));
        TRY(ParseOwnerVec_push(&p->const_owner, p->in_prelude));
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
        Status body = parse_block(p, &s.as.while_.body);
        p->frame.loops--;
        TRY(body);
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
    Status s = parse_statement_body(p, out);
    parse_leave(p);
    return s;
}

// The statements of a block from the stack's `from` on, to its end.
static inline STATUS_MUST_USE Status parse_block_body(Parser *p, size_t from, Stmts *out) {
    while (!parse_block_ends(p->tok.tag)) {
        if (p->tok.tag == TAG_SEMICOLON) {
            TRY(parse_advance(p));
            continue;
        }
        Stmt s;
        TRY(parse_statement(p, &s));
        TRY(ParseStmtVec_push(&p->stmts, s));
        if (s.kind == STMT_RETURN) {
            while (p->tok.tag == TAG_SEMICOLON) TRY(parse_advance(p));
            if (!parse_block_ends(p->tok.tag)) return parse_fail(p, parse_here(p), CLASS_SYNTAX, S("`return` must be the last statement of its block"));
            break;
        }
    }
    return parse_take_stmts(p, from, out);
}

// Statements up to the end of a block, in a scope of their own. Its scope,
// its statements' stack and its level are put back on every way out, as
// Zig's `defer`s do.
static inline STATUS_MUST_USE Status parse_block(Parser *p, Stmts *out) {
    TRY(parse_enter(p));
    p->frame.depth++;
    size_t mark = p->frame.locals.len;
    size_t from = p->stmts.len;
    Status s = parse_block_body(p, from, out);
    p->stmts.len = from;
    p->frame.locals.len = mark;
    p->frame.depth--;
    parse_leave(p);
    return s;
}

// ── functions and the preludes they load ──

static inline STATUS_MUST_USE Status parse_load_prelude(Parser *p, size_t k, size_t i);

// The library that brought `name`, among those `use`d above, or NULL.
static inline const ParseExtra *parse_library_having(const Parser *p, Str name) {
    for (size_t k = 0; k < p->extra.len; k++) {
        const ParseExtra *x = &at(p->extra, k);
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
    if (p->in_prelude == PARSE_SCRIPT && ParseFunctionNames_get(&p->function_names, name, index)) {
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

// Function `index`'s parameters and body, in the frame made for it.
static inline STATUS_MUST_USE Status parse_function_body(Parser *p, uint32_t index) {
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
    return OK;
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
        const ParseExtra *x = parse_library_having(p, name);
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
    if (p->in_prelude == PARSE_SCRIPT && ParseFunctionNames_get(&p->function_names, name, &seen))
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
            ParseExtra *x = &at(p->extra, p->in_prelude - 1);
            at(x->loaded, parse_index_sure(x->index, name)) = index;
        }
    } else {
        TRY(ParseFunctionNames_put(&p->function_names, name, index));
    }
    // Reserved now, so it may call itself; known before the body, so the body
    // may call it.
    Function f;
    memset(&f, 0, sizeof f);
    f.name = shown;
    f.pos = pos;
    TRY(ParseFunctionVec_push(&p->functions, f));

    ParseFrame saved = p->frame;
    bool saved_in_function = p->in_function;
    const ParseFrame *saved_script_frame = p->script_frame;
    // A prelude's function, loaded at its first call, may be parsed in the
    // middle of the script's: only the script's own top level counts.
    if (!saved_in_function && p->in_prelude == PARSE_SCRIPT) p->script_frame = &saved;
    memset(&p->frame, 0, sizeof p->frame);
    p->frame.locals = ParseLocalVec_make(p->arena);
    p->in_function = true;
    Status s = parse_function_body(p, index);
    // The caller's frame, put back on every way out, as Zig's `defer`.
    p->frame = saved;
    p->in_function = saved_in_function;
    p->script_frame = saved_script_frame;
    return s;
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
    Status s = parse_advance(p);
    if (s == OK) s = parse_function(p, parse_here(p));
    // The script's place, put back on every way out, as Zig's `defer`.
    p->lexer = saved_lexer;
    p->tok = saved_tok;
    p->has_ahead = saved_has_ahead;
    p->ahead = saved_ahead;
    p->in_prelude = saved_in_prelude;
    p->nesting = saved_nesting;
    return s;
}

#endif
