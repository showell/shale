// compile.h: compiles a script's functions into one array of ops for vm.h, a
// stack machine over the walker's semantics: Zig Shale's
// src/script/compile.zig, op for op.
//
// Steps are charged exactly as Zig's tree-walker charges them, a step as it
// enters each node, before the node's own work, with the depth budget checked
// there against how deep the walk is. The compiler records those enters in
// the walker's order, each with its node's place and its static depth in its
// function (`VmEnter`), and hangs the run of enters that precedes an op's work
// on that op (`VmOp.enter`, `VmOp.enters`). The VM runs them first. A label (a
// jump's target) first flushes the enters pending before it onto a `nop`, so a
// jump never repeats or skips them.
//
// A call's frame starts its walk where the walker's would: the caller's walk
// at the call (its frame's base plus the call's static depth), plus one for
// the call itself (`VmOp.c` of `call` and `attempt`).

#ifndef SHALE_COMPILE_H
#define SHALE_COMPILE_H

#include "ast.h"

typedef enum {
    // Does nothing; carries the enters flushed before a label.
    OP_NOP,
    OP_PUSH_NIL,
    // `a`: 1 for true.
    OP_PUSH_BOOL,
    // `a`: the index in `Code.ints`.
    OP_PUSH_INT,
    // `a`: the index in `Code.strings`.
    OP_PUSH_STRING,
    // `a`: the slot.
    OP_PUSH_LOCAL,
    // `a`: the const's index.
    OP_PUSH_CONST,
    // Discards the top value.
    OP_POP,
    // `a`: the slot.
    OP_STORE_LOCAL,
    // `a`: the slot; `b`: the const's index.
    OP_STORE_CONST,
    // obj, key -> value.
    OP_INDEX_GET,
    // value, obj, key ->.
    OP_INDEX_SET,
    // Pushes an empty table.
    OP_NEW_TABLE,
    // `a`: the items to come; pushes the list.
    OP_LIST_BEGIN,
    // list, item -> list (`pos` the item's).
    OP_LIST_ITEM,
    // Pushes an empty map.
    OP_MAP_BEGIN,
    // map, value -> map: `a` the key (`Code.strings`), `b` the map's place
    // (`Code.positions`), `pos` the entry's.
    OP_MAP_ENTRY,
    // `a`: the UnOp.
    OP_UNARY,
    // `a`: the BinOp (not `and`, `or` or `..`).
    OP_BINARY,
    // `a` parts -> one string; `b`: where each part is checked
    // (`Code.positions`, `a` of them).
    OP_CONCAT,
    // `a`: the target.
    OP_JUMP,
    // cond ->; to `a` when it is false.
    OP_JUMP_FALSE,
    // To `a`, keeping the value, when it is false (`and`); else pops it.
    OP_AND_JUMP,
    // To `a`, keeping the value, when it is true (`or`); else pops it.
    OP_OR_JUMP,
    // `a`: the builtin; `b`: the arguments.
    OP_BUILTIN,
    // `a`: the arguments; `b`: their places (`Code.positions`).
    OP_PRINT,
    // A call to builtin `a` with more arguments than any takes.
    OP_TOO_MANY,
    // `yield()`: what the run still reaches, copied into a fresh arena; pushes nil.
    OP_COLLECT,
    // The call-depth check before a function's arguments.
    OP_CALL_PREP,
    // `a`: the function; `b`: the arguments; `c`: the callee's walk from the
    // caller's frame.
    OP_CALL,
    // As OP_CALL, its error a value.
    OP_ATTEMPT,
    // value ->: the frame's end.
    OP_RET,
    // The frame's end, returning nil.
    OP_RET_NIL,
    // A `for`'s start, end or step (`a`: 0, 1, 2) checked an integer.
    OP_FOR_INT,
    // from, to[, step] ->: `a` the hidden slots, `b` 1 when a step is given.
    OP_FOR_NUM_INIT,
    // `a` the hidden slots, `b` the exit, `c` the loop's slot.
    OP_FOR_NUM_ITER,
    // `a` the hidden slots, `b` the loop's head, `c` its exit.
    OP_FOR_NUM_NEXT,
    // list ->: `a` the hidden slots, `b` the exit, `c` the statement's place
    // (`Code.positions`).
    OP_FOR_LIST_INIT,
    // `a` the hidden slots, `b` the exit, `c` the loop's slot.
    OP_FOR_LIST_ITER,
} OpTag;

