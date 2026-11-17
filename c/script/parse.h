// parse.h: parses tokens into a `Program`, resolving names as it goes. Lua's
// constructs this language does not take are refused by name, and the depth is
// held to the nesting budget so that no source can run the parser off its
// stack. The preludes after the core's (the Linux and git preludes, a repo's, a
// user's) and the libraries a `use` names are indexed by their lines and each
// function parsed at its first call.
//
// It is the parser's one header to include. Its pieces, in the order each
// includes the one before: prelude_index.h (the preludes' indexes),
// parse_state.h (the parser and its primitives), parse_stmt.h (statements and
// functions), parse_expr.h (expressions) and parse_library.h (the preludes'
// checks and `use`). Here is `parse_program`.

#ifndef SHALE_PARSE_H
#define SHALE_PARSE_H

#include "parse_library.h"

// ── the whole source ──

// The whole source, parsed, after the core prelude (`core`) and the
// preludes after it, each checked first. On STOP, `p->stopped` says the parse
// stopped and `p->stop` where and why; otherwise the arena refused.
static inline STATUS_MUST_USE Status parse_program(Parser *p, Str core, Preludes preludes, ParseLoader loader, Program *out) {
    p->core = core;
    p->loader = loader;
    TRY(parse_index(p->arena, core, parse_next_function, &p->core_index));
    TRY(parse_slots(p->arena, p->core_index.len, &p->core_loaded));
    p->extra_cap = preludes.len + PARSE_LIBRARIES_MAX;
    ParseExtra *extras = ARENA_ARRAY(p->arena, ParseExtra, p->extra_cap);
    if (extras == NULL) return STOP;
    p->extra.ptr = extras;
    p->extra.len = 0;
    for (size_t i = 0; i < preludes.len; i++) {
        ParseExtra x = {0};
        memset(&x, 0, sizeof x);
        x.prelude = at(preludes, i);
        TRY(parse_index(p->arena, x.prelude.source, parse_next_function, &x.index));
        TRY(parse_slots(p->arena, x.index.len, &x.loaded));
        p->extra.len++;
        ParseExtras room = {extras, p->extra_cap};
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
