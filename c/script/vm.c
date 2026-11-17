// vm.c: runs a script's compiled ops (compile.h) on a run (run.h): the
// operand stack, the frames and a loop that never recurses. An `attempt` is
// a frame marked with its answer: a stop that is not a budget's unwinds to
// the innermost such frame, and the answer says what it was. Here too are
// the builtins' table and the copy a `yield` makes. A program includes it
// after builtins.c and builtins_text.c.

// ── the builtins' table ──

typedef struct {
    uint8_t at_least;
    uint8_t at_most;
    // NULL for `print`, the VM's own op, which never reaches builtins_call.
    BuiltinImpl impl;
} BuiltinSpec;

SLICE_TYPE(BuiltinSpecs, const BuiltinSpec);

// Every builtin's arity, in ast.h's order, and its function.
static BuiltinSpecs builtin_specs(void) {
    static const BuiltinSpec specs[] = {
        {1, 1, bi_ls},             // ls
        {1, 1, bi_stat},           // stat
        {1, 1, bi_exists},         // exists
        {1, 1, bi_is_dir},         // is_dir
        {1, 3, bi_read},           // read
        {2, 2, bi_read_or},        // read_or
        {1, 3, bi_read_lines},     // read_lines
        {2, 2, bi_line_at},        // line_at
        {2, 2, bi_grep},           // grep
        {1, 3, bi_count_lines},    // count_lines
        {2, 4, bi_count_in},       // count_in
        {2, 4, bi_find_in},        // find_in
        {2, 2, bi_replace},        // replace
        {2, 2, bi_append},         // append
        {1, 1, bi_mkdir},          // mkdir
        {1, 1, bi_remove},         // remove
        {1, 1, bi_remove_tree},    // remove_tree
        {1, 2, bi_walk},           // walk
        {2, 4, bi_edit},           // edit
        {2, 2, bi_split},          // split
        {2, 2, bi_join},           // join
        {2, 2, bi_starts_with},    // starts_with
        {2, 2, bi_ends_with},      // ends_with
        {2, 2, bi_contains},       // contains
        {2, 3, bi_find},           // find
        {2, 3, bi_sub},            // sub
        {1, 1, bi_bad_utf8},       // bad_utf8
        {1, 1, bi_trim},           // trim
        {1, 1, bi_trim_start},     // trim_start
        {1, 1, bi_trim_end},       // trim_end
        {1, 1, bi_upper},          // upper
        {1, 1, bi_lower},          // lower
        {1, 1, bi_tostring},       // tostring
        {1, 1, bi_tonumber},       // tonumber
        {1, 1, bi_parse_int},      // parse_int
        {1, 1, bi_lines},          // lines
        {1, 1, bi_words},          // words
        {2, 2, bi_like},           // like
        {2, 2, bi_rep},            // rep
        {3, 4, bi_substitute},     // substitute
        {1, 4, bi_path},           // path
        {2, 2, bi_match_path},     // match_path
        {1, 1, bi_basename},       // basename
        {1, 1, bi_is_name},        // is_name
        {2, 2, bi_strip_suffix},   // strip_suffix
        {3, 3, bi_between},        // between
        {2, 3, bi_re_find},        // re_find
        {2, 3, bi_re_match},       // re_match
        {3, 4, bi_re_replace},     // re_replace
        {2, 3, bi_re_find_all},    // re_find_all
        {2, 4, bi_min},            // min
        {2, 4, bi_max},            // max
        {1, 1, bi_abs},            // abs
        {1, 1, bi_date},           // date
        {1, 1, bi_seconds},        // seconds
        {1, 1, bi_human},          // human
        {2, 2, bi_pad},            // pad
        {2, 2, bi_table},          // table
        {2, 2, bi_push},           // push
        {1, 1, bi_pop},            // pop
        {1, 3, bi_sort},           // sort
        {1, 1, bi_keys},           // keys
        {2, 2, bi_has_key},        // has_key
        {2, 3, bi_get},            // get
        {1, 1, bi_count},          // count
        {1, 1, bi_reverse},        // reverse
        {2, 2, bi_take},           // take
        {2, 2, bi_last},           // last
        {2, 2, bi_same},           // same
        {0, 0, bi_now},            // now
        {1, 2, bi_arg},            // arg
        {1, 1, bi_fail},           // fail
        {1, 4, bi_host},           // host
        {0, 0, bi_budget},         // budget
        {0, 0, bi_call_bytes},     // call_bytes
        {0, 0, bi_yield},          // yield
        {0, 255, NULL},            // print, the VM's own op
    };
    // Sized by its specs: sized by the enum, a spec left out would be
    // zero-filled, a builtin of no arguments and no function.
    _Static_assert(sizeof specs / sizeof *specs == BUILTIN_COUNT_OF, "a spec for every builtin");
    BuiltinSpecs all = {specs, BUILTIN_COUNT_OF};
    return all;
}

