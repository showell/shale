// builtins.c: the builtins that need no host, but text's and regular
// expressions' (builtins_text.c): the argument helpers every builtin shares,
// lists and maps, numbers, time and the run, and formatting. Their messages
// are in the frozen surface (docs/SURFACE.txt), and a script sees their step
// charges through the budgets, so neither changes freely.
//
// A program includes it after run.h; vm.c holds the table that calls them.

typedef Status (*BuiltinImpl)(Run *r, Pos pos, ConstValues args, Value *out);

static Str builtin_name(Builtin b) {
    return at(ast_builtin_names(), (size_t)b);
}

// The name a stop gives builtin `b`: the host call running where `b` is `host`,
// so a check the host's code meets names what the script called.
static Str builtin_who(const Run *r, Builtin b) {
    if (b == BUILTIN_HOST && r->host_call.len > 0) return r->host_call;
    return builtin_name(b);
}

// ── the argument helpers ──

static STATUS_MUST_USE Status builtin_string_arg(Run *r, Pos pos, Builtin b, const Value *v, Str what, Str *out) {
    if (v->tag == VALUE_STRING) {
        *out = v->as.string;
        return OK;
    }
    return run_failf(r, pos, CLASS_TYPE, "`{s}`: {s} must be a string, not {s}", S("`"), builtin_who(r, b), S("`: "), what, S(" must be a string, not "), value_type_name(v));
}

static STATUS_MUST_USE Status builtin_int_arg(Run *r, Pos pos, Builtin b, const Value *v, Str what, int64_t *out) {
    if (v->tag == VALUE_INT) {
        *out = v->as.integer;
        return OK;
    }
    return run_failf(r, pos, CLASS_TYPE, "`{s}`: {s} must be an integer, not {s}", S("`"), builtin_who(r, b), S("`: "), what, S(" must be an integer, not "), value_type_name(v));
}

// A list, or a table still empty (which becomes one).
static STATUS_MUST_USE Status builtin_list_arg(Run *r, Pos pos, Builtin b, const Value *v, Table **out) {
    if (v->tag == VALUE_TABLE) {
        if (v->as.table->kind != TABLE_MAP) {
            *out = v->as.table;
            return OK;
        }
        return run_failf(r, pos, CLASS_TYPE, "`{s}` takes a list, given a map", S("`"), builtin_who(r, b), S("` takes a list, given a map"));
    }
    return run_failf(r, pos, CLASS_TYPE, "`{s}` takes a list, not {s}", S("`"), builtin_who(r, b), S("` takes a list, not "), value_type_name(v));
}

// A map, or a table still empty.
static STATUS_MUST_USE Status builtin_map_arg(Run *r, Pos pos, Builtin b, const Value *v, Table **out) {
    if (v->tag == VALUE_TABLE) {
        if (v->as.table->kind != TABLE_LIST) {
            *out = v->as.table;
            return OK;
        }
        return run_failf(r, pos, CLASS_TYPE, "`{s}` takes a map, given a list (`#l` is a list's length)", S("`"), builtin_who(r, b), S("` takes a map, given a list (`#l` is a list's length)"));
    }
    return run_failf(r, pos, CLASS_TYPE, "`{s}` takes a map, not {s}", S("`"), builtin_who(r, b), S("` takes a map, not "), value_type_name(v));
}

// The price of bulk work: a step for every 16 bytes, or part of 16, that a
// read brings, a scan looks at or a search runs over.
enum { BUILTIN_BYTES_PER_STEP = 16 };

static STATUS_MUST_USE Status builtin_charge_bytes(Run *r, Pos pos, uint64_t n) {
    return run_charge(r, pos, n / BUILTIN_BYTES_PER_STEP + (n % BUILTIN_BYTES_PER_STEP != 0 ? 1 : 0));
}

// A scalar as `print` and `tostring` write it; a table is an error naming
// `what`.
static STATUS_MUST_USE Status builtin_scalar_text(Run *r, Pos pos, const Value *v, IntText *t, Str what, Str *out) {
    switch (v->tag) {
    case VALUE_NIL: *out = S("nil"); return OK;
    case VALUE_BOOL: *out = v->as.boolean ? S("true") : S("false"); return OK;
    case VALUE_INT: *out = int_text(t, v->as.integer); return OK;
    case VALUE_STRING: *out = v->as.string; return OK;
    case VALUE_TABLE: break;
    }
    return run_failf(r, pos, CLASS_TYPE, "`{s}` takes scalars, not {s}: `for x in l do {s}(x) end` takes a list's items one by one",
                     S("`"), what, S("` takes scalars, not "), value_type_name(v), S(": `for x in l do "), what, S("(x) end` takes a list's items one by one"));
}

static STATUS_MUST_USE Status builtin_new_list(Run *r, Pos pos, Table **out) {
    TRY(run_new_table(r, pos, out));
    (*out)->kind = TABLE_LIST;
    return OK;
}

static STATUS_MUST_USE Status builtin_push_str(Run *r, Pos pos, Table *t, Str s) {
    Value v;
    value_string(&v, s);
    return run_list_push(r, pos, t, &v);
}

// ── lists and maps ──

static STATUS_MUST_USE Status bi_push(Run *r, Pos pos, ConstValues args, Value *out) {
    Table *t = NULL;
    TRY(builtin_list_arg(r, pos, BUILTIN_PUSH, &at(args, 0), &t));
    if (at(args, 1).tag == VALUE_NIL) return run_fail(&r->ctx, pos, CLASS_TYPE, S("a list cannot hold nil"));
    TRY(run_list_push(r, pos, t, &at(args, 1)));
    t->kind = TABLE_LIST;
    value_nil(out);
    return OK;
}