typedef struct {
    OpTag tag;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    // Where an error at this op is reported, unless `tag` names another place.
    Pos pos;
    // The walker's enters before this op's work: `Code.enters` from `enter`,
    // `enters` of them, the deepest `deepest` below the frame's walk.
    uint32_t enter;
    uint32_t enters;
    uint32_t deepest;
} VmOp;

// One node the walker enters: where, and how deep in its function.
typedef struct {
    Pos pos;
    uint32_t depth;
} VmEnter;

typedef struct {
    Str name;
    Pos pos;
    // The index of its first op.
    uint32_t entry;
    // Its slots, the hidden ones its loops keep among them.
    uint32_t frame;
    // The most operands it has on the stack at once.
    uint32_t operands;
} CodeFunc;

VEC_DEFINE(VmOpVec, VmOp)
// VmEnter is 12 bytes, so VmEnterVec_pop is a return that TCC 0.9.27
// miscompiles when it goes straight into a call (style.md, "Declarations").
// Nothing pops an enter; code that does keeps the answer in a local first.
VEC_DEFINE(VmEnterVec, VmEnter) // struct return: VmEnterVec_pop is never called
VEC_DEFINE(IntVec, int64_t)
VEC_DEFINE(PosVec, Pos)
VEC_DEFINE(U32Vec, uint32_t)
VEC_DEFINE(LiteralVec, Str)
SLICE_TYPE(CodeFuncs, CodeFunc);

typedef struct {
    VmOpVec ops;
    VmEnterVec enters;
    IntVec ints;
    // String literals and map keys.
    LiteralVec strings;
    // The places an op checks beyond its own `pos`.
    PosVec positions;
    // Indexed by OP_CALL's `a`.
    CodeFuncs functions;
    // The script's top level.
    CodeFunc main;
} Code;

// A loop's jumps still to be pointed: its `break`s, and its `continue`s where
// the target comes later (a numeric `for`'s next).
typedef struct {
    U32Vec breaks;
    U32Vec continues;
    bool has_head;
    uint32_t head;
} CompileLoop;

VEC_DEFINE(CompileLoopVec, CompileLoop)

typedef struct {
    Arena *arena;
    // The most arguments a builtin takes (BUILTINS_MOST_ARGS).
    uint32_t most_args;
    // The depth budget. A node at this static depth or past it stops any run
    // that reaches it, so it compiles to its enter and nothing under it. This
    // also bounds the compiler's own recursion, as the walker's is bounded.
    uint32_t depth_most;
    Code code;
    // The first enter not yet hung on an op.
    uint32_t pending;
    // The function being compiled.
    uint32_t hidden;
    uint32_t frame;
    uint32_t operands;
    uint32_t operands_most;
    CompileLoopVec loops;
} Compiler;

static inline STATUS_MUST_USE Status compile_enter(Compiler *c, Pos pos, uint32_t depth) {
    VmEnter e = {pos, depth};
    return VmEnterVec_push(&c->code.enters, e);
}