// Whether builtin `b` changes the store: what a background run refuses.
static bool builtin_changes_store(Builtin b) {
    return b == BUILTIN_REPLACE || b == BUILTIN_APPEND || b == BUILTIN_MKDIR || b == BUILTIN_REMOVE || b == BUILTIN_REMOVE_TREE || b == BUILTIN_EDIT;
}

static STATUS_MUST_USE Status builtins_too_many(Run *r, Pos pos, Builtin b) {
    return run_failf(r, pos, CLASS_ARGUMENT, "`{s}` takes at most {d} arguments", S("`"), builtin_name(b), S("` takes at most "), run_unum(r, at(builtin_specs(), (size_t)b).at_most), S(" arguments"));
}

// Calls builtin `b` after counting its arguments against its spec.
static STATUS_MUST_USE Status builtins_call(Run *r, Pos pos, Builtin b, ConstValues args, Value *out) {
    const BuiltinSpec *s = &at(builtin_specs(), (size_t)b);
    if (builtin_changes_store(b)) r->tried_change = true;
    if (args.len < s->at_least || args.len > s->at_most) {
        Str name = builtin_name(b);
        Str plural = s->at_least == 1 ? S("") : S("s");
        if (s->at_least == s->at_most) return run_failf(r, pos, CLASS_ARGUMENT, "`{s}` takes {d} argument{s}, given {d}", S("`"), name, S("` takes "), run_unum(r, s->at_least), S(" argument"), plural, S(", given "), run_unum(r, args.len));
        return run_failf(r, pos, CLASS_ARGUMENT, "`{s}` takes {d} to {d} arguments, given {d}", S("`"), name, S("` takes "), run_unum(r, s->at_least), S(" to "), run_unum(r, s->at_most), S(" arguments, given "), run_unum(r, args.len));
    }
    if (s->impl == NULL) {
        fprintf(stderr, "vm: a call of `print`, the VM's own op, reached the builtins' table\n");
        abort();
    }
    return s->impl(r, pos, args, out);
}

// ── the copy at a yield ──

// The copy's bookkeeping, made fresh at each yield in an arena of its own:
// old table to its copy, and old string (where it starts and how long it is)
// to its copy, so a string or a table reached twice is copied once. Each key
// is the bytes of the old pointer (and length), as map.h keys by bytes.
typedef struct {
    Run *r;
    Pos pos;
    Arena *books;
    ValueMap seen_tables;
    ValueMap seen_strings;
    // Copies whose contents still point into the old arena: old, then new.
    ValueVec todo;
    uint64_t values;
    uint64_t bytes;
} Copier;

typedef struct {
    uintptr_t ptr;
    size_t len;
} StrKey;

static STATUS_MUST_USE Status copier_key(Copier *c, uintptr_t ptr, size_t len, Str *out) {
    StrKey *k = ARENA_NEW(c->books, StrKey);
    if (k == NULL) return run_machine_oom(c->r, c->pos);
    memset(k, 0, sizeof *k);
    k->ptr = ptr;
    k->len = len;
    Str s = {(const char *)k, sizeof *k};
    *out = s;
    return OK;
}

static STATUS_MUST_USE Status copier_string(Copier *c, Str s, Str *out) {
    Str key = {0};
    TRY(copier_key(c, (uintptr_t)s.ptr, s.len, &key));
    Value got;
    if (ValueMap_get(&c->seen_strings, key, &got)) {
        *out = got.as.string;
        return OK;
    }
    c->bytes += s.len;
    Str copy = {0};
    if (!arena_dup(c->r->ctx.values, s, &copy)) return run_budget_stop(&c->r->ctx, c->pos, BUDGET_KEPT);
    value_string(&got, copy);
    if (ValueMap_put(&c->seen_strings, key, got) != OK) return run_machine_oom(c->r, c->pos);
    *out = copy;
    return OK;
}