static STATUS_MUST_USE Status bi_pop(Run *r, Pos pos, ConstValues args, Value *out) {
    Table *t = NULL;
    TRY(builtin_list_arg(r, pos, BUILTIN_POP, &at(args, 0), &t));
    if (t->list.len == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`pop` of an empty list: nothing to take (`#l > 0` asks first)"));
    *out = ValueVec_pop(&t->list);
    return OK;
}

static STATUS_MUST_USE Status bi_reverse(Run *r, Pos pos, ConstValues args, Value *out) {
    Table *t = NULL;
    TRY(builtin_list_arg(r, pos, BUILTIN_REVERSE, &at(args, 0), &t));
    TRY(run_charge(r, pos, t->list.len));
    size_t n = t->list.len;
    for (size_t i = 0; i < n / 2; i++) {
        Value x = at(t->list, i);
        at(t->list, i) = at(t->list, n - 1 - i);
        at(t->list, n - 1 - i) = x;
    }
    value_nil(out);
    return OK;
}

enum { BUILTIN_SAME_DEPTH_MAX = 200 };

// Whether two values are the same all the way down.
static STATUS_MUST_USE Status builtin_same_at(Run *r, Pos pos, const Value *a, const Value *b, size_t depth, bool *out) {
    if (depth == BUILTIN_SAME_DEPTH_MAX) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`same`: tables nested more than 200 deep (does one hold itself?)"));
    TRY(run_charge(r, pos, 1));
    *out = false;
    switch (a->tag) {
    case VALUE_NIL: *out = b->tag == VALUE_NIL; return OK;
    case VALUE_BOOL: *out = b->tag == VALUE_BOOL && b->as.boolean == a->as.boolean; return OK;
    case VALUE_INT: *out = b->tag == VALUE_INT && b->as.integer == a->as.integer; return OK;
    case VALUE_STRING:
        if (b->tag != VALUE_STRING || b->as.string.len != a->as.string.len) return OK;
        TRY(builtin_charge_bytes(r, pos, a->as.string.len));
        *out = str_eq(a->as.string, b->as.string);
        return OK;
    case VALUE_TABLE: break;
    }
    if (b->tag != VALUE_TABLE) return OK;
    Table *t = a->as.table;
    Table *u = b->as.table;
    if (t == u) {
        *out = true;
        return OK;
    }
    size_t t_n = table_count(t);
    size_t u_n = table_count(u);
    if (t_n == 0 && u_n == 0) {
        *out = true;
        return OK;
    }
    if (t->kind != u->kind || t_n != u_n) return OK;
    if (t->kind == TABLE_LIST) {
        for (size_t i = 0; i < t_n; i++) {
            bool same = false;
            TRY(builtin_same_at(r, pos, &at(t->list, i), &at(u->list, i), depth + 1, &same));
            if (!same) return OK;
        }
        *out = true;
        return OK;
    }
    for (size_t i = 0; i < t_n; i++) {
        Str k = ValueMap_key_at(&t->map, i);
        Value x = ValueMap_value_at(&t->map, i);
        TRY(builtin_charge_bytes(r, pos, k.len));
        Value y;
        if (!ValueMap_get(&u->map, k, &y)) return OK;
        bool same = false;
        TRY(builtin_same_at(r, pos, &x, &y, depth + 1, &same));
        if (!same) return OK;
    }
    *out = true;
    return OK;
}

static STATUS_MUST_USE Status bi_same(Run *r, Pos pos, ConstValues args, Value *out) {
    bool same = false;
    TRY(builtin_same_at(r, pos, &at(args, 0), &at(args, 1), 0, &same));
    value_bool(out, same);
    return OK;
}

// `take(l, n)` (`from_end` false) or `last(l, n)`: a new list of the first or
// the last `n` items, or all of a shorter list.
static STATUS_MUST_USE Status builtin_part(Run *r, Pos pos, Builtin b, ConstValues args, bool from_end, Value *out) {
    Table *t = NULL;
    TRY(builtin_list_arg(r, pos, b, &at(args, 0), &t));
    int64_t n = 0;
    TRY(builtin_int_arg(r, pos, b, &at(args, 1), S("its count"), &n));
    if (n < 0) return run_failf(r, pos, CLASS_ARGUMENT, "`{s}`: a count of {d}", S("`"), builtin_name(b), S("`: a count of "), run_num(r, n));
    size_t len = t->list.len;
    size_t k = (uint64_t)n < len ? (size_t)n : len;
    TRY(run_charge(r, pos, k));
    Table *o = NULL;
    TRY(run_new_table(r, pos, &o));
    if (ValueVec_reserve(&o->list, k) != OK) return run_values_oom(r, pos);
    size_t from = from_end ? len - k : 0;
    for (size_t i = 0; i < k; i++) TRY(run_list_push(r, pos, o, &at(t->list, from + i)));
    o->kind = TABLE_LIST;
    value_table(out, o);
    return OK;
}

static STATUS_MUST_USE Status bi_take(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_part(r, pos, BUILTIN_TAKE, args, false, out);
}

static STATUS_MUST_USE Status bi_last(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_part(r, pos, BUILTIN_LAST, args, true, out);
}

static STATUS_MUST_USE Status bi_keys(Run *r, Pos pos, ConstValues args, Value *out) {
    Table *t = NULL;
    TRY(builtin_map_arg(r, pos, BUILTIN_KEYS, &at(args, 0), &t));
    Table *o = NULL;
    TRY(run_new_table(r, pos, &o));
    size_t n = ValueMap_len(&t->map);
    TRY(run_charge(r, pos, n));
    if (ValueVec_reserve(&o->list, n) != OK) return run_values_oom(r, pos);
    for (size_t i = 0; i < n; i++) TRY(builtin_push_str(r, pos, o, ValueMap_key_at(&t->map, i)));
    o->kind = TABLE_LIST;
    value_table(out, o);
    return OK;
}

static STATUS_MUST_USE Status bi_has_key(Run *r, Pos pos, ConstValues args, Value *out) {
    Table *t = NULL;
    TRY(builtin_map_arg(r, pos, BUILTIN_HAS_KEY, &at(args, 0), &t));
    Str k = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_HAS_KEY, &at(args, 1), S("its key"), &k));
    TRY(run_charge(r, pos, k.len));
    value_bool(out, map_core_find(&t->map.core, k) != SIZE_MAX);
    return OK;
}

