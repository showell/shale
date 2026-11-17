// run.h: a run's state and the semantics every op and builtin shares: the steps
// and the budgets, the output, the stack of frames' slots, indexing,
// arithmetic, comparison, `..` chains and `attempt`'s answers. Its messages
// are in the frozen surface (docs/SURFACE.txt), and a script sees its step
// charges through the budgets, so neither changes freely.

#ifndef SHALE_RUN_H
#define SHALE_RUN_H

#include "../foundation/err.h"
#include "cli.h"
#include "regex.h"
#include "compile.h"
#include "value.h"

// The most arguments any builtin but `print` takes.
enum { BUILTINS_MOST_ARGS = 4 };

SLICE_TYPE(ConstValues, const Value);

// RUN_COLD keeps a function out of the code that calls it, and a call of it
// off the hot path's line: a stop's path, an op's rare work, whose locals
// and saved registers would otherwise weigh on every op. RUN_HOT makes GCC
// inline a small one that `at`'s trap, inlined into it, makes look too big
// to. TCC takes neither, and needs neither.
#if defined(__GNUC__) && !defined(__TINYC__)
#define RUN_COLD __attribute__((cold))
#define RUN_HOT __attribute__((always_inline))
#else
#define RUN_COLD
#define RUN_HOT
#endif

typedef struct Run Run;

// The store a run's paths reach, its hook and its effects log, as
// builtins_store.c defines it; NULL for a run with no store.
typedef struct RunStore RunStore;

// The core's one way out to its host: `host(name, ...)` calls `call` with the
// name and the rest of its arguments. NULL `call` is no host.
typedef struct {
    void *ctx;
    Status (*call)(void *ctx, Run *r, Pos pos, Str name, ConstValues args, Value *out);
} RunHost;

typedef struct Run {
    // The stop's place and the calls in progress; `ctx.values` is the values'
    // arena, `ctx.function` the function the run is in.
    RunCtx ctx;
    // The frames' slots, the operands and the frames, under the machine budget.
    Arena *machine;
    const Program *program;
    uint64_t steps;
    // Calls of script functions in progress.
    uint32_t depth;
    int64_t now;
    // What `print` wrote, at most `out_cap` bytes, in an arena of its own.
    Buf out;
    size_t out_cap;
    const Arg *args;
    size_t args_len;
    uint64_t reads;
    uint64_t read_bytes;
    uint64_t changes;
    uint64_t change_bytes;
    uint32_t host_runs;
    bool tried_change;
    uint64_t collections;
    // Every frame's slots, one stack, each call's frame on top; `base` is the
    // current frame's first.
    ValueVec stack;
    size_t base;
    // The top-level `const`s, each unset until its line has run.
    Value *consts;
    size_t consts_len;
    // The last string a `..` chain built: its buffer, how much of it the
    // string fills, and the buffer's size, so `s = s .. x` is linear.
    char *tail_ptr; // cstr: its size is `tail_cap`
    size_t tail_used;
    size_t tail_cap;
    // The patterns `re_find` and its kin compiled.
    RegexCache patterns;
    RunStore *store;
    RunHost host;
    // The host call running (`host("real", ...)`'s `real`), "" outside one:
    // a stop the host's code meets in a builtin's checks names it.
    Str host_call;
} Run;

// ── stops ──

// The text of a 64-bit integer in decimal.
typedef struct {
    char bytes[24];
} IntText;

static inline Str int_text(IntText *t, int64_t n) {
    Bytes room = {t->bytes, sizeof t->bytes};
    size_t k = room.len;
    uint64_t u = n < 0 ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
    do {
        k--;
        at(room, k) = (char)('0' + u % 10);
        u /= 10;
    } while (u > 0);
    if (n < 0) {
        k--;
        at(room, k) = '-';
    }
    return str_sub(((Str){t->bytes, sizeof t->bytes}), k, sizeof t->bytes);
}

// `n` in decimal in the values' arena, for a message; "" when refused, and the
// message that holds it is refused then too.
static inline Str run_num(Run *r, int64_t n) {
    IntText t;
    Str s = {0};
    if (!arena_dup(r->ctx.values, int_text(&t, n), &s)) return S("");
    return s;
}