static STATUS_MUST_USE Status copier_table(Copier *c, Table *old, Table **out) {
    Str key = {0};
    TRY(copier_key(c, (uintptr_t)old, 0, &key));
    Value got;
    if (ValueMap_get(&c->seen_tables, key, &got)) {
        *out = got.as.table;
        return OK;
    }
    Table *t = ARENA_NEW(c->r->ctx.values, Table);
    if (t == NULL) return run_budget_stop(&c->r->ctx, c->pos, BUDGET_KEPT);
    t->kind = old->kind;
    t->list = ValueVec_make(c->r->ctx.values);
    t->map = ValueMap_make(c->r->ctx.values);
    value_table(&got, t);
    if (ValueMap_put(&c->seen_tables, key, got) != OK) return run_machine_oom(c->r, c->pos);
    Value ov;
    value_table(&ov, old);
    if (ValueVec_push(&c->todo, ov) != OK || ValueVec_push(&c->todo, got) != OK) return run_machine_oom(c->r, c->pos);
    *out = t;
    return OK;
}

// `v` in the fresh arena; a table's contents follow from `todo`.
static STATUS_MUST_USE Status copier_copy(Copier *c, Value *v) {
    c->values++;
    if (v->tag == VALUE_STRING) return copier_string(c, v->as.string, &v->as.string);
    if (v->tag == VALUE_TABLE) return copier_table(c, v->as.table, &v->as.table);
    return OK;
}

// Every pending table's contents copied, until the worklist is empty: a
// worklist, not recursion, so a list nested a million deep is copied in a loop.
static STATUS_MUST_USE Status copier_drain(Copier *c) {
    while (c->todo.len > 0) {
        Table *t = ValueVec_pop(&c->todo).as.table;
        Table *old = ValueVec_pop(&c->todo).as.table;
        // A list keeps exactly its capacity: a list literal half built when
        // an item's call yielded has reserved
        // room for the rest, and the growth rule's half again would grow a
        // kept list at every yield.
        if (old->list.cap > 0) {
            Value *items = ARENA_ARRAY(c->r->ctx.values, Value, old->list.cap);
            if (items == NULL) return run_budget_stop(&c->r->ctx, c->pos, BUDGET_KEPT);
            t->list.ptr = items;
            t->list.cap = old->list.cap;
        }
        for (size_t i = 0; i < old->list.len; i++) {
            Value item = at(old->list, i);
            TRY(copier_copy(c, &item));
            if (ValueVec_push(&t->list, item) != OK) return run_budget_stop(&c->r->ctx, c->pos, BUDGET_KEPT);
        }
        for (size_t i = 0; i < ValueMap_len(&old->map); i++) {
            Str k = {0};
            TRY(copier_string(c, ValueMap_key_at(&old->map, i), &k));
            Value x = ValueMap_value_at(&old->map, i);
            TRY(copier_copy(c, &x));
            if (ValueMap_put(&t->map, k, x) != OK) return run_budget_stop(&c->r->ctx, c->pos, BUDGET_KEPT);
        }
    }
    return OK;
}

SLICE_TYPE(TableSlots, Table *);