static STATUS_MUST_USE Status bi_get(Run *r, Pos pos, ConstValues args, Value *out) {
    Value fallback;
    value_nil(&fallback);
    if (args.len > 2) fallback = at(args, 2);
    const Value *tv = &at(args, 0);
    const Value *kv = &at(args, 1);
    if (tv->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`get` takes a map or a list, not {s}", S("`get` takes a map or a list, not "), value_type_name(tv));
    Table *t = tv->as.table;
    if (kv->tag == VALUE_INT) {
        int64_t k = kv->as.integer;
        if (t->kind == TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`get`: a map's keys are strings: this one is a map, given an integer"));
        if (k < 1 || (uint64_t)k > t->list.len) {
            *out = fallback;
            return OK;
        }
        *out = at(t->list, (size_t)(k - 1));
        return OK;
    }
    if (kv->tag == VALUE_STRING) {
        if (t->kind == TABLE_LIST) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`get`: a list's keys are integers: this one is a list, given a string"));
        TRY(run_charge(r, pos, kv->as.string.len));
        if (!ValueMap_get(&t->map, kv->as.string, out)) *out = fallback;
        return OK;
    }
    return run_failf(r, pos, CLASS_TYPE, "`get`: a key must be an integer (a list) or a string (a map), not {s}", S("`get`: a key must be an integer (a list) or a string (a map), not "), value_type_name(kv));
}

static STATUS_MUST_USE Status bi_count(Run *r, Pos pos, ConstValues args, Value *out) {
    const Value *tv = &at(args, 0);
    if (tv->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`count` takes a map or a list, not {s}", S("`count` takes a map or a list, not "), value_type_name(tv));
    Table *t = tv->as.table;
    value_int(out, (int64_t)(t->kind == TABLE_LIST ? t->list.len : ValueMap_len(&t->map)));
    return OK;
}

// ── sorting ──

// The most a sort of `n` items, `bytes` of them in all, can cost.
static uint64_t builtin_sort_bound(size_t n, uint64_t bytes) {
    if (n < 2) return 1;
    uint64_t lg = 0;
    while (((uint64_t)1 << lg) < (uint64_t)n) lg++;
    return 2 * (uint64_t)n * lg + bytes * lg;
}

// Whether `a` sorts before `b`: all integers or all strings, checked before.
static bool builtin_less(const Value *a, const Value *b) {
    if (a->tag == VALUE_INT) return a->as.integer < b->as.integer;
    return str_order(a->as.string, b->as.string) < 0;
}

// One row of a sort by keys: its keys' values, looked up once, and the row.
typedef struct {
    Value *keys;
    Value row;
} SortRow;

typedef struct {
    Str key;
    bool desc;
} SortKey;

SLICE_TYPE(SortRows, SortRow);
SLICE_TYPE(SortKeys, SortKey);
SLICE_TYPE(KeyValues, Value);

enum { BUILTIN_SORT_KEYS_MAX = 8 };

static bool builtin_row_less(SortKeys keys, const SortRow *a, const SortRow *b) {
    KeyValues av = {a->keys, keys.len};
    KeyValues bv = {b->keys, keys.len};
    for (size_t k = 0; k < keys.len; k++) {
        if (builtin_less(&at(av, k), &at(bv, k))) return !at(keys, k).desc;
        if (builtin_less(&at(bv, k), &at(av, k))) return at(keys, k).desc;
    }
    return false;
}

// A stable merge sort of `items` (rows when `keys` has any, else scalars,
// descending when `desc`), through `tmp` of the same length.
static void builtin_merge_values(Values items, Values tmp, bool desc) {
    size_t n = items.len;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo;
            size_t j = mid;
            size_t k = lo;
            while (i < mid && j < hi) {
                bool take_right = desc ? builtin_less(&at(items, i), &at(items, j)) : builtin_less(&at(items, j), &at(items, i));
                at(tmp, k++) = take_right ? at(items, j++) : at(items, i++);
            }
            while (i < mid) at(tmp, k++) = at(items, i++);
            while (j < hi) at(tmp, k++) = at(items, j++);
        }
        for (size_t k = 0; k < n; k++) at(items, k) = at(tmp, k);
    }
}

static void builtin_merge_rows(SortRows rows, SortRows tmp, SortKeys keys) {
    size_t n = rows.len;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo;
            size_t j = mid;
            size_t k = lo;
            while (i < mid && j < hi) {
                bool take_right = builtin_row_less(keys, &at(rows, j), &at(rows, i));
                at(tmp, k++) = take_right ? at(rows, j++) : at(rows, i++);
            }
            while (i < mid) at(tmp, k++) = at(rows, i++);
            while (j < hi) at(tmp, k++) = at(rows, j++);
        }
        for (size_t k = 0; k < n; k++) at(rows, k) = at(tmp, k);
    }
}

// `sort`'s order, "asc" or "desc"; `*desc` whether it is descending.
static STATUS_MUST_USE Status builtin_order_arg(Run *r, Pos pos, const Value *v, bool *desc) {
    Str order = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_SORT, v, S("its order"), &order));
    if (str_eq(order, S("desc"))) {
        *desc = true;
        return OK;
    }
    if (str_eq(order, S("asc"))) {
        *desc = false;
        return OK;
    }
    return run_failf(r, pos, CLASS_ARGUMENT, "`sort`'s order is \"asc\" or \"desc\", not \"{s}\"", S("`sort`'s order is \"asc\" or \"desc\", not \""), order, S("\""));
}

