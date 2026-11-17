// parse_expr.h: expressions, as Zig Shale's src/script/parse.zig's
// `binary`, `unary`, `suffixed`, `primary`, `call` and `table` do:
// precedence climbing; a call resolved where it stands (the script's
// function, a prelude's, a builtin, or Lua's library refused by name) and
// held to its parameters; and a table literal that is a list or a map, never
// both. Each level of the nesting budget is left on every way out.

#ifndef SHALE_PARSE_EXPR_H
#define SHALE_PARSE_EXPR_H

#include "parse_stmt.h"

// ── expressions ──

typedef struct {
    BinOp op;
    uint8_t prec;
    bool right;
} ParseLevel;

static inline bool parse_binop(Tag tag, ParseLevel *out) {
    ParseLevel l = {BINOP_ADD, 0, false};
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

static inline STATUS_MUST_USE Status parse_binary(Parser *p, uint8_t min, Expr **out);

// Precedence climbing: operators of at least `min` bind here; `..` binds to
// the right, every other to the left.
static inline STATUS_MUST_USE Status parse_binary_body(Parser *p, uint8_t min, Expr **out) {
    Expr *left = NULL;
    TRY(parse_unary(p, &left));
    ParseLevel lv = {0};
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
    return OK;
}

// A binary expression, one level of the nesting budget, left on every way
// out.
static inline STATUS_MUST_USE Status parse_binary(Parser *p, uint8_t min, Expr **out) {
    TRY(parse_enter(p));
    Status s = parse_binary_body(p, min, out);
    parse_leave(p);
    return s;
}

static inline STATUS_MUST_USE Status parse_expr(Parser *p, Expr **out) {
    return parse_binary(p, 0, out);
}

// A unary operator's operand, from the operator on, folding a minus into
// its literal.
static inline STATUS_MUST_USE Status parse_unary_operand(Parser *p, Pos pos, UnOp op, Expr **out) {
    TRY(parse_advance(p));
    // The magnitude of the smallest integer is no int64, so only a minus right
    // before it makes it one.
    if (op == UNOP_NEG && p->tok.tag == TAG_INT && p->tok.min_magnitude) {
        TRY(parse_advance(p));
        Expr *e = parse_new_expr(p, pos, EXPR_INT);
        if (e == NULL) return STOP;
        e->as.integer = INT64_MIN;
        *out = e;
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
        return OK;
    }
    Expr *e = parse_new_expr(p, pos, EXPR_UNARY);
    if (e == NULL) return STOP;
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    *out = e;
    return OK;
}

// A unary operator charges a level of the nesting budget, left on every way
// out; anything else is a suffixed expression.
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
    Status s = parse_unary_operand(p, pos, op, out);
    parse_leave(p);
    return s;
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

// A primary expression and the `.k` and `[k]` after it.
static inline STATUS_MUST_USE Status parse_suffixed_body(Parser *p, Expr **out) {
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
    return OK;
}

// A suffixed expression, one level of the nesting budget, left on every way
// out.
static inline STATUS_MUST_USE Status parse_suffixed(Parser *p, Expr **out) {
    TRY(parse_enter(p));
    Status s = parse_suffixed_body(p, out);
    parse_leave(p);
    return s;
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

// A call's arguments, pushed on the expressions' stack from `from` on, held
// to its function's parameters (`params` -1 for a builtin's, held where it
// runs).
static inline STATUS_MUST_USE Status parse_call_args(Parser *p, Pos pos, CallExpr *call, Str shown, int32_t params, uint16_t optional, size_t from, Expr **out) {
    if (p->tok.tag != TAG_RPAREN) {
        for (;;) {
            Expr *arg = NULL;
            TRY(parse_expr(p, &arg));
            TRY(ParseExprVec_push(&p->exprs, arg));
            if (p->tok.tag != TAG_COMMA) break;
            TRY(parse_advance(p));
        }
    }
    Token close = {0};
    TRY(parse_expect(p, TAG_RPAREN, S("`)` to close the call"), &close));
    size_t given = p->exprs.len - from;
    if (call->target == TARGET_BUILTIN && call->builtin == BUILTIN_ARG) {
        Expr *first = given > 0 ? at(p->exprs, from) : NULL;
        if (first != NULL && first->kind == EXPR_STRING) {
            bool seen = false;
            for (size_t k = 0; k < p->args_read.len; k++) {
                if (str_eq(at(p->args_read, k), first->as.string)) seen = true;
            }
            if (!seen) TRY(ParseStrVec_push(&p->args_read, first->as.string));
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
            TRY(ParseExprVec_push(&p->exprs, nil));
        }
    }
    TRY(parse_take_exprs(p, from, &call->args));
    Expr *e = parse_new_expr(p, pos, EXPR_CALL);
    if (e == NULL) return STOP;
    e->as.call = *call;
    *out = e;
    return OK;
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
    Status s = parse_call_args(p, pos, &call, shown, params, optional, from, out);
    // The arguments' stack, put back on every way out, as Zig's `defer`.
    p->exprs.len = from;
    return s;
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
        return ParseEntryVec_push(&p->entries, en);
    }
    if (entries > 0) return parse_fail(p, here, CLASS_TYPE, S("a table literal mixes positional and keyed entries: a list or a map, not both"));
    if (value->kind == EXPR_NIL) return parse_fail(p, here, CLASS_TYPE, S("a list cannot hold nil"));
    return ParseExprVec_push(&p->exprs, value);
}

// A table literal's entries, from its `{` on, pushed on the stacks from
// `items_from` and `entries_from` on.
static inline STATUS_MUST_USE Status parse_table_body(Parser *p, Pos pos, size_t items_from, size_t entries_from, Expr **out) {
    TRY(parse_advance(p));
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
    *out = e;
    return OK;
}

// A table literal, one level of the nesting budget; the level and both
// stacks are put back on every way out, as Zig's `defer`s.
static inline STATUS_MUST_USE Status parse_table(Parser *p, Pos pos, Expr **out) {
    TRY(parse_enter(p));
    size_t items_from = p->exprs.len;
    size_t entries_from = p->entries.len;
    Status s = parse_table_body(p, pos, items_from, entries_from, out);
    p->exprs.len = items_from;
    p->entries.len = entries_from;
    parse_leave(p);
    return s;
}

#endif