static inline Str run_unum(Run *r, uint64_t n) {
    Buf b = buf_make(r->ctx.values);
    if (buf_u64(&b, n) != OK) return S("");
    return buf_view(&b);
}

// A stop whose message is `parts` joined, or `fallback` (the format text) when
// the arena refuses it. The pieces a literal leaves out are empty, so all 16
// are joined and no count can drop one.
static inline STATUS_MUST_USE Status run_fail_parts(Run *r, Pos pos, StopClass cls, Str fallback, StrPieces parts) {
    StrParts all = {parts.piece, sizeof parts.piece / sizeof(Str)};
    Msg m;
    msg_init(&m, r->ctx.values);
    for (size_t k = 0; k < all.len; k++) {
        if (at(all, k).len > 0) msg_str(&m, at(all, k));
    }
    return run_fail_built(&r->ctx, pos, cls, &m.b, m.s, fallback);
}

#define run_failf(r, pos, cls, fallback, ...) run_fail_parts((r), (pos), (cls), S(fallback), (StrPieces){{__VA_ARGS__}})

// A refused allocation in the values' arena: the arena budget.
static inline STATUS_MUST_USE Status run_values_oom(Run *r, Pos pos) {
    return run_oom(&r->ctx, pos);
}

// A refused allocation in the machine's memory: its budget.
static inline STATUS_MUST_USE Status run_machine_oom(Run *r, Pos pos) {
    return run_refused_stop(&r->ctx, pos, BUDGET_MACHINE);
}

static inline uint64_t run_sat_sub(uint64_t a, uint64_t b) {
    return a > b ? a - b : 0;
}

// Steps, charged; `n` for a builtin's work in proportion to what it touches.
// A charge past what is left stops one step past the budget, as a single step
// does: the run never did that work, so the count must not grow by the charge.
static inline RUN_COLD STATUS_MUST_USE Status run_charge_over(Run *r, Pos pos) {
    uint64_t budget = r->ctx.budgets.steps;
    r->steps = budget == UINT64_MAX ? budget : budget + 1;
    return run_budget_stop(&r->ctx, pos, BUDGET_STEPS);
}

static inline STATUS_MUST_USE Status run_charge(Run *r, Pos pos, uint64_t n) {
    if (n > run_sat_sub(r->ctx.budgets.steps, r->steps)) return run_charge_over(r, pos);
    r->steps += n;
    return OK;
}

// ── tables ──

static inline STATUS_MUST_USE Status run_new_table(Run *r, Pos pos, Table **out) {
    Table *t = ARENA_NEW(r->ctx.values, Table);
    if (t == NULL) return run_values_oom(r, pos);
    t->kind = TABLE_EMPTY;
    t->list = ValueVec_make(r->ctx.values);
    t->map = ValueMap_make(r->ctx.values);
    *out = t;
    return OK;
}

static inline STATUS_MUST_USE Status run_list_push(Run *r, Pos pos, Table *t, const Value *v) {
    if (ValueVec_push(&t->list, *v) != OK) return run_values_oom(r, pos);
    return OK;
}

static inline STATUS_MUST_USE Status run_map_put(Run *r, Pos pos, Table *t, Str key, const Value *v) {
    if (ValueMap_put(&t->map, key, *v) != OK) return run_values_oom(r, pos);
    return OK;
}

static inline STATUS_MUST_USE Status run_map_put_int(Run *r, Pos pos, Table *t, Str key, int64_t i) {
    Value v;
    value_int(&v, i);
    return run_map_put(r, pos, t, key, &v);
}

static inline STATUS_MUST_USE Status run_map_put_str(Run *r, Pos pos, Table *t, Str key, Str s) {
    Value v;
    value_string(&v, s);
    return run_map_put(r, pos, t, key, &v);
}

static inline STATUS_MUST_USE Status run_map_put_bool(Run *r, Pos pos, Table *t, Str key, bool b) {
    Value v;
    value_bool(&v, b);
    return run_map_put(r, pos, t, key, &v);
}