static STATUS_MUST_USE Status builtin_sort_keys(Run *r, Pos pos, const Value *keys_arg, bool desc, SortKey *keys, size_t *n_keys) {
    if (keys_arg->tag == VALUE_STRING) {
        SortKey k = {keys_arg->as.string, desc};
        *keys = k;
        *n_keys = 1;
        return OK;
    }
    if (keys_arg->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`sort`'s keys are a key's name or a list of them, not {s}", S("`sort`'s keys are a key's name or a list of them, not "), value_type_name(keys_arg));
    Table *kt = keys_arg->as.table;
    if (kt->kind != TABLE_LIST || kt->list.len == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`sort`'s keys are a key's name or a list of them, not an empty list or a map"));
    if (kt->list.len > BUILTIN_SORT_KEYS_MAX) return run_failf(r, pos, CLASS_ARGUMENT, "`sort` takes at most {d} keys, given {d}", S("`sort` takes at most 8 keys, given "), run_unum(r, kt->list.len), S(""));
    SortKeys room = {keys, BUILTIN_SORT_KEYS_MAX};
    for (size_t i = 0; i < kt->list.len; i++) {
        const Value *k = &at(kt->list, i);
        Str nth = run_unum(r, (uint64_t)i + 1);
        if (k->tag == VALUE_STRING) {
            SortKey sk = {k->as.string, desc};
            at(room, i) = sk;
            continue;
        }
        if (k->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`sort`'s key {d} is {s}: a key is a name or {{key = name, desc = true}}", S("`sort`'s key "), nth, S(" is "), value_type_name(k), S(": a key is a name or {key = name, desc = true}"));
        Table *km = k->as.table;
        if (km->kind != TABLE_MAP) return run_failf(r, pos, CLASS_ARGUMENT, "`sort`'s key {d} is a list: a key is a name or {{key = name, desc = true}}", S("`sort`'s key "), nth, S(" is a list: a key is a name or {key = name, desc = true}"));
        Value name;
        if (!ValueMap_get(&km->map, S("key"), &name)) return run_failf(r, pos, CLASS_ARGUMENT, "`sort`'s key {d} has no `key`: {{key = name, desc = true}}", S("`sort`'s key "), nth, S(" has no `key`: {key = name, desc = true}"));
        if (name.tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`sort`'s key {d}: its `key` is a name, not {s}", S("`sort`'s key "), nth, S(": its `key` is a name, not "), value_type_name(&name));
        bool own = desc;
        for (size_t e = 0; e < ValueMap_len(&km->map); e++) {
            Str ek = ValueMap_key_at(&km->map, e);
            Value ev = ValueMap_value_at(&km->map, e);
            if (str_eq(ek, S("key"))) continue;
            if (!str_eq(ek, S("desc"))) return run_failf(r, pos, CLASS_ARGUMENT, "`sort`'s key {d}: `{s}` is not one of its fields (`key`, `desc`)", S("`sort`'s key "), nth, S(": `"), ek, S("` is not one of its fields (`key`, `desc`)"));
            if (ev.tag != VALUE_BOOL) return run_failf(r, pos, CLASS_TYPE, "`sort`'s key {d}: `desc` is true or false, not {s}", S("`sort`'s key "), nth, S(": `desc` is true or false, not "), value_type_name(&ev));
            own = ev.as.boolean;
        }
        SortKey sk = {name.as.string, own};
        at(room, i) = sk;
    }
    *n_keys = kt->list.len;
    return OK;
}

// A list of maps sorted in place by its keys, stably: rows equal in every key
// keep their order, also when descending.
static STATUS_MUST_USE Status builtin_sort_by_keys(Run *r, Pos pos, Table *t, const Value *keys_arg, bool desc) {
    SortKey keys_buf[BUILTIN_SORT_KEYS_MAX];
    size_t n_keys = 0;
    TRY(builtin_sort_keys(r, pos, keys_arg, desc, keys_buf, &n_keys));
    SortKeys keys = {keys_buf, n_keys};
    size_t n = t->list.len;
    if (n == 0) return OK;
    uint64_t key_bytes = 0;
    for (size_t k = 0; k < keys.len; k++) key_bytes += at(keys, k).key.len;
    // Each row's keys are hashed twice: checked here, fetched below.
    TRY(run_charge(r, pos, 2 * (uint64_t)n * key_bytes));
    ValueTag kinds[BUILTIN_SORT_KEYS_MAX];
    bool kind_set[BUILTIN_SORT_KEYS_MAX] = {false};
    SLICE_TYPE(KindSlots, ValueTag);
    SLICE_TYPE(KindSet, bool);
    KindSlots ks = {kinds, BUILTIN_SORT_KEYS_MAX};
    KindSet kset = {kind_set, BUILTIN_SORT_KEYS_MAX};
    uint64_t bytes = 0;
    for (size_t i = 0; i < n; i++) {
        const Value *item = &at(t->list, i);
        Str nth = run_unum(r, (uint64_t)i + 1);
        if (item->tag != VALUE_TABLE || item->as.table->kind != TABLE_MAP) return run_failf(r, pos, CLASS_TYPE, "`sort` by a key takes a list of maps, and item {d} is {s}", S("`sort` by a key takes a list of maps, and item "), nth, S(" is "), value_type_name(item));
        for (size_t j = 0; j < keys.len; j++) {
            Str key = at(keys, j).key;
            Value v;
            if (!ValueMap_get(&item->as.table->map, key, &v)) return run_failf(r, pos, CLASS_INDEX, "`sort`: item {d} has no \"{s}\"", S("`sort`: item "), nth, S(" has no \""), key, S("\""));
            bool scalar = v.tag == VALUE_INT || v.tag == VALUE_STRING;
            if (!scalar || (at(kset, j) && at(ks, j) != v.tag)) return run_failf(r, pos, CLASS_TYPE, "`sort` by \"{s}\" takes all integers or all strings, and item {d}'s is {s}", S("`sort` by \""), key, S("\" takes all integers or all strings, and item "), nth, S("'s is "), value_type_name(&v));
            at(ks, j) = v.tag;
            at(kset, j) = true;
            if (v.tag == VALUE_STRING) bytes += v.as.string.len;
        }
    }
    // At most two comparisons per key for each comparison of the bound.
    TRY(run_charge(r, pos, 2 * (uint64_t)keys.len * builtin_sort_bound(n, bytes)));
    SortRow *rows_buf = ARENA_ARRAY(r->ctx.values, SortRow, n);
    SortRow *tmp_buf = ARENA_ARRAY(r->ctx.values, SortRow, n);
    if (rows_buf == NULL || tmp_buf == NULL) return run_values_oom(r, pos);
    SortRows rows = {rows_buf, n};
    SortRows tmp = {tmp_buf, n};
    for (size_t i = 0; i < n; i++) {
        Value *kv = ARENA_ARRAY(r->ctx.values, Value, keys.len);
        if (kv == NULL) return run_values_oom(r, pos);
        KeyValues slots = {kv, keys.len};
        const Value *item = &at(t->list, i);
        for (size_t j = 0; j < keys.len; j++) {
            // The pass above found every key in every item.
            if (!ValueMap_get(&item->as.table->map, at(keys, j).key, &at(slots, j))) c_bug("builtins: `sort` lost a key the pass before it found");
        }
        at(rows, i).keys = kv;
        at(rows, i).row = *item;
    }
    builtin_merge_rows(rows, tmp, keys);
    for (size_t i = 0; i < n; i++) at(t->list, i) = at(rows, i).row;
    return OK;
}

static STATUS_MUST_USE Status bi_sort(Run *r, Pos pos, ConstValues args, Value *out) {
    Table *t = NULL;
    TRY(builtin_list_arg(r, pos, BUILTIN_SORT, &at(args, 0), &t));
    *out = at(args, 0);
    size_t n = t->list.len;
    // With no items a key cannot be told from an order; either way the
    // result is the empty list.
    if (n == 0) return OK;
    bool rows = at(t->list, 0).tag == VALUE_TABLE;
    bool second = args.len > 1 && at(args, 1).tag != VALUE_NIL;
    if (second && (rows || at(args, 1).tag == VALUE_TABLE)) {
        bool desc = false;
        if (args.len > 2) TRY(builtin_order_arg(r, pos, &at(args, 2), &desc));
        return builtin_sort_by_keys(r, pos, t, &at(args, 1), desc);
    }
    if (args.len > 2) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`sort` of integers or strings takes one order, `sort(l, \"desc\")`; a key is a list of maps'"));
    bool desc = false;
    if (second) {
        const Value *o = &at(args, 1);
        if (o->tag == VALUE_STRING && !str_eq(o->as.string, S("asc")) && !str_eq(o->as.string, S("desc"))) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`sort` of integers or strings takes an order, \"asc\" or \"desc\"; \"{s}\" would be a key, which sorts a list of maps",
                             S("`sort` of integers or strings takes an order, \"asc\" or \"desc\"; \""), o->as.string, S("\" would be a key, which sorts a list of maps"));
        }
        TRY(builtin_order_arg(r, pos, o, &desc));
    }
    ValueTag kind = at(t->list, 0).tag;
    for (size_t i = 0; i < n; i++) {
        const Value *item = &at(t->list, i);
        if (item->tag != kind || (kind != VALUE_INT && kind != VALUE_STRING)) {
            return run_failf(r, pos, CLASS_TYPE, "`sort` takes all integers or all strings, and item {d} is {s}", S("`sort` takes all integers or all strings, and item "), run_unum(r, (uint64_t)i + 1), S(" is "), value_type_name(item));
        }
    }
    // The bound charged before sorting: a step per comparison and per byte,
    // each byte compared at most once a pass.
    uint64_t bytes = 0;
    if (kind == VALUE_STRING) {
        for (size_t i = 0; i < n; i++) bytes += at(t->list, i).as.string.len;
    }
    TRY(run_charge(r, pos, builtin_sort_bound(n, bytes)));
    Value *tmp_buf = ARENA_ARRAY(r->ctx.values, Value, n);
    if (tmp_buf == NULL) return run_values_oom(r, pos);
    Values items = {t->list.ptr, n};
    Values tmp = {tmp_buf, n};
    builtin_merge_values(items, tmp, desc);
    return OK;
}

// ── numbers ──

static STATUS_MUST_USE Status builtin_extreme(Run *r, Pos pos, Builtin b, ConstValues args, Value *out) {
    int64_t best = 0;
    TRY(builtin_int_arg(r, pos, b, &at(args, 0), S("each argument"), &best));
    for (size_t i = 1; i < args.len; i++) {
        int64_t x = 0;
        TRY(builtin_int_arg(r, pos, b, &at(args, i), S("each argument"), &x));
        if (b == BUILTIN_MIN ? x < best : x > best) best = x;
    }
    value_int(out, best);
    return OK;
}

static STATUS_MUST_USE Status bi_min(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_extreme(r, pos, BUILTIN_MIN, args, out);
}

static STATUS_MUST_USE Status bi_max(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_extreme(r, pos, BUILTIN_MAX, args, out);
}

static STATUS_MUST_USE Status bi_abs(Run *r, Pos pos, ConstValues args, Value *out) {
    int64_t x = 0;
    TRY(builtin_int_arg(r, pos, BUILTIN_ABS, &at(args, 0), S("its integer"), &x));
    if (x == INT64_MIN) return run_failf(r, pos, CLASS_ARITHMETIC, "abs({d}) overflows 64 bits", S("abs("), run_num(r, x), S(") overflows 64 bits"));
    value_int(out, x < 0 ? -x : x);
    return OK;
}

// ── time and the run ──

static STATUS_MUST_USE Status bi_now(Run *r, Pos pos, ConstValues args, Value *out) {
    (void)pos;
    (void)args;
    value_int(out, r->now);
    return OK;
}

static STATUS_MUST_USE Status bi_fail(Run *r, Pos pos, ConstValues args, Value *out) {
    (void)out;
    Str msg = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_FAIL, &at(args, 0), S("its message"), &msg));
    TRY(run_charge(r, pos, msg.len));
    return run_fail(&r->ctx, pos, CLASS_FAIL, msg);
}