// An op, the pending enters hung on it; `*at` its index (NULL when unwanted).
static inline STATUS_MUST_USE Status compile_emit_at(Compiler *c, OpTag tag, uint32_t a, uint32_t b, uint32_t cc, Pos pos, uint32_t *at_) {
    VmOp o;
    memset(&o, 0, sizeof o);
    o.tag = tag;
    o.a = a;
    o.b = b;
    o.c = cc;
    o.pos = pos;
    uint32_t n = (uint32_t)c->code.enters.len;
    o.enter = c->pending;
    o.enters = n - c->pending;
    o.deepest = 0;
    for (uint32_t k = c->pending; k < n; k++) {
        uint32_t d = at(c->code.enters, k).depth;
        if (d > o.deepest) o.deepest = d;
    }
    c->pending = n;
    TRY(VmOpVec_push(&c->code.ops, o));
    if (at_ != NULL) *at_ = (uint32_t)(c->code.ops.len - 1);
    return OK;
}

static inline STATUS_MUST_USE Status compile_emit(Compiler *c, OpTag tag, uint32_t a, uint32_t b, Pos pos) {
    return compile_emit_at(c, tag, a, b, 0, pos, NULL);
}

// The pending enters on a `nop`, so what follows can be a target.
static inline STATUS_MUST_USE Status compile_flush(Compiler *c) {
    if (c->pending == c->code.enters.len) return OK;
    Pos last = at(c->code.enters, c->code.enters.len - 1).pos;
    return compile_emit(c, OP_NOP, 0, 0, last);
}

// The next op's index, as a jump's target.
static inline STATUS_MUST_USE Status compile_label(Compiler *c, uint32_t *out) {
    TRY(compile_flush(c));
    *out = (uint32_t)c->code.ops.len;
    return OK;
}

static inline void compile_patch(Compiler *c, uint32_t op, uint32_t target) {
    VmOp *o = &at(c->code.ops, op);
    switch (o->tag) {
    case OP_JUMP:
    case OP_JUMP_FALSE:
    case OP_AND_JUMP:
    case OP_OR_JUMP: o->a = target; return;
    case OP_FOR_NUM_ITER:
    case OP_FOR_LIST_ITER:
    case OP_FOR_LIST_INIT: o->b = target; return;
    case OP_FOR_NUM_NEXT: o->c = target; return;
    default: break;
    }
    fprintf(stderr, "compile: a patch of an op that does not jump\n");
    abort();
}

static inline void compile_push(Compiler *c, uint32_t n) {
    c->operands += n;
    if (c->operands > c->operands_most) c->operands_most = c->operands;
}

static inline void compile_pop(Compiler *c, uint32_t n) {
    c->operands -= n;
}

static inline STATUS_MUST_USE Status compile_position(Compiler *c, Pos pos, uint32_t *out) {
    TRY(PosVec_push(&c->code.positions, pos));
    *out = (uint32_t)(c->code.positions.len - 1);
    return OK;
}

static inline uint32_t compile_take_hidden(Compiler *c, uint32_t n) {
    uint32_t h = c->hidden;
    c->hidden += n;
    if (c->hidden > c->frame) c->frame = c->hidden;
    return h;
}

static inline CompileLoop *compile_loop_top(Compiler *c) {
    return &at(c->loops, c->loops.len - 1);
}

static inline STATUS_MUST_USE Status compile_loop_open(Compiler *c, bool has_head, uint32_t head) {
    CompileLoop l;
    l.breaks = U32Vec_make(c->arena);
    l.continues = U32Vec_make(c->arena);
    l.has_head = has_head;
    l.head = head;
    return CompileLoopVec_push(&c->loops, l);
}

// The loop closed: its breaks pointed at `end` and its continues at `next`.
static inline void compile_loop_close(Compiler *c, uint32_t end, uint32_t next) {
    CompileLoop l = CompileLoopVec_pop(&c->loops);
    for (size_t k = 0; k < l.breaks.len; k++) compile_patch(c, at(l.breaks, k), end);
    for (size_t k = 0; k < l.continues.len; k++) compile_patch(c, at(l.continues, k), next);
}

static inline STATUS_MUST_USE Status compile_expr(Compiler *c, const Expr *e, uint32_t d);
static inline STATUS_MUST_USE Status compile_block(Compiler *c, Stmts stmts, uint32_t depth);