// Replaces the run's values arena with a fresh one holding only what `slots`
// and the attempts' answers `outs` reach, with the run's consts. Each is
// updated in place to its copy. A step for each value and each 16 bytes of
// string; past the kept budget the run stops.
// DRAGON: use after free. The old arena goes back to the system here, so
// any pointer into it held outside these roots dangles: a builtin's buffer,
// the `..` chain's tail, a stop's message made before the yield.
// REMEDY: the VM yields only between ops, when the stack, the operands, the
// attempts' answers and the consts are all it holds; the tail is cleared
// below, and a stop the copy makes has its message in the fresh arena.
static STATUS_MUST_USE Status collect_run(Run *r, Pos pos, Values stack, Values operands, TableSlots outs) {
    r->collections++;
    Arena *values = r->ctx.values;
    Arena old = *values;
    *values = arena_make(old.limit);
    Arena books = arena_make(r->ctx.budgets.machine_bytes);
    Copier c;
    memset(&c, 0, sizeof c);
    c.r = r;
    c.pos = pos;
    c.books = &books;
    c.seen_tables = ValueMap_make(&books);
    c.seen_strings = ValueMap_make(&books);
    c.todo = ValueVec_make(&books);
    // The effects go first, since the run reports them however it ends;
    // should even they not fit, the old arena is put back and the run stops
    // with what it had.
    if (r->store != NULL) {
        Effects kept = Effects_make(values);
        bool fits = Effects_reserve(&kept, r->store->effects.len) == OK;
        for (size_t i = 0; fits && i < r->store->effects.len; i++) {
            Effect e = at(r->store->effects, i);
            c.bytes += e.path.len;
            fits = arena_dup(values, e.path, &e.path) && Effects_push(&kept, e) == OK;
        }
        if (!fits) {
            arena_deinit(&books);
            arena_deinit(values);
            *values = old;
            return run_budget_stop(&r->ctx, pos, BUDGET_KEPT);
        }
        r->store->effects = kept;
        r->store->scan_buf.ptr = NULL;
        r->store->scan_buf.len = 0;
    }
    Status s = OK;
    for (size_t i = 0; s == OK && i < stack.len; i++) s = copier_copy(&c, &at(stack, i));
    for (size_t i = 0; s == OK && i < operands.len; i++) s = copier_copy(&c, &at(operands, i));
    for (size_t i = 0; s == OK && i < outs.len; i++) {
        if (at(outs, i) != NULL) s = copier_table(&c, at(outs, i), &at(outs, i));
    }
    Value *consts = NULL;
    if (s == OK) {
        consts = ARENA_ARRAY(values, Value, r->consts_len);
        if (consts == NULL) s = run_budget_stop(&r->ctx, pos, BUDGET_KEPT);
    }
    if (s == OK) {
        Values from = {r->consts, r->consts_len};
        Values to = {consts, r->consts_len};
        for (size_t i = 0; s == OK && i < from.len; i++) {
            at(to, i) = at(from, i);
            // An unset const is nil, and nil is no value to count.
            if (at(to, i).tag != VALUE_NIL) s = copier_copy(&c, &at(to, i));
        }
        r->consts = consts;
    }
    if (s == OK) s = copier_drain(&c);
    arena_deinit(&books);
    // A stop's message, made in the fresh arena, outlives the old one.
    arena_deinit(&old);
    r->tail_ptr = NULL;
    r->tail_used = 0;
    r->tail_cap = 0;
    TRY(s);
    TRY(run_charge(r, pos, c.values + (c.bytes + BUILTIN_BYTES_PER_STEP - 1) / BUILTIN_BYTES_PER_STEP));
    if (values->held > r->ctx.budgets.kept_bytes) return run_budget_stop(&r->ctx, pos, BUDGET_KEPT);
    return OK;
}

// ── the machine ──

typedef struct {
    const CodeFunc *func;
    // Where the caller resumes.
    uint32_t ret_pc;
    // Its slots in the run's stack.
    size_t base;
    // The walk's depth where its body starts (the walker's `Run.walk`).
    uint32_t walk;
    // The operand stack's height below its call.
    size_t sp;
    // The caller's function name, put back when it ends.
    Str caller;
    // The answer table, when an `attempt` called this frame; NULL otherwise.
    Table *out;
    // The `attempt`'s place.
    Pos at;
} VmFrame;

VEC_DEFINE(VmFrameVec, VmFrame)

typedef struct {
    Run *r;
    const Code *code;
    ValueVec operands;
    VmFrameVec frames;
    uint32_t pc;
    // The top frame's `walk`, kept here for every op's enters.
    uint32_t walk;
} Machine;

static VmFrame *vm_frame(Machine *m) {
    return &at(m->frames, m->frames.len - 1);
}

static Value *vm_slot(Machine *m, uint32_t s) {
    return &at(m->r->stack, m->r->base + s);
}

static Value *vm_top(Machine *m) {
    return &at(m->operands, m->operands.len - 1);
}

static Value vm_pop(Machine *m) {
    return ValueVec_pop(&m->operands);
}

static STATUS_MUST_USE Status vm_push(Machine *m, Pos pos, const Value *v) {
    if (ValueVec_push(&m->operands, *v) != OK) return run_machine_oom(m->r, pos);
    return OK;
}

static STATUS_MUST_USE Status vm_push_nil(Machine *m, Pos pos) {
    Value v;
    value_nil(&v);
    return vm_push(m, pos, &v);
}

// The top `n` operands, as a slice.
static ConstValues vm_args(Machine *m, size_t n) {
    ConstValues all = {m->operands.ptr, m->operands.len};
    ConstValues top = {all.ptr, 0};
    if (n > 0) top = SLICE_SUB(ConstValues, all, all.len - n, all.len);
    return top;
}

// The walker's enters before an op's work: a step each, the steps budget and
// then the depth budget checked at each node; all at once when the run fits.
static STATUS_MUST_USE Status vm_enters(Machine *m, const VmOp *op) {
    Run *r = m->r;
    uint32_t walk = m->walk;
    const Budgets *b = &r->ctx.budgets;
    if (op->enters <= run_sat_sub(b->steps, r->steps) && (uint64_t)walk + op->deepest < b->depth) {
        r->steps += op->enters;
        return OK;
    }
    for (uint32_t k = op->enter; k < op->enter + op->enters; k++) {
        const VmEnter *e = &at(m->code->enters, k);
        r->steps++;
        if (r->steps > b->steps) return run_budget_stop(&r->ctx, e->pos, BUDGET_STEPS);
        if ((uint64_t)walk + e->depth >= b->depth) return run_budget_stop(&r->ctx, e->pos, BUDGET_DEPTH);
    }
    return OK;
}