// The host's call `name` with the rest of the arguments: its name is what the
// stops in its checks give, until it returns. Where there is no host (metal), a
// stop of class `host` naming the call.
static STATUS_MUST_USE Status bi_host(Run *r, Pos pos, ConstValues args, Value *out) {
    Str name = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &at(args, 0), S("its call's name"), &name));
    TRY(run_charge(r, pos, name.len));
    if (r->host.call != NULL) {
        Str outer = r->host_call;
        r->host_call = name;
        ConstValues rest = SLICE_SUB(ConstValues, args, 1, args.len);
        Status s = r->host.call(r->host.ctx, r, pos, name, rest, out);
        r->host_call = outer;
        return s;
    }
    return run_failf(r, pos, CLASS_HOST, "`{s}`: not on this host", S("`"), name, S("`: not on this host"));
}

static STATUS_MUST_USE Status bi_arg(Run *r, Pos pos, ConstValues args, Value *out) {
    Str name = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_ARG, &at(args, 0), S("its name"), &name));
    TRY(run_charge(r, pos, name.len));
    Value fallback;
    value_nil(&fallback);
    if (args.len > 1) fallback = at(args, 1);
    SLICE_TYPE(GivenArgs, const Arg);
    GivenArgs given = {r->args, r->args_len};
    for (size_t i = 0; i < given.len; i++) {
        const Arg *a = &at(given, i);
        if (!str_eq(a->name, name)) continue;
        TRY(run_charge(r, pos, a->value.len));
        Str shown = str_sub(a->value, 0, a->value.len < 40 ? a->value.len : 40);
        if (fallback.tag == VALUE_INT) {
            int64_t n = 0;
            if (!cli_parse_integer(a->value, &n)) return run_failf(r, pos, CLASS_ARGUMENT, "`arg`: {s}={s} is not an integer, and its default is one", S("`arg`: "), name, S("="), shown, S(" is not an integer, and its default is one"));
            value_int(out, n);
            return OK;
        }
        if (fallback.tag == VALUE_BOOL) {
            if (str_eq(a->value, S("true")) || str_eq(a->value, S("1"))) {
                value_bool(out, true);
                return OK;
            }
            if (str_eq(a->value, S("false")) || str_eq(a->value, S("0"))) {
                value_bool(out, false);
                return OK;
            }
            return run_failf(r, pos, CLASS_ARGUMENT, "`arg`: {s}={s} is not true or false, and its default is one", S("`arg`: "), name, S("="), shown, S(" is not true or false, and its default is one"));
        }
        Str copy = {0};
        if (!arena_dup(r->ctx.values, a->value, &copy)) return run_values_oom(r, pos);
        value_string(out, copy);
        return OK;
    }
    *out = fallback;
    return OK;
}