static inline STATUS_MUST_USE Status compile_if(Compiler *c, const Stmt *s, uint32_t d) {
    U32Vec ends = U32Vec_make(c->arena);
    Arms arms = s->as.if_.arms;
    for (size_t k = 0; k < arms.len; k++) {
        const Arm *arm = &at(arms, k);
        TRY(compile_expr(c, arm->cond, d + 1));
        uint32_t skip = 0;
        TRY(compile_emit_at(c, OP_JUMP_FALSE, 0, 0, 0, arm->cond->pos, &skip));
        compile_pop(c, 1);
        TRY(compile_block(c, arm->body, d + 1));
        uint32_t j = 0;
        TRY(compile_emit_at(c, OP_JUMP, 0, 0, 0, s->pos, &j));
        TRY(U32Vec_push(&ends, j));
        uint32_t here = 0;
        TRY(compile_label(c, &here));
        compile_patch(c, skip, here);
    }
    if (s->as.if_.has_otherwise) TRY(compile_block(c, s->as.if_.otherwise, d + 1));
    uint32_t end = 0;
    TRY(compile_label(c, &end));
    for (size_t k = 0; k < ends.len; k++) compile_patch(c, at(ends, k), end);
    return OK;
}

static inline STATUS_MUST_USE Status compile_while(Compiler *c, const Stmt *s, uint32_t d) {
    uint32_t head = 0;
    TRY(compile_label(c, &head));
    const Expr *cond = s->as.while_.cond;
    TRY(compile_expr(c, cond, d + 1));
    uint32_t exit = 0;
    TRY(compile_emit_at(c, OP_JUMP_FALSE, 0, 0, 0, cond->pos, &exit));
    compile_pop(c, 1);
    TRY(compile_loop_open(c, true, head));
    TRY(compile_block(c, s->as.while_.body, d + 1));
    TRY(compile_emit(c, OP_JUMP, head, 0, s->pos));
    uint32_t end = 0;
    TRY(compile_label(c, &end));
    compile_patch(c, exit, end);
    compile_loop_close(c, end, end);
    return OK;
}

static inline STATUS_MUST_USE Status compile_numeric_for(Compiler *c, const Stmt *s, uint32_t d) {
    const Expr *step = s->as.numeric_for.step;
    TRY(compile_expr(c, s->as.numeric_for.from, d + 1));
    TRY(compile_emit(c, OP_FOR_INT, 0, 0, s->as.numeric_for.from->pos));
    TRY(compile_expr(c, s->as.numeric_for.to, d + 1));
    TRY(compile_emit(c, OP_FOR_INT, 1, 0, s->as.numeric_for.to->pos));
    if (step != NULL) {
        TRY(compile_expr(c, step, d + 1));
        TRY(compile_emit(c, OP_FOR_INT, 2, 0, step->pos));
    }
    uint32_t h = compile_take_hidden(c, 3);
    TRY(compile_emit(c, OP_FOR_NUM_INIT, h, step != NULL ? 1 : 0, s->pos));
    compile_pop(c, step != NULL ? 3 : 2);
    uint32_t head = 0;
    TRY(compile_label(c, &head));
    uint32_t iter = 0;
    TRY(compile_emit_at(c, OP_FOR_NUM_ITER, h, 0, s->as.numeric_for.slot, s->pos, &iter));
    TRY(compile_loop_open(c, false, 0));
    TRY(compile_block(c, s->as.numeric_for.body, d + 1));
    uint32_t next = 0;
    TRY(compile_label(c, &next));
    uint32_t stepped = 0;
    TRY(compile_emit_at(c, OP_FOR_NUM_NEXT, h, head, 0, s->pos, &stepped));
    uint32_t end = 0;
    TRY(compile_label(c, &end));
    compile_patch(c, iter, end);
    compile_patch(c, stepped, end);
    compile_loop_close(c, end, next);
    c->hidden -= 3;
    return OK;
}