static inline STATUS_MUST_USE Status run_not_indexable(Run *r, Pos pos, const Value *obj) {
    return run_failf(r, pos, CLASS_TYPE, "only a list or a map can be indexed, not {s}", S("only a list or a map can be indexed, not "), value_type_name(obj));
}

static inline STATUS_MUST_USE Status run_bad_key(Run *r, Pos pos, const Value *key) {
    return run_failf(r, pos, CLASS_TYPE, "a key must be an integer (a list) or a string (a map), not {s}", S("a key must be an integer (a list) or a string (a map), not "), value_type_name(key));
}

// `obj[key] = v`.
static inline STATUS_MUST_USE Status run_set_index(Run *r, Pos pos, const Value *obj, const Value *key, const Value *v) {
    if (obj->tag != VALUE_TABLE) return run_not_indexable(r, pos, obj);
    Table *t = obj->as.table;
    if (key->tag == VALUE_INT) {
        int64_t k = key->as.integer;
        if (t->kind == TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("a map's keys are strings: this one is a map, indexed by an integer"));
        if (v->tag == VALUE_NIL) return run_fail(&r->ctx, pos, CLASS_TYPE, S("a list cannot hold nil"));
        size_t len = t->list.len;
        if (k >= 1 && (uint64_t)k <= len) {
            at(t->list, (size_t)(k - 1)) = *v;
            return OK;
        }
        if (k >= 1 && (uint64_t)k == (uint64_t)len + 1) {
            TRY(run_list_push(r, pos, t, v));
            t->kind = TABLE_LIST;
            return OK;
        }
        return run_failf(r, pos, CLASS_INDEX, "index {d} is outside the list (length {d}): only index {d} appends, and `push(l, x)` says so",
                         S("index "), run_num(r, k), S(" is outside the list (length "), run_unum(r, len), S("): only index "), run_unum(r, (uint64_t)len + 1), S(" appends, and `push(l, x)` says so"));
    }
    if (key->tag == VALUE_STRING) {
        Str k = key->as.string;
        if (t->kind == TABLE_LIST) return run_fail(&r->ctx, pos, CLASS_TYPE, S("a list's keys are integers: this one is a list, indexed by a string"));
        if (v->tag == VALUE_NIL) return run_fail(&r->ctx, pos, CLASS_TYPE, S("a map cannot hold nil (`m.k = nil` is an error: there is no remove)"));
        // A key is hashed and compared whole, so it costs a step a byte.
        TRY(run_charge(r, pos, k.len));
        TRY(run_map_put(r, pos, t, k, v));
        t->kind = TABLE_MAP;
        return OK;
    }
    return run_bad_key(r, pos, key);
}

// `obj[key]`.
static inline STATUS_MUST_USE Status run_get_index(Run *r, Pos pos, const Value *obj, const Value *key, Value *out) {
    if (obj->tag != VALUE_TABLE) return run_not_indexable(r, pos, obj);
    Table *t = obj->as.table;
    if (key->tag == VALUE_INT) {
        int64_t k = key->as.integer;
        if (t->kind == TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("a map's keys are strings: this one is a map, indexed by an integer"));
        size_t len = t->list.len;
        if (k < 1 || (uint64_t)k > len) {
            return run_failf(r, pos, CLASS_INDEX, "index {d} is outside the list (length {d})", S("index "), run_num(r, k), S(" is outside the list (length "), run_unum(r, len), S(")"));
        }
        *out = at(t->list, (size_t)(k - 1));
        return OK;
    }
    if (key->tag == VALUE_STRING) {
        Str k = key->as.string;
        if (t->kind == TABLE_LIST) return run_fail(&r->ctx, pos, CLASS_TYPE, S("a list's keys are integers: this one is a list, indexed by a string"));
        TRY(run_charge(r, pos, k.len));
        if (ValueMap_get(&t->map, k, out)) return OK;
        return run_failf(r, pos, CLASS_INDEX, "no key \"{s}\" in the map (`has_key` asks first)", S("no key \""), k, S("\" in the map (`has_key` asks first)"));
    }
    return run_bad_key(r, pos, key);
}