// Never reached: `yield()` compiles to its own op, and a call with arguments
// is refused by its count, so a call here is a bug in the C.
static STATUS_MUST_USE Status bi_yield(Run *r, Pos pos, ConstValues args, Value *out) {
    (void)r;
    (void)pos;
    (void)args;
    (void)out;
    c_bug("builtins: `yield` reached the builtins' table, past its own op");
    // Never reached: TCC refuses a function that might return no value.
    return STOP;
}

static int64_t builtin_capped(uint64_t n) {
    return n > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)n;
}

// What is left of each budget a script spends as it runs.
static STATUS_MUST_USE Status bi_budget(Run *r, Pos pos, ConstValues args, Value *out) {
    (void)args;
    const Budgets *b = &r->ctx.budgets;
    Table *t = NULL;
    TRY(run_new_table(r, pos, &t));
    Arena *v = r->ctx.values;
    TRY(run_map_put_int(r, pos, t, S("steps"), builtin_capped(run_sat_sub(b->steps, r->steps))));
    TRY(run_map_put_int(r, pos, t, S("reads"), builtin_capped(run_sat_sub(b->reads, r->reads))));
    TRY(run_map_put_int(r, pos, t, S("read_bytes"), builtin_capped(run_sat_sub(b->read_bytes, r->read_bytes))));
    TRY(run_map_put_int(r, pos, t, S("changes"), builtin_capped(run_sat_sub(b->changes, r->changes))));
    TRY(run_map_put_int(r, pos, t, S("change_bytes"), builtin_capped(run_sat_sub(b->change_bytes, r->change_bytes))));
    TRY(run_map_put_int(r, pos, t, S("programs"), builtin_capped(run_sat_sub(b->programs, r->host_runs))));
    TRY(run_map_put_int(r, pos, t, S("output"), builtin_capped(run_sat_sub(r->out_cap, r->out.len))));
    TRY(run_map_put_int(r, pos, t, S("arena_bytes"), builtin_capped(run_sat_sub(v->limit, v->held))));
    t->kind = TABLE_MAP;
    value_table(out, t);
    return OK;
}

static STATUS_MUST_USE Status bi_call_bytes(Run *r, Pos pos, ConstValues args, Value *out) {
    (void)pos;
    (void)args;
    value_int(out, builtin_capped(r->ctx.budgets.call_bytes));
    return OK;
}

// The latest second `date` writes: 9999-12-31T23:59:59Z.
static const int64_t BUILTIN_LAST_SECOND = 253402300799;

static int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0))) q--;
    return q;
}

static int64_t floor_mod(int64_t a, int64_t b) {
    return a - floor_div(a, b) * b;
}

// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
static int64_t days_from_civil(int64_t y0, int64_t m, int64_t d) {
    int64_t y = m <= 2 ? y0 - 1 : y0;
    int64_t era = floor_div(y, 400);
    int64_t yoe = y - era * 400;
    int64_t mp = floor_mod(m + 9, 12);
    int64_t doy = floor_div(153 * mp + 2, 5) + d - 1;
    int64_t doe = yoe * 365 + floor_div(yoe, 4) - floor_div(yoe, 100) + doy;
    return era * 146097 + doe - 719468;
}

typedef struct {
    int64_t y;
    int64_t m;
    int64_t d;
} Civil;