static inline STATUS_MUST_USE Status compile_list_for(Compiler *c, const Stmt *s, uint32_t d) {
    const Expr *list = s->as.list_for.list;
    TRY(compile_expr(c, list, d + 1));
    uint32_t h = compile_take_hidden(c, 3);
    uint32_t place = 0;
    TRY(compile_position(c, s->pos, &place));
    uint32_t init = 0;
    TRY(compile_emit_at(c, OP_FOR_LIST_INIT, h, 0, place, list->pos, &init));
    compile_pop(c, 1);
    uint32_t head = 0;
    TRY(compile_label(c, &head));
    uint32_t iter = 0;
    TRY(compile_emit_at(c, OP_FOR_LIST_ITER, h, 0, s->as.list_for.slot, s->pos, &iter));
    TRY(compile_loop_open(c, true, head));
    TRY(compile_block(c, s->as.list_for.body, d + 1));
    TRY(compile_emit(c, OP_JUMP, head, 0, s->pos));
    uint32_t end = 0;
    TRY(compile_label(c, &end));
    compile_patch(c, init, end);
    compile_patch(c, iter, end);
    // The list's slot let go at the exit, a break's too: a finished loop's
    // list is no longer the run's to keep at a `yield`.
    TRY(compile_emit(c, OP_PUSH_NIL, 0, 0, s->pos));
    compile_push(c, 1);
    TRY(compile_emit(c, OP_STORE_LOCAL, h, 0, s->pos));
    compile_pop(c, 1);
    compile_loop_close(c, end, end);
    c->hidden -= 3;
    return OK;
}

static inline STATUS_MUST_USE Status compile_stmt(Compiler *c, const Stmt *s, uint32_t d) {
    TRY(compile_enter(c, s->pos, d));
    if (d >= c->depth_most) return compile_emit(c, OP_NOP, 0, 0, s->pos);
    switch (s->kind) {
    case STMT_LOCAL:
        if (s->as.local.value != NULL) {
            TRY(compile_expr(c, s->as.local.value, d + 1));
        } else {
            TRY(compile_emit(c, OP_PUSH_NIL, 0, 0, s->pos));
            compile_push(c, 1);
        }
        TRY(compile_emit(c, OP_STORE_LOCAL, s->as.local.slot, 0, s->pos));
        compile_pop(c, 1);
        return OK;
    case STMT_CONSTANT:
        TRY(compile_expr(c, s->as.constant.value, d + 1));
        TRY(compile_emit(c, OP_STORE_CONST, s->as.constant.slot, s->as.constant.index, s->pos));
        compile_pop(c, 1);
        return OK;
    case STMT_ASSIGN: {
        const Expr *target = s->as.assign.target;
        TRY(compile_expr(c, s->as.assign.value, d + 1));
        if (target->kind == EXPR_LOCAL) {
            TRY(compile_emit(c, OP_STORE_LOCAL, target->as.local, 0, s->pos));
            compile_pop(c, 1);
            return OK;
        }
        // The parser takes no other target than a local or an index.
        TRY(compile_expr(c, target->as.index.obj, d + 1));
        TRY(compile_expr(c, target->as.index.key, d + 1));
        TRY(compile_emit(c, OP_INDEX_SET, 0, 0, target->pos));
        compile_pop(c, 3);
        return OK;
    }
    case STMT_CALL:
        TRY(compile_expr(c, s->as.call, d + 1));
        TRY(compile_emit(c, OP_POP, 0, 0, s->pos));
        compile_pop(c, 1);
        return OK;
    case STMT_IF: return compile_if(c, s, d);
    case STMT_WHILE: return compile_while(c, s, d);
    case STMT_NUMERIC_FOR: return compile_numeric_for(c, s, d);
    case STMT_LIST_FOR: return compile_list_for(c, s, d);
    case STMT_RETURN:
        if (s->as.return_ != NULL) {
            TRY(compile_expr(c, s->as.return_, d + 1));
            TRY(compile_emit(c, OP_RET, 0, 0, s->pos));
            compile_pop(c, 1);
            return OK;
        }
        return compile_emit(c, OP_RET_NIL, 0, 0, s->pos);
    case STMT_BREAK: {
        uint32_t j = 0;
        TRY(compile_emit_at(c, OP_JUMP, 0, 0, 0, s->pos, &j));
        return U32Vec_push(&compile_loop_top(c)->breaks, j);
    }
    case STMT_CONTINUE: {
        CompileLoop *l = compile_loop_top(c);
        bool has_head = l->has_head;
        uint32_t head = l->head;
        uint32_t j = 0;
        TRY(compile_emit_at(c, OP_JUMP, has_head ? head : 0, 0, 0, s->pos, &j));
        if (!has_head) TRY(U32Vec_push(&l->continues, j));
        return OK;
    }
    case STMT_DO: return compile_block(c, s->as.do_, d + 1);
    }
    return OK;
}