// A frame for `func`, its `argc` arguments taken off the operand stack. Its
// walk starts `op->c` deeper than the caller's frame's.
static STATUS_MUST_USE Status vm_enter_frame(Machine *m, const VmOp *op, const CodeFunc *func, uint32_t argc, Table *out) {
    Run *r = m->r;
    size_t base = r->stack.len;
    if (ValueVec_reserve(&r->stack, func->frame) != OK) return run_machine_oom(r, op->pos);
    Value nil;
    value_nil(&nil);
    for (uint32_t k = 0; k < func->frame; k++) {
        if (ValueVec_push(&r->stack, nil) != OK) return run_machine_oom(r, op->pos);
    }
    ConstValues args = vm_args(m, argc);
    for (uint32_t k = 0; k < argc; k++) at(r->stack, base + k) = at(args, k);
    m->operands.len -= argc;
    if (ValueVec_reserve(&m->operands, (size_t)func->operands + 1) != OK) return run_machine_oom(r, op->pos);
    uint32_t caller_walk = vm_frame(m)->walk;
    Call call = {op->pos.line, op->pos.column, r->ctx.function};
    if (CallVec_push(&r->ctx.calls, call) != OK) return run_machine_oom(r, op->pos);
    VmFrame f = {func, m->pc, base, caller_walk + op->c, m->operands.len, r->ctx.function, out, op->pos};
    if (VmFrameVec_push(&m->frames, f) != OK) return run_machine_oom(r, op->pos);
    r->base = base;
    r->ctx.function = func->name;
    r->depth++;
    m->pc = func->entry;
    m->walk = vm_frame(m)->walk;
    return OK;
}

// Ends the top frame with `v`; `*more` false when it was the script's top
// level.
static STATUS_MUST_USE Status vm_leave(Machine *m, const Value *v, bool *more) {
    Run *r = m->r;
    VmFrame f = VmFrameVec_pop(&m->frames);
    *more = m->frames.len > 0;
    if (!*more) return OK;
    r->stack.len = f.base;
    r->base = vm_frame(m)->base;
    m->walk = vm_frame(m)->walk;
    r->ctx.function = f.caller;
    r->depth--;
    r->ctx.calls.len--;
    m->operands.len = f.sp;
    m->pc = f.ret_pc;
    if (f.out == NULL) return vm_push(m, f.at, v);
    Value answer;
    TRY(run_attempt_returned(r, f.at, f.out, v, &answer));
    return vm_push(m, f.at, &answer);
}

// Unwinds the current stop to the innermost `attempt` frame, unless it is a
// budget's. `*caught` says whether the run goes on.
static STATUS_MUST_USE Status vm_caught(Machine *m, bool *caught) {
    Run *r = m->r;
    *caught = false;
    Stop s = r->ctx.stop;
    if (s.cls == CLASS_BUDGET) return OK;
    size_t i = m->frames.len;
    while (i > 1) {
        i--;
        VmFrame f = at(m->frames, i);
        if (f.out == NULL) continue;
        m->frames.len = i;
        r->stack.len = f.base;
        r->base = vm_frame(m)->base;
        m->walk = vm_frame(m)->walk;
        r->ctx.function = f.caller;
        r->depth = (uint32_t)(i - 1);
        m->operands.len = f.sp;
        m->pc = f.ret_pc;
        r->ctx.stopped = false;
        Value answer;
        TRY(run_attempt_failed(r, f.at, f.out, &s, &answer));
        TRY(vm_push(m, f.at, &answer));
        // Cut only after the answer, which is made from the stop's whole trail.
        r->ctx.calls.len = i - 1;
        *caught = true;
        return OK;
    }
    return OK;
}