static void civil_from_days(int64_t z0, Civil *out) {
    int64_t z = z0 + 719468;
    int64_t era = floor_div(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = floor_div(doe - floor_div(doe, 1460) + floor_div(doe, 36524) - floor_div(doe, 146096), 365);
    int64_t doy = doe - (365 * yoe + floor_div(yoe, 4) - floor_div(yoe, 100));
    int64_t mp = floor_div(5 * doy + 2, 153);
    out->d = doy - floor_div(153 * mp + 2, 5) + 1;
    out->m = mp < 10 ? mp + 3 : mp - 9;
    out->y = yoe + era * 400 + (out->m <= 2 ? 1 : 0);
}

static int64_t days_in_month(int64_t y, int64_t m) {
    if (m == 2) return (floor_mod(y, 4) == 0 && (floor_mod(y, 100) != 0 || floor_mod(y, 400) == 0)) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}

static STATUS_MUST_USE Status builtin_digits(Buf *b, int64_t n, size_t width) {
    IntText t;
    Str s = int_text(&t, n);
    for (size_t k = s.len; k < width; k++) TRY(buf_byte(b, '0'));
    return buf_str(b, s);
}

static STATUS_MUST_USE Status bi_date(Run *r, Pos pos, ConstValues args, Value *out) {
    int64_t t = 0;
    TRY(builtin_int_arg(r, pos, BUILTIN_DATE, &at(args, 0), S("its seconds"), &t));
    if (t < 0 || t > BUILTIN_LAST_SECOND) return run_failf(r, pos, CLASS_ARGUMENT, "`date`: {d} is outside 1970-01-01 to 9999-12-31", S("`date`: "), run_num(r, t), S(" is outside 1970-01-01 to 9999-12-31"));
    Civil c;
    civil_from_days(floor_div(t, 86400), &c);
    int64_t rem = floor_mod(t, 86400);
    TRY(run_charge(r, pos, 20));
    Buf b = buf_make(r->ctx.values);
    Status s = builtin_digits(&b, c.y, 4);
    if (s == OK) s = buf_byte(&b, '-');
    if (s == OK) s = builtin_digits(&b, c.m, 2);
    if (s == OK) s = buf_byte(&b, '-');
    if (s == OK) s = builtin_digits(&b, c.d, 2);
    if (s == OK) s = buf_byte(&b, 'T');
    if (s == OK) s = builtin_digits(&b, rem / 3600, 2);
    if (s == OK) s = buf_byte(&b, ':');
    if (s == OK) s = builtin_digits(&b, (rem % 3600) / 60, 2);
    if (s == OK) s = buf_byte(&b, ':');
    if (s == OK) s = builtin_digits(&b, rem % 60, 2);
    if (s == OK) s = buf_byte(&b, 'Z');
    if (s != OK) return run_values_oom(r, pos);
    value_string(out, buf_view(&b));
    return OK;
}

// `s[at..][0..n]` as decimal; false unless all of it is digits.
static bool builtin_digits_at(Str s, size_t from, size_t n, int64_t *out) {
    if (from + n > s.len) return false;
    int64_t v = 0;
    for (size_t k = from; k < from + n; k++) {
        uint8_t c = str_byte(s, k);
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    *out = v;
    return true;
}

// The seconds since the epoch of an RFC 3339 time, or of a date alone (its
// midnight UTC).
static bool builtin_parse_time(Str s, int64_t *out) {
    int64_t y = 0;
    int64_t m = 0;
    int64_t d = 0;
    if (!builtin_digits_at(s, 0, 4, &y)) return false;
    if (s.len < 10 || str_byte(s, 4) != '-' || str_byte(s, 7) != '-') return false;
    if (!builtin_digits_at(s, 5, 2, &m) || !builtin_digits_at(s, 8, 2, &d)) return false;
    if (m < 1 || m > 12 || d < 1 || d > days_in_month(y, m)) return false;
    int64_t day = days_from_civil(y, m, d) * 86400;
    if (s.len == 10) {
        *out = day;
        return true;
    }
    if (s.len < 20 || str_byte(s, 10) != 'T' || str_byte(s, 13) != ':' || str_byte(s, 16) != ':') return false;
    int64_t hh = 0;
    int64_t mm = 0;
    int64_t ss = 0;
    if (!builtin_digits_at(s, 11, 2, &hh) || !builtin_digits_at(s, 14, 2, &mm) || !builtin_digits_at(s, 17, 2, &ss)) return false;
    if (hh > 23 || mm > 59 || ss > 59) return false;
    size_t k = 19;
    // Fractions of a second are dropped: the result is whole seconds.
    if (str_byte(s, k) == '.') {
        k++;
        size_t from = k;
        while (k < s.len && str_byte(s, k) >= '0' && str_byte(s, k) <= '9') k++;
        if (k == from) return false;
    }
    if (k >= s.len) return false;
    int64_t offset = 0;
    uint8_t c = str_byte(s, k);
    if (c == 'Z') {
        k++;
    } else if (c == '+' || c == '-') {
        int64_t oh = 0;
        int64_t om = 0;
        if (!builtin_digits_at(s, k + 1, 2, &oh)) return false;
        if (k + 3 >= s.len || str_byte(s, k + 3) != ':') return false;
        if (!builtin_digits_at(s, k + 4, 2, &om)) return false;
        if (oh > 23 || om > 59) return false;
        offset = (oh * 60 + om) * 60;
        if (c == '-') offset = -offset;
        k += 6;
    } else {
        return false;
    }
    if (k != s.len) return false;
    *out = day + hh * 3600 + mm * 60 + ss - offset;
    return true;
}

static STATUS_MUST_USE Status bi_seconds(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_SECONDS, &at(args, 0), S("its time"), &s));
    TRY(run_charge(r, pos, s.len));
    Str shown = str_sub(s, 0, s.len < 40 ? s.len : 40);
    int64_t t = 0;
    if (!builtin_parse_time(s, &t)) return run_failf(r, pos, CLASS_ARGUMENT, "`seconds`: \"{s}\" is not an RFC 3339 time (2026-10-14T03:00:00Z, or +02:00 for Z) nor a date (2026-10-14)", S("`seconds`: \""), shown, S("\" is not an RFC 3339 time (2026-10-14T03:00:00Z, or +02:00 for Z) nor a date (2026-10-14)"));
    if (t < 0 || t > BUILTIN_LAST_SECOND) return run_failf(r, pos, CLASS_ARGUMENT, "`seconds`: \"{s}\" is outside 1970-01-01 to 9999-12-31, `date`'s range", S("`seconds`: \""), shown, S("\" is outside 1970-01-01 to 9999-12-31, `date`'s range"));
    value_int(out, t);
    return OK;
}

// ── formatting ──

static STATUS_MUST_USE Status bi_human(Run *r, Pos pos, ConstValues args, Value *out) {
    int64_t n = 0;
    TRY(builtin_int_arg(r, pos, BUILTIN_HUMAN, &at(args, 0), S("its size"), &n));
    TRY(run_charge(r, pos, 16));
    static const Str units_all[7] = {S("B"), S("KiB"), S("MiB"), S("GiB"), S("TiB"), S("PiB"), S("EiB")};
    Names units = {units_all, 7};
    bool neg = n < 0;
    uint64_t mag = neg ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
    Buf b = buf_make(r->ctx.values);
    Status s = neg ? buf_byte(&b, '-') : OK;
    if (mag < 1024) {
        if (s == OK) s = buf_u64(&b, mag);
        if (s == OK) s = buf_lit(&b, " B");
        if (s != OK) return run_values_oom(r, pos);
        value_string(out, buf_view(&b));
        return OK;
    }
    size_t u = 1;
    while (u + 1 < units.len && mag >= ((uint64_t)1 << (10 * (u + 1)))) u++;
    uint64_t unit = (uint64_t)1 << (10 * u);
    // `(mag * 10 + unit / 2) / unit`, the tenths rounded half up, split so 64
    // bits hold it.
    uint64_t tenths = mag / unit * 10 + (mag % unit * 10 + unit / 2) / unit;
    // 1023.95 KiB rounds to 1024.0; carry to 1.0 MiB.
    if (tenths >= 10240 && u + 1 < units.len) {
        u++;
        unit <<= 10;
        tenths = mag / unit * 10 + (mag % unit * 10 + unit / 2) / unit;
    }
    if (s == OK) s = buf_u64(&b, tenths / 10);
    if (s == OK) s = buf_byte(&b, '.');
    if (s == OK) s = buf_u64(&b, tenths % 10);
    if (s == OK) s = buf_byte(&b, ' ');
    if (s == OK) s = buf_str(&b, at(units, u));
    if (s != OK) return run_values_oom(r, pos);
    value_string(out, buf_view(&b));
    return OK;
}