static inline STATUS_MUST_USE Status compile_block(Compiler *c, Stmts stmts, uint32_t depth) {
    for (size_t k = 0; k < stmts.len; k++) TRY(compile_stmt(c, &at(stmts, k), depth));
    return OK;
}

// A `..` chain, as the walker's `concatChain` evaluates it: the chain's inner
// `..` nodes are not entered, and every part is, one deeper than the chain.
// Part k is checked at the `..` whose left operand it is, the last part at the
// innermost's. The chain's places are kept apart until its parts are
// compiled, since a part that is itself a chain, a map or a `print` adds
// places of its own.
static inline STATUS_MUST_USE Status compile_concat(Compiler *c, Pos pos, const Expr *le, const Expr *re, uint32_t d) {
    PosVec places = PosVec_make(c->arena);
    TRY(PosVec_push(&places, pos));
    TRY(compile_expr(c, le, d + 1));
    const Expr *r = re;
    while (r->kind == EXPR_BINARY && r->as.binary.op == BINOP_CONCAT) {
        TRY(PosVec_push(&places, r->pos));
        TRY(compile_expr(c, r->as.binary.left, d + 1));
        r = r->as.binary.right;
    }
    TRY(PosVec_push(&places, at(places, places.len - 1)));
    TRY(compile_expr(c, r, d + 1));
    uint32_t n = (uint32_t)places.len;
    uint32_t first = (uint32_t)c->code.positions.len;
    for (size_t k = 0; k < places.len; k++) TRY(PosVec_push(&c->code.positions, at(places, k)));
    TRY(compile_emit(c, OP_CONCAT, n, first, pos));
    compile_pop(c, n - 1);
    return OK;
}

static inline STATUS_MUST_USE Status compile_call(Compiler *c, Pos pos, const CallExpr *call, uint32_t d) {
    uint32_t argc = (uint32_t)call->args.len;
    switch (call->target) {
    case TARGET_BUILTIN: {
        Builtin b = call->builtin;
        if (b == BUILTIN_PRINT) {
            uint32_t first = (uint32_t)c->code.positions.len;
            for (size_t k = 0; k < call->args.len; k++) TRY(PosVec_push(&c->code.positions, at(call->args, k)->pos));
            for (size_t k = 0; k < call->args.len; k++) TRY(compile_expr(c, at(call->args, k), d + 1));
            TRY(compile_emit(c, OP_PRINT, argc, first, pos));
            compile_pop(c, argc);
            compile_push(c, 1);
            return OK;
        }
        if (argc > c->most_args) {
            TRY(compile_emit(c, OP_TOO_MANY, (uint32_t)b, 0, pos));
            compile_push(c, 1);
            return OK;
        }
        if (b == BUILTIN_YIELD && argc == 0) {
            TRY(compile_emit(c, OP_COLLECT, 0, 0, pos));
            compile_push(c, 1);
            return OK;
        }
        for (size_t k = 0; k < call->args.len; k++) TRY(compile_expr(c, at(call->args, k), d + 1));
        TRY(compile_emit(c, OP_BUILTIN, (uint32_t)b, argc, pos));
        compile_pop(c, argc);
        compile_push(c, 1);
        return OK;
    }
    case TARGET_FUNCTION:
        TRY(compile_emit(c, OP_CALL_PREP, call->function, 0, pos));
        for (size_t k = 0; k < call->args.len; k++) TRY(compile_expr(c, at(call->args, k), d + 1));
        TRY(compile_emit_at(c, OP_CALL, call->function, argc, d + 1, pos, NULL));
        compile_pop(c, argc);
        compile_push(c, 1);
        return OK;
    case TARGET_ATTEMPT:
        for (size_t k = 0; k < call->args.len; k++) TRY(compile_expr(c, at(call->args, k), d + 1));
        TRY(compile_emit_at(c, OP_ATTEMPT, call->function, argc, d + 1, pos, NULL));
        compile_pop(c, argc);
        compile_push(c, 1);
        return OK;
    }
    return OK;
}