// The `yield()` op: every value the machine holds is a root.
static STATUS_MUST_USE Status vm_yield(Machine *m, Pos pos) {
    Run *r = m->r;
    Table **outs_buf = ARENA_ARRAY(r->machine, Table *, m->frames.len);
    if (outs_buf == NULL) return run_machine_oom(r, pos);
    TableSlots outs = {outs_buf, m->frames.len};
    for (size_t i = 0; i < outs.len; i++) at(outs, i) = at(m->frames, i).out;
    Values stack = {r->stack.ptr, r->stack.len};
    Values operands = {m->operands.ptr, m->operands.len};
    Status s = collect_run(r, pos, stack, operands, outs);
    for (size_t i = 0; i < outs.len; i++) at(m->frames, i).out = at(outs, i);
    arena_free(r->machine, outs_buf, outs.len * sizeof(Table *));
    return s;
}

static Str vm_for_what(uint32_t a) {
    if (a == 0) return S("a `for`'s start");
    if (a == 1) return S("a `for`'s end");
    return S("a `for`'s step");
}

static STATUS_MUST_USE Status vm_binary(Machine *m, const VmOp *op) {
    Run *r = m->r;
    Value rv = vm_pop(m);
    Value lv = vm_pop(m);
    BinOp bop = (BinOp)op->a;
    Value out;
    switch (bop) {
    case BINOP_ADD:
    case BINOP_SUB:
    case BINOP_MUL:
    case BINOP_IDIV:
    case BINOP_MOD: TRY(run_arith(r, op->pos, bop, &lv, &rv, &out)); break;
    case BINOP_EQ:
    case BINOP_NE: {
        bool eq = false;
        TRY(run_equal(r, op->pos, &lv, &rv, &eq));
        value_bool(&out, eq == (bop == BINOP_EQ));
        break;
    }
    case BINOP_EQ_NIL:
    case BINOP_NE_NIL: {
        bool both = lv.tag == VALUE_NIL && rv.tag == VALUE_NIL;
        value_bool(&out, both == (bop == BINOP_EQ_NIL));
        break;
    }
    default: TRY(run_compare(r, op->pos, bop, &lv, &rv, &out)); break;
    }
    return vm_push(m, op->pos, &out);
}

static STATUS_MUST_USE Status vm_print(Machine *m, const VmOp *op) {
    Run *r = m->r;
    ConstValues vals = vm_args(m, op->a);
    Positions places = {m->code->positions.ptr, m->code->positions.len};
    for (size_t i = 0; i < vals.len; i++) {
        Pos place = at(places, op->b + i);
        if (i > 0) TRY(run_write(r, op->pos, S("\t")));
        IntText t;
        Str text = {0};
        TRY(builtin_scalar_text(r, place, &at(vals, i), &t, S("print"), &text));
        TRY(run_write(r, place, text));
    }
    TRY(run_write(r, op->pos, S("\n")));
    m->operands.len -= op->a;
    return vm_push_nil(m, op->pos);
}