// `text` padded with spaces to `width`: on the right, or for a negative width
// on the left. Longer text is kept whole.
static STATUS_MUST_USE Status builtin_padded(Run *r, Pos pos, Str text, int64_t width, Str *out) {
    uint64_t aw = width < 0 ? (uint64_t)(-(width + 1)) + 1 : (uint64_t)width;
    size_t w = aw > UINT32_MAX ? UINT32_MAX : (size_t)aw;
    if (text.len >= w) {
        *out = text;
        return OK;
    }
    TRY(run_charge(r, pos, w));
    char *p = arena_alloc(r->ctx.values, w, 1);
    if (p == NULL) return run_values_oom(r, pos);
    Bytes room = {p, w};
    size_t gap = w - text.len;
    size_t text_at = width < 0 ? gap : 0;
    size_t fill_at = width < 0 ? 0 : text.len;
    memset(&at(room, fill_at), ' ', gap);
    if (text.len > 0) mem_copy(&at(room, text_at), text.ptr, text.len);
    Str s = {p, w};
    *out = s;
    return OK;
}

static STATUS_MUST_USE Status builtin_cell_text(Run *r, Pos pos, const Value *v, Str what, Str *out) {
    IntText t;
    Str s = {0};
    TRY(builtin_scalar_text(r, pos, v, &t, what, &s));
    if (!arena_dup(r->ctx.values, s, out)) return run_values_oom(r, pos);
    return OK;
}

static STATUS_MUST_USE Status bi_pad(Run *r, Pos pos, ConstValues args, Value *out) {
    const Value *v = &at(args, 0);
    int64_t width = 0;
    TRY(builtin_int_arg(r, pos, BUILTIN_PAD, &at(args, 1), S("its width"), &width));
    Str text = {0};
    if (v->tag == VALUE_STRING) {
        text = v->as.string;
    } else if (v->tag == VALUE_INT) {
        TRY(builtin_cell_text(r, pos, v, S("pad"), &text));
    } else {
        return run_failf(r, pos, CLASS_TYPE, "`pad` takes a string or an integer, not {s}", S("`pad` takes a string or an integer, not "), value_type_name(v));
    }
    Str s = {0};
    TRY(builtin_padded(r, pos, text, width, &s));
    value_string(out, s);
    return OK;
}

SLICE_TYPE(Cells, Str);
SLICE_TYPE(Widths, size_t);
SLICE_TYPE(Numeric, bool);

// Rows (maps) as aligned text: a line of column names, then one a row,
// columns two spaces apart, a column of integers right-aligned.
static STATUS_MUST_USE Status bi_table(Run *r, Pos pos, ConstValues args, Value *out) {
    Table *rows = NULL;
    Table *cols = NULL;
    TRY(builtin_list_arg(r, pos, BUILTIN_TABLE, &at(args, 0), &rows));
    TRY(builtin_list_arg(r, pos, BUILTIN_TABLE, &at(args, 1), &cols));
    Arena *a = r->ctx.values;
    size_t nc = cols->list.len;
    Str *names_buf = ARENA_ARRAY(a, Str, nc);
    if (names_buf == NULL) return run_values_oom(r, pos);
    Cells names = {names_buf, nc};
    for (size_t i = 0; i < nc; i++) {
        const Value *c = &at(cols->list, i);
        if (c->tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`table`: column {d} must be a string, not {s}", S("`table`: column "), run_unum(r, (uint64_t)i + 1), S(" must be a string, not "), value_type_name(c));
        at(names, i) = c->as.string;
    }
    size_t nr = rows->list.len;
    // Every cell costs a step, charged before the grid is made.
    TRY(run_charge(r, pos, (uint64_t)nr * nc));
    Str *cells_buf = ARENA_ARRAY(a, Str, nr * nc);
    size_t *widths_buf = ARENA_ARRAY(a, size_t, nc);
    bool *numeric_buf = ARENA_ARRAY(a, bool, nc);
    if (cells_buf == NULL || widths_buf == NULL || numeric_buf == NULL) return run_values_oom(r, pos);
    Cells cells = {cells_buf, nr * nc};
    Widths widths = {widths_buf, nc};
    Numeric numeric = {numeric_buf, nc};
    for (size_t c = 0; c < nc; c++) {
        at(widths, c) = at(names, c).len;
        at(numeric, c) = nr > 0;
    }
    for (size_t ri = 0; ri < nr; ri++) {
        const Value *row = &at(rows->list, ri);
        if (row->tag != VALUE_TABLE || row->as.table->kind != TABLE_MAP) return run_failf(r, pos, CLASS_TYPE, "`table`: row {d} must be a map, not {s}", S("`table`: row "), run_unum(r, (uint64_t)ri + 1), S(" must be a map, not "), value_type_name(row));
        for (size_t c = 0; c < nc; c++) {
            Str n = at(names, c);
            TRY(run_charge(r, pos, n.len));
            Value v;
            if (!ValueMap_get(&row->as.table->map, n, &v)) return run_failf(r, pos, CLASS_INDEX, "`table`: row {d} has no \"{s}\"", S("`table`: row "), run_unum(r, (uint64_t)ri + 1), S(" has no \""), n, S("\""));
            if (v.tag != VALUE_INT) at(numeric, c) = false;
            Str t = {0};
            TRY(builtin_cell_text(r, pos, &v, S("table"), &t));
            TRY(run_charge(r, pos, t.len));
            at(cells, ri * nc + c) = t;
            if (t.len > at(widths, c)) at(widths, c) = t.len;
        }
    }
    Buf b = buf_make(a);
    for (size_t ri = 0; ri < nr + 1; ri++) {
        if (ri > 0 && buf_byte(&b, '\n') != OK) return run_values_oom(r, pos);
        size_t start = b.len;
        for (size_t c = 0; c < nc; c++) {
            if (c > 0 && buf_lit(&b, "  ") != OK) return run_values_oom(r, pos);
            Str t = ri == 0 ? at(names, c) : at(cells, (ri - 1) * nc + c);
            int64_t w = (int64_t)at(widths, c);
            Str p = {0};
            TRY(builtin_padded(r, pos, t, at(numeric, c) ? -w : w, &p));
            if (buf_str(&b, p) != OK) return run_values_oom(r, pos);
        }
        while (b.len > start && str_byte(buf_view(&b), b.len - 1) == ' ') b.len--;
    }
    value_string(out, buf_view(&b));
    return OK;
}