static inline STATUS_MUST_USE Status compile_expr(Compiler *c, const Expr *e, uint32_t d) {
    TRY(compile_enter(c, e->pos, d));
    if (d >= c->depth_most) {
        // No run gets past this node's enter; a placeholder keeps the operand
        // count right.
        TRY(compile_emit(c, OP_PUSH_NIL, 0, 0, e->pos));
        compile_push(c, 1);
        return OK;
    }
    switch (e->kind) {
    case EXPR_NIL:
        TRY(compile_emit(c, OP_PUSH_NIL, 0, 0, e->pos));
        compile_push(c, 1);
        return OK;
    case EXPR_BOOLEAN:
        TRY(compile_emit(c, OP_PUSH_BOOL, e->as.boolean ? 1 : 0, 0, e->pos));
        compile_push(c, 1);
        return OK;
    case EXPR_INT:
        TRY(IntVec_push(&c->code.ints, e->as.integer));
        TRY(compile_emit(c, OP_PUSH_INT, (uint32_t)(c->code.ints.len - 1), 0, e->pos));
        compile_push(c, 1);
        return OK;
    case EXPR_STRING:
        TRY(LiteralVec_push(&c->code.strings, e->as.string));
        TRY(compile_emit(c, OP_PUSH_STRING, (uint32_t)(c->code.strings.len - 1), 0, e->pos));
        compile_push(c, 1);
        return OK;
    case EXPR_LOCAL:
        TRY(compile_emit(c, OP_PUSH_LOCAL, e->as.local, 0, e->pos));
        compile_push(c, 1);
        return OK;
    case EXPR_CONSTANT:
        TRY(compile_emit(c, OP_PUSH_CONST, e->as.constant, 0, e->pos));
        compile_push(c, 1);
        return OK;
    case EXPR_INDEX:
        TRY(compile_expr(c, e->as.index.obj, d + 1));
        TRY(compile_expr(c, e->as.index.key, d + 1));
        TRY(compile_emit(c, OP_INDEX_GET, 0, 0, e->pos));
        compile_pop(c, 1);
        return OK;
    case EXPR_CALL: return compile_call(c, e->pos, &e->as.call, d);
    case EXPR_UNARY:
        TRY(compile_expr(c, e->as.unary.operand, d + 1));
        return compile_emit(c, OP_UNARY, (uint32_t)e->as.unary.op, 0, e->pos);
    case EXPR_BINARY: {
        BinOp op = e->as.binary.op;
        if (op == BINOP_AND || op == BINOP_OR) {
            TRY(compile_expr(c, e->as.binary.left, d + 1));
            uint32_t j = 0;
            TRY(compile_emit_at(c, op == BINOP_AND ? OP_AND_JUMP : OP_OR_JUMP, 0, 0, 0, e->pos, &j));
            compile_pop(c, 1);
            TRY(compile_expr(c, e->as.binary.right, d + 1));
            uint32_t here = 0;
            TRY(compile_label(c, &here));
            compile_patch(c, j, here);
            return OK;
        }
        if (op == BINOP_CONCAT) return compile_concat(c, e->pos, e->as.binary.left, e->as.binary.right, d);
        TRY(compile_expr(c, e->as.binary.left, d + 1));
        TRY(compile_expr(c, e->as.binary.right, d + 1));
        TRY(compile_emit(c, OP_BINARY, (uint32_t)op, 0, e->pos));
        compile_pop(c, 1);
        return OK;
    }
    case EXPR_LIST: {
        Exprs items = e->as.list;
        TRY(compile_emit(c, OP_LIST_BEGIN, (uint32_t)items.len, 0, e->pos));
        compile_push(c, 1);
        for (size_t k = 0; k < items.len; k++) {
            const Expr *it = at(items, k);
            TRY(compile_expr(c, it, d + 1));
            TRY(compile_emit(c, OP_LIST_ITEM, 0, 0, it->pos));
            compile_pop(c, 1);
        }
        return OK;
    }
    case EXPR_MAP: {
        MapEntries entries = e->as.map;
        TRY(compile_emit(c, OP_MAP_BEGIN, 0, 0, e->pos));
        compile_push(c, 1);
        uint32_t place = 0;
        TRY(compile_position(c, e->pos, &place));
        for (size_t k = 0; k < entries.len; k++) {
            const MapEntry *en = &at(entries, k);
            TRY(compile_expr(c, en->value, d + 1));
            TRY(LiteralVec_push(&c->code.strings, en->key));
            TRY(compile_emit(c, OP_MAP_ENTRY, (uint32_t)(c->code.strings.len - 1), place, en->pos));
            compile_pop(c, 1);
        }
        return OK;
    }
    case EXPR_EMPTY_TABLE:
        TRY(compile_emit(c, OP_NEW_TABLE, 0, 0, e->pos));
        compile_push(c, 1);
        return OK;
    }
    return OK;
}