// Runs one op; `*done` set when the top level ended.
static STATUS_MUST_USE Status vm_op(Machine *m, const VmOp *op, bool *done) {
    Run *r = m->r;
    const Code *code = m->code;
    switch (op->tag) {
    case OP_NOP: return OK;
    case OP_PUSH_NIL: return vm_push_nil(m, op->pos);
    case OP_PUSH_BOOL: {
        Value v;
        value_bool(&v, op->a != 0);
        return vm_push(m, op->pos, &v);
    }
    case OP_PUSH_INT: {
        Value v;
        value_int(&v, at(code->ints, op->a));
        return vm_push(m, op->pos, &v);
    }
    case OP_PUSH_STRING: {
        Value v;
        value_string(&v, at(code->strings, op->a));
        return vm_push(m, op->pos, &v);
    }
    case OP_PUSH_LOCAL: {
        Value v = *vm_slot(m, op->a);
        return vm_push(m, op->pos, &v);
    }
    case OP_PUSH_CONST: {
        // Set before it is read: the parser shows a const only below its line.
        Values consts = {r->consts, r->consts_len};
        Value v = at(consts, op->a);
        return vm_push(m, op->pos, &v);
    }
    case OP_POP:
        (void)vm_pop(m);
        return OK;
    case OP_STORE_LOCAL:
        *vm_slot(m, op->a) = vm_pop(m);
        return OK;
    case OP_STORE_CONST: {
        Value v = vm_pop(m);
        *vm_slot(m, op->a) = v;
        Values consts = {r->consts, r->consts_len};
        at(consts, op->b) = v;
        return OK;
    }
    case OP_INDEX_GET: {
        Value key = vm_pop(m);
        Value obj = vm_pop(m);
        Value v;
        TRY(run_get_index(r, op->pos, &obj, &key, &v));
        return vm_push(m, op->pos, &v);
    }
    case OP_INDEX_SET: {
        Value key = vm_pop(m);
        Value obj = vm_pop(m);
        Value v = vm_pop(m);
        return run_set_index(r, op->pos, &obj, &key, &v);
    }
    case OP_NEW_TABLE:
    case OP_LIST_BEGIN:
    case OP_MAP_BEGIN: {
        Table *t = NULL;
        TRY(run_new_table(r, op->pos, &t));
        if (op->tag == OP_LIST_BEGIN) {
            if (ValueVec_reserve(&t->list, op->a) != OK) return run_values_oom(r, op->pos);
            t->kind = TABLE_LIST;
        }
        if (op->tag == OP_MAP_BEGIN) t->kind = TABLE_MAP;
        Value v;
        value_table(&v, t);
        return vm_push(m, op->pos, &v);
    }
    case OP_LIST_ITEM: {
        Value v = vm_pop(m);
        if (v.tag == VALUE_NIL) return run_fail(&r->ctx, op->pos, CLASS_TYPE, S("a list cannot hold nil"));
        return run_list_push(r, op->pos, vm_top(m)->as.table, &v);
    }
    case OP_MAP_ENTRY: {
        Value v = vm_pop(m);
        if (v.tag == VALUE_NIL) return run_fail(&r->ctx, op->pos, CLASS_TYPE, S("a map cannot hold nil"));
        Pos place = at(code->positions, op->b);
        Str key = at(code->strings, op->a);
        TRY(run_charge(r, place, key.len));
        return run_map_put(r, place, vm_top(m)->as.table, key, &v);
    }
    case OP_UNARY: {
        Value v = vm_pop(m);
        Value out;
        TRY(run_unary(r, op->pos, (UnOp)op->a, &v, &out));
        return vm_push(m, op->pos, &out);
    }
    case OP_BINARY: return vm_binary(m, op);
    case OP_CONCAT: {
        ConstValues parts = vm_args(m, op->a);
        Positions all = {code->positions.ptr, code->positions.len};
        Positions places = SLICE_SUB(Positions, all, op->b, op->b + op->a);
        Value v;
        TRY(run_join_chain(r, op->pos, parts, places, &v));
        m->operands.len -= op->a;
        return vm_push(m, op->pos, &v);
    }
    case OP_JUMP:
        m->pc = op->a;
        return OK;
    case OP_JUMP_FALSE: {
        Value v = vm_pop(m);
        if (!value_truthy(&v)) m->pc = op->a;
        return OK;
    }
    case OP_AND_JUMP:
    case OP_OR_JUMP: {
        bool truthy = value_truthy(vm_top(m));
        if (truthy == (op->tag == OP_OR_JUMP)) {
            m->pc = op->a;
        } else {
            (void)vm_pop(m);
        }
        return OK;
    }
    case OP_BUILTIN: {
        ConstValues args = vm_args(m, op->b);
        Value v;
        TRY(builtins_call(r, op->pos, (Builtin)op->a, args, &v));
        m->operands.len -= op->b;
        return vm_push(m, op->pos, &v);
    }
    case OP_COLLECT:
        TRY(vm_yield(m, op->pos));
        return vm_push_nil(m, op->pos);
    case OP_PRINT: return vm_print(m, op);
    case OP_TOO_MANY: return builtins_too_many(r, op->pos, (Builtin)op->a);
    case OP_CALL_PREP:
        if (r->depth >= r->ctx.budgets.call_depth) return run_budget_stop(&r->ctx, op->pos, BUDGET_CALL_DEPTH);
        return OK;
    case OP_CALL: return vm_enter_frame(m, op, &at(code->functions, op->a), op->b, NULL);
    case OP_ATTEMPT: {
        Table *out = NULL;
        TRY(run_attempt_table(r, op->pos, &out));
        if (r->depth >= r->ctx.budgets.call_depth) return run_budget_stop(&r->ctx, op->pos, BUDGET_CALL_DEPTH);
        return vm_enter_frame(m, op, &at(code->functions, op->a), op->b, out);
    }
    case OP_RET:
    case OP_RET_NIL: {
        Value v;
        value_nil(&v);
        if (op->tag == OP_RET) v = vm_pop(m);
        bool more = false;
        TRY(vm_leave(m, &v, &more));
        *done = !more;
        return OK;
    }
    case OP_FOR_INT: return run_int_of(r, op->pos, vm_top(m), vm_for_what(op->a));
    case OP_FOR_NUM_INIT: {
        int64_t step = 1;
        if (op->b != 0) step = vm_pop(m).as.integer;
        int64_t to = vm_pop(m).as.integer;
        int64_t from = vm_pop(m).as.integer;
        if (step == 0) return run_fail(&r->ctx, op->pos, CLASS_ARGUMENT, S("a `for` with a step of 0 never ends"));
        value_int(vm_slot(m, op->a), from);
        value_int(vm_slot(m, op->a + 1), to);
        value_int(vm_slot(m, op->a + 2), step);
        return OK;
    }
    case OP_FOR_NUM_ITER: {
        int64_t i = vm_slot(m, op->a)->as.integer;
        int64_t to = vm_slot(m, op->a + 1)->as.integer;
        int64_t step = vm_slot(m, op->a + 2)->as.integer;
        if (!(step > 0 ? i <= to : i >= to)) {
            m->pc = op->b;
            return OK;
        }
        TRY(run_charge(r, op->pos, 1));
        value_int(vm_slot(m, op->c), i);
        return OK;
    }
    case OP_FOR_NUM_NEXT: {
        int64_t i = vm_slot(m, op->a)->as.integer;
        int64_t step = vm_slot(m, op->a + 2)->as.integer;
        int64_t next = 0;
        // A step that would overflow ends the loop.
        if (int_add(i, step, &next)) {
            value_int(vm_slot(m, op->a), next);
            m->pc = op->b;
        } else {
            m->pc = op->c;
        }
        return OK;
    }
    case OP_FOR_LIST_INIT: {
        Value l = vm_pop(m);
        Table *t = NULL;
        TRY(run_list_to_walk(r, op->pos, &l, &t));
        if (t == NULL) {
            m->pc = op->b;
            return OK;
        }
        size_t n = t->list.len;
        TRY(run_charge(r, at(code->positions, op->c), n));
        value_table(vm_slot(m, op->a), t);
        value_int(vm_slot(m, op->a + 1), (int64_t)n);
        value_int(vm_slot(m, op->a + 2), 0);
        return OK;
    }
    case OP_FOR_LIST_ITER: {
        int64_t i = vm_slot(m, op->a + 2)->as.integer;
        Table *t = vm_slot(m, op->a)->as.table;
        // A list `pop` shortened ends the loop at its new end.
        if (i >= vm_slot(m, op->a + 1)->as.integer || (uint64_t)i >= t->list.len) {
            m->pc = op->b;
            return OK;
        }
        TRY(run_charge(r, op->pos, 1));
        *vm_slot(m, op->c) = at(t->list, (size_t)i);
        value_int(vm_slot(m, op->a + 2), i + 1);
        return OK;
    }
    }
    return OK;
}