// The list of a `for x in`, `pos` being its expression's: NULL for a table
// still empty, whose loop runs no turn and charges nothing.
static inline STATUS_MUST_USE Status run_list_to_walk(Run *r, Pos pos, const Value *l, Table **out) {
    if (l->tag == VALUE_TABLE) {
        Table *t = l->as.table;
        if (t->kind == TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`for x in` a map: walk `keys(m)` instead"));
        *out = t->kind == TABLE_EMPTY ? NULL : t;
        return OK;
    }
    return run_failf(r, pos, CLASS_TYPE, "`for x in` takes a list, given {s}", S("`for x in` takes a list, given "), value_type_name(l));
}

// A numeric `for`'s start, end or step (`what`), `pos` its expression's.
static inline STATUS_MUST_USE Status run_int_of(Run *r, Pos pos, const Value *v, Str what) {
    if (v->tag == VALUE_INT) return OK;
    return run_failf(r, pos, CLASS_TYPE, "{s} must be an integer, not {s}", what, S(" must be an integer, not "), value_type_name(v));
}

// ── operators ──

static inline Str run_op_text(BinOp op) {
    switch (op) {
    case BINOP_ADD: return S("+");
    case BINOP_SUB: return S("-");
    case BINOP_MUL: return S("*");
    case BINOP_IDIV: return S("//");
    case BINOP_MOD: return S("%");
    case BINOP_CONCAT: return S("..");
    case BINOP_EQ:
    case BINOP_EQ_NIL: return S("==");
    case BINOP_NE:
    case BINOP_NE_NIL: return S("~=");
    case BINOP_LT: return S("<");
    case BINOP_LE: return S("<=");
    case BINOP_GT: return S(">");
    case BINOP_GE: return S(">=");
    case BINOP_AND: return S("and");
    case BINOP_OR: return S("or");
    }
    return S("");
}

static inline STATUS_MUST_USE Status run_unary(Run *r, Pos pos, UnOp op, const Value *v, Value *out) {
    switch (op) {
    case UNOP_NOT:
        value_bool(out, !value_truthy(v));
        return OK;
    case UNOP_NEG:
        if (v->tag != VALUE_INT) return run_failf(r, pos, CLASS_TYPE, "`-` takes an integer, not {s}", S("`-` takes an integer, not "), value_type_name(v));
        if (v->as.integer == INT64_MIN) return run_failf(r, pos, CLASS_ARITHMETIC, "negating {d} overflows 64 bits", S("negating "), run_num(r, v->as.integer), S(" overflows 64 bits"));
        value_int(out, -v->as.integer);
        return OK;
    case UNOP_LEN:
        if (v->tag == VALUE_STRING) {
            value_int(out, (int64_t)v->as.string.len);
            return OK;
        }
        if (v->tag == VALUE_TABLE) {
            if (v->as.table->kind == TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`#` of a map: use `count(m)`"));
            value_int(out, (int64_t)v->as.table->list.len);
            return OK;
        }
        return run_failf(r, pos, CLASS_TYPE, "`#` takes a string or a list, not {s}", S("`#` takes a string or a list, not "), value_type_name(v));
    }
    return OK;
}

// `a + b`, `a - b` and `a * b` on int64_t: false on overflow, `*out` then
// untouched. GCC's checked builtins are a flag test after the operation;
// TCC has none, so it takes the portable comparisons, and a division for
// `*`.
#if defined(__GNUC__) && !defined(__TINYC__)
#define RUN_INT_BUILTINS 1
#else
#define RUN_INT_BUILTINS 0
#endif

static inline bool int_add(int64_t a, int64_t b, int64_t *out) {
#if RUN_INT_BUILTINS
    int64_t sum = 0;
    if (__builtin_add_overflow(a, b, &sum)) return false;
    *out = sum;
#else
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    *out = a + b;
#endif
    return true;
}

static inline bool int_sub(int64_t a, int64_t b, int64_t *out) {
#if RUN_INT_BUILTINS
    int64_t diff = 0;
    if (__builtin_sub_overflow(a, b, &diff)) return false;
    *out = diff;
#else
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return false;
    *out = a - b;
#endif
    return true;
}

static inline bool int_mul(int64_t a, int64_t b, int64_t *out) {
#if RUN_INT_BUILTINS
    int64_t product = 0;
    if (__builtin_mul_overflow(a, b, &product)) return false;
    *out = product;
#else
    if (a > 0) {
        if (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a) return false;
    } else if (a < 0) {
        if (b > 0 ? a < INT64_MIN / b : b < INT64_MAX / a) return false;
    }
    *out = a * b;
#endif
    return true;
}

// `a op b` for an integer operator (`+`, `-`, `*`, `//`, `%`): false when it
// overflows or divides by zero, for run_arith to say which.
static inline RUN_HOT bool run_int_arith(BinOp op, int64_t a, int64_t b, int64_t *out) {
    int64_t got = 0;
    switch (op) {
    case BINOP_ADD: return int_add(a, b, out);
    case BINOP_SUB: return int_sub(a, b, out);
    case BINOP_MUL: return int_mul(a, b, out);
    case BINOP_IDIV:
        if (b == 0 || (a == INT64_MIN && b == -1)) return false;
        // `//` floors: the quotient rounded toward minus infinity.
        got = a / b;
        if (a % b != 0 && ((a < 0) != (b < 0))) got--;
        break;
    default:
        if (b == 0) return false;
        if (b == -1) {
            got = 0;
            break;
        }
        // `%` takes the divisor's sign, as the floor of `//` leaves it.
        got = a % b;
        if (got != 0 && ((got < 0) != (b < 0))) got += b;
        break;
    }
    *out = got;
    return true;
}

static inline Str run_number_hint(const Value *v) {
    return v->tag == VALUE_STRING ? S(" (`tonumber(s)` reads one from a string)") : S("");
}

static inline STATUS_MUST_USE Status run_arith(Run *r, Pos pos, BinOp op, const Value *l, const Value *rv, Value *out) {
    if (l->tag != VALUE_INT) return run_failf(r, pos, CLASS_TYPE, "`{s}` takes integers, and its left is {s}{s}", S("`"), run_op_text(op), S("` takes integers, and its left is "), value_type_name(l), run_number_hint(l));
    if (rv->tag != VALUE_INT) return run_failf(r, pos, CLASS_TYPE, "`{s}` takes integers, and its right is {s}{s}", S("`"), run_op_text(op), S("` takes integers, and its right is "), value_type_name(rv), run_number_hint(rv));
    int64_t a = l->as.integer;
    int64_t b = rv->as.integer;
    int64_t got = 0;
    if (run_int_arith(op, a, b, &got)) {
        value_int(out, got);
        return OK;
    }
    // Only `//` and `%` fail on a zero right: `+`, `-` and `*` of 0 never
    // overflow.
    if (b == 0 && op == BINOP_IDIV) return run_fail(&r->ctx, pos, CLASS_ARITHMETIC, S("integer division by zero"));
    if (b == 0) return run_fail(&r->ctx, pos, CLASS_ARITHMETIC, S("`%` by zero"));
    return run_failf(r, pos, CLASS_ARITHMETIC, "{d} {s} {d} overflows 64 bits", run_num(r, a), S(" "), run_op_text(op), S(" "), run_num(r, b), S(" overflows 64 bits"));
}

// The byte order of two strings, a prefix first: -1, 0 or 1.
static inline int str_order(Str a, Str b) {
    size_t n = a.len < b.len ? a.len : b.len;
    int c = n == 0 ? 0 : memcmp(a.ptr, b.ptr, n);
    if (c != 0) return c < 0 ? -1 : 1;
    if (a.len == b.len) return 0;
    return a.len < b.len ? -1 : 1;
}

static inline STATUS_MUST_USE Status run_cannot_compare(Run *r, Pos pos, BinOp op, const Value *l, const Value *rv) {
    return run_failf(r, pos, CLASS_TYPE, "`{s}` compares {s} with {s}", S("`"), run_op_text(op), S("` compares "), value_type_name(l), S(" with "), value_type_name(rv));
}

static inline STATUS_MUST_USE Status run_compare(Run *r, Pos pos, BinOp op, const Value *l, const Value *rv, Value *out) {
    int order = 0;
    if (l->tag == VALUE_INT && rv->tag == VALUE_INT) {
        order = l->as.integer < rv->as.integer ? -1 : (l->as.integer > rv->as.integer ? 1 : 0);
    } else if (l->tag == VALUE_STRING && rv->tag == VALUE_STRING) {
        Str a = l->as.string;
        Str b = rv->as.string;
        TRY(run_charge(r, pos, a.len < b.len ? a.len : b.len));
        order = str_order(a, b);
    } else {
        return run_cannot_compare(r, pos, op, l, rv);
    }
    bool got = false;
    switch (op) {
    case BINOP_LT: got = order < 0; break;
    case BINOP_LE: got = order <= 0; break;
    case BINOP_GT: got = order > 0; break;
    default: got = order >= 0; break;
    }
    value_bool(out, got);
    return OK;
}

static inline STATUS_MUST_USE Status run_equal(Run *r, Pos pos, const Value *l, const Value *rv, bool *out) {
    if (l->tag == VALUE_TABLE || rv->tag == VALUE_TABLE) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`==` on a list or a map is not in this language"));
    if (l->tag != rv->tag) {
        *out = false;
        return OK;
    }
    switch (l->tag) {
    case VALUE_NIL: *out = true; return OK;
    case VALUE_BOOL: *out = l->as.boolean == rv->as.boolean; return OK;
    case VALUE_INT: *out = l->as.integer == rv->as.integer; return OK;
    default: break;
    }
    Str a = l->as.string;
    Str b = rv->as.string;
    TRY(run_charge(r, pos, a.len < b.len ? a.len : b.len));
    *out = str_eq(a, b);
    return OK;
}

// ── `..` chains ──

static inline size_t run_text_len(const Value *v) {
    if (v->tag == VALUE_STRING) return v->as.string.len;
    IntText t;
    return int_text(&t, v->as.integer).len;
}

static inline STATUS_MUST_USE Status run_part_len(Run *r, Pos pos, const Value *v, size_t *out) {
    if (v->tag == VALUE_STRING || v->tag == VALUE_INT) {
        *out = run_text_len(v);
        return OK;
    }
    return run_failf(r, pos, CLASS_TYPE, "`..` takes strings and integers, not {s}", S("`..` takes strings and integers, not "), value_type_name(v));
}

SLICE_TYPE(Positions, const Pos);

// A `..` chain's parts, evaluated, joined: `ops[k]` is the `..` part k is
// checked at. When the first part is the whole string the last chain built and
// its buffer has room, the rest is written after it in place, so `s = s .. x`
// in a loop is linear in time and arena. An outgrown buffer is copied to one
// twice the size. No string is ever changed: the room is past the end of every
// slice of the buffer. Each byte written is a step.
static inline STATUS_MUST_USE Status run_join_chain(Run *r, Pos pos, ConstValues parts, Positions ops, Value *out) {
    size_t n = parts.len;
    // Checked as the nested joins would have been: the innermost pair, then
    // outward, each at its own `..`.
    size_t total = 0;
    size_t k = n;
    while (k > 0) {
        k--;
        size_t i = k == n - 1 ? n - 2 : (k == n - 2 ? n - 1 : k);
        size_t len = 0;
        TRY(run_part_len(r, at(ops, i), &at(parts, i), &len));
        total += len;
    }
    const Value *head = &at(parts, 0);
    Str first = head->tag == VALUE_STRING ? head->as.string : S("");
    bool same = head->tag == VALUE_STRING && first.len > 0 && first.ptr == r->tail_ptr && first.len == r->tail_used;
    bool in_place = same && total <= r->tail_cap;
    char *buf = r->tail_ptr;
    if (!in_place) {
        size_t cap = total;
        if (same) cap = 2 * total > 64 ? 2 * total : 64;
        buf = arena_alloc(r->ctx.values, cap, 1);
        if (buf == NULL) return run_values_oom(r, pos);
        r->tail_cap = cap;
    }
    size_t len = in_place ? first.len : 0;
    TRY(run_charge(r, pos, total - len));
    Bytes room = {buf, total};
    for (size_t p = in_place ? 1 : 0; p < n; p++) {
        const Value *v = &at(parts, p);
        IntText t;
        Str piece = v->tag == VALUE_STRING ? v->as.string : int_text(&t, v->as.integer);
        if (piece.len > 0) memcpy(&at(room, len), piece.ptr, piece.len);
        len += piece.len;
    }
    r->tail_ptr = buf;
    r->tail_used = total;
    Str s = {buf, total};
    value_string(out, s);
    return OK;
}

// ── output ──

// `bytes` written to the output, a step a byte; past the output budget, what
// fits is written and the run stops.
static inline STATUS_MUST_USE Status run_write(Run *r, Pos pos, Str bytes) {
    TRY(run_charge(r, pos, bytes.len));
    size_t room = r->out_cap - r->out.len;
    if (bytes.len > room) {
        if (buf_str(&r->out, str_sub(bytes, 0, room)) != OK) return run_values_oom(r, pos);
        return run_budget_stop(&r->ctx, pos, BUDGET_OUTPUT);
    }
    if (buf_str(&r->out, bytes) != OK) return run_values_oom(r, pos);
    return OK;
}

// ── `attempt` ──

// `attempt`'s answer, made before the call, so its arena comes first.
static inline STATUS_MUST_USE Status run_attempt_table(Run *r, Pos pos, Table **out) {
    TRY(run_new_table(r, pos, out));
    (*out)->kind = TABLE_MAP;
    return OK;
}

// `{ok = false, error, class, line, column, in_function, trail}` for a stop the
// call made, its trail innermost first, as the report says it.
static inline STATUS_MUST_USE Status run_attempt_failed(Run *r, Pos pos, Table *out, const Stop *s, Value *v) {
    TRY(run_map_put_bool(r, pos, out, S("ok"), false));
    TRY(run_map_put_str(r, pos, out, S("error"), s->message));
    TRY(run_map_put_str(r, pos, out, S("class"), stop_class_name(s->cls)));
    TRY(run_map_put_int(r, pos, out, S("line"), s->line));
    TRY(run_map_put_int(r, pos, out, S("column"), s->column));
    TRY(run_map_put_str(r, pos, out, S("in_function"), s->function));
    Table *trail = NULL;
    TRY(run_new_table(r, pos, &trail));
    if (ValueVec_reserve(&trail->list, s->trail.len) != OK) return run_values_oom(r, pos);
    for (size_t k = s->trail.len; k > 0; k--) {
        const Call c = at(s->trail, k - 1);
        Table *site = NULL;
        TRY(run_new_table(r, pos, &site));
        site->kind = TABLE_MAP;
        TRY(run_map_put_str(r, pos, site, S("in_function"), c.function));
        TRY(run_map_put_int(r, pos, site, S("line"), c.line));
        TRY(run_map_put_int(r, pos, site, S("column"), c.column));
        Value sv;
        value_table(&sv, site);
        TRY(run_list_push(r, pos, trail, &sv));
    }
    if (s->trail.len > 0) trail->kind = TABLE_LIST;
    Value tv;
    value_table(&tv, trail);
    TRY(run_map_put(r, pos, out, S("trail"), &tv));
    value_table(v, out);
    return OK;
}

// `{ok = true, value = got}`, no `value` for nil.
static inline STATUS_MUST_USE Status run_attempt_returned(Run *r, Pos pos, Table *out, const Value *got, Value *v) {
    TRY(run_map_put_bool(r, pos, out, S("ok"), true));
    if (got->tag != VALUE_NIL) TRY(run_map_put(r, pos, out, S("value"), got));
    value_table(v, out);
    return OK;
}

#endif