static inline STATUS_MUST_USE Status compile_function(Compiler *c, const Function *f, CodeFunc *out) {
    c->hidden = f->frame;
    c->frame = f->frame;
    c->operands = 0;
    c->operands_most = 0;
    uint32_t entry = (uint32_t)c->code.ops.len;
    TRY(compile_block(c, f->body, 0));
    TRY(compile_emit(c, OP_RET_NIL, 0, 0, f->pos));
    if (c->operands != 0) {
        fprintf(stderr, "compile: a function ends with operands left on the stack\n");
        abort();
    }
    out->name = f->name;
    out->pos = f->pos;
    out->entry = entry;
    out->frame = c->frame;
    out->operands = c->operands_most;
    return OK;
}

// Compiles `program` into `a`. STOP only when the arena refuses, which the
// caller reads as the machine budget, as Zig does.
static inline STATUS_MUST_USE Status compile_program(Arena *a, const Program *program, uint32_t most_args, uint32_t depth_most, Code *out) {
    Compiler c;
    memset(&c, 0, sizeof c);
    c.arena = a;
    c.most_args = most_args;
    c.depth_most = depth_most;
    c.code.ops = VmOpVec_make(a);
    c.code.enters = VmEnterVec_make(a);
    c.code.ints = IntVec_make(a);
    c.code.strings = LiteralVec_make(a);
    c.code.positions = PosVec_make(a);
    c.loops = CompileLoopVec_make(a);
    CodeFunc *funcs = ARENA_ARRAY(a, CodeFunc, program->functions.len);
    if (funcs == NULL) return STOP;
    CodeFuncs all = {funcs, program->functions.len};
    for (size_t k = 0; k < all.len; k++) TRY(compile_function(&c, &at(program->functions, k), &at(all, k)));
    TRY(compile_function(&c, &program->main, &c.code.main));
    c.code.functions = all;
    *out = c.code;
    return OK;
}

#endif