// Runs ops until the top level ends or a stop no `attempt` catches.
static STATUS_MUST_USE Status vm_run(Machine *m) {
    for (;;) {
        const VmOp *op = &at(m->code->ops, m->pc);
        m->pc++;
        bool done = false;
        Status s = op->enters != 0 ? vm_enters(m, op) : OK;
        if (s == OK) s = vm_op(m, op, &done);
        if (s == OK) {
            if (done) return OK;
            continue;
        }
        if (!m->r->ctx.stopped) TRY(run_values_oom(m->r, op->pos));
        bool caught = false;
        TRY(vm_caught(m, &caught));
        if (!caught) return STOP;
    }
}

// The script's top level run whole: the consts first, then the main frame
// at the stack's foot.
static STATUS_MUST_USE Status vm_main(Run *r, const Code *code) {
    Pos at_main = r->program->main.pos;
    r->consts_len = r->program->const_names.len;
    r->consts = ARENA_ARRAY(r->ctx.values, Value, r->consts_len);
    if (r->consts == NULL) return run_values_oom(r, at_main);
    Values consts = {r->consts, r->consts_len};
    for (size_t k = 0; k < consts.len; k++) value_nil(&at(consts, k));
    Value nil;
    value_nil(&nil);
    for (uint32_t k = 0; k < code->main.frame; k++) {
        if (ValueVec_push(&r->stack, nil) != OK) return run_machine_oom(r, at_main);
    }
    r->base = 0;
    Machine m;
    memset(&m, 0, sizeof m);
    m.r = r;
    m.code = code;
    m.operands = ValueVec_make(r->machine);
    m.frames = VmFrameVec_make(r->machine);
    m.pc = code->main.entry;
    if (ValueVec_reserve(&m.operands, (size_t)code->main.operands + 1) != OK) return run_machine_oom(r, at_main);
    VmFrame top = {&code->main, 0, 0, 0, 0, S(""), NULL, at_main};
    if (VmFrameVec_push(&m.frames, top) != OK) return run_machine_oom(r, at_main);
    return vm_run(&m);
}
