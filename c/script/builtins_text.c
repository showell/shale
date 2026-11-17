// builtins_text.c: the text builtins: splitting and joining, searching,
// slicing and replacing, `like` and `match_path` globs, and reading
// integers; and the regular-expression builtins over regex.c. Every search
// is for plain text but the `re_` ones'. A program includes it after
// builtins.c.

// ── searching ──

// Searches for a needle, charging the work as it is done. The
// caller charges the haystack, a step per 16 bytes, for the fast pass over the
// needle's first byte. Each place that pass stops costs a step more, and the
// bytes compared there a step per 16, each batch of compares charged when it
// is done so a budget stops a long near-match within a batch.
typedef struct {
    Run *r;
    Pos pos;
    Str needle;
} Finder;

enum { FINDER_BATCH_COMPARES = 64 * 1024 };

// The first index of the needle in `hay` at or after `from`, in `*at_`, or
// `*found` false.
static STATUS_MUST_USE Status finder_find(const Finder *f, Str hay, size_t from, bool *found, size_t *at_) {
    Str n = f->needle;
    // "" is found where the search starts, as Lua's `find` answers 1.
    if (n.len == 0) {
        *found = from <= hay.len;
        *at_ = from;
        return OK;
    }
    *found = false;
    uint8_t first = str_byte(n, 0);
    Str rest = str_sub(n, 1, n.len);
    size_t k = from;
    uint64_t stops = 0;
    uint64_t compared = 0;
    uint64_t paid_stops = 0;
    uint64_t paid_compared = 0;
    while (k < hay.len) {
        const char *hit = memchr(&at(hay, k), first, hay.len - k);
        if (hit == NULL) break;
        size_t i = (size_t)(hit - hay.ptr);
        stops++;
        if (hay.len - i < n.len) break;
        compared += n.len - 1;
        if (str_eq(str_sub(hay, i + 1, i + n.len), rest)) {
            *found = true;
            *at_ = i;
            break;
        }
        k = i + 1;
        if (compared - paid_compared >= FINDER_BATCH_COMPARES) {
            uint64_t whole = (compared - paid_compared) / BUILTIN_BYTES_PER_STEP * BUILTIN_BYTES_PER_STEP;
            TRY(run_charge(f->r, f->pos, stops - paid_stops + whole / BUILTIN_BYTES_PER_STEP));
            paid_stops = stops;
            paid_compared += whole;
        }
    }
    uint64_t left = compared - paid_compared;
    return run_charge(f->r, f->pos, stops - paid_stops + left / BUILTIN_BYTES_PER_STEP + (left % BUILTIN_BYTES_PER_STEP != 0 ? 1 : 0));
}

static void finder_init(Finder *f, Run *r, Pos pos, Str needle) {
    f->r = r;
    f->pos = pos;
    f->needle = needle;
}

// ── text ──

static STATUS_MUST_USE Status bi_split(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str sep = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_SPLIT, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_SPLIT, &at(args, 1), S("its separator"), &sep));
    if (sep.len == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`split` by \"\" is an error: there is no empty separator"));
    TRY(builtin_charge_bytes(r, pos, s.len));
    Finder f;
    finder_init(&f, r, pos, sep);
    Table *o = NULL;
    TRY(run_new_table(r, pos, &o));
    size_t k = 0;
    for (;;) {
        bool found = false;
        size_t i = 0;
        TRY(finder_find(&f, s, k, &found, &i));
        if (!found) break;
        // Each piece is an item made: a step each.
        TRY(run_charge(r, pos, 1));
        TRY(builtin_push_str(r, pos, o, str_sub(s, k, i)));
        k = i + sep.len;
    }
    TRY(builtin_push_str(r, pos, o, str_sub(s, k, s.len)));
    o->kind = TABLE_LIST;
    value_table(out, o);
    return OK;
}

static STATUS_MUST_USE Status bi_join(Run *r, Pos pos, ConstValues args, Value *out) {
    Table *t = NULL;
    Str sep = {0};
    TRY(builtin_list_arg(r, pos, BUILTIN_JOIN, &at(args, 0), &t));
    TRY(builtin_string_arg(r, pos, BUILTIN_JOIN, &at(args, 1), S("its separator"), &sep));
    Buf b = buf_make(r->ctx.values);
    for (size_t i = 0; i < t->list.len; i++) {
        if (i > 0 && buf_str(&b, sep) != OK) return run_values_oom(r, pos);
        const Value *item = &at(t->list, i);
        IntText it;
        Str piece = {0};
        if (item->tag == VALUE_STRING) {
            piece = item->as.string;
        } else if (item->tag == VALUE_INT) {
            piece = int_text(&it, item->as.integer);
        } else {
            return run_failf(r, pos, CLASS_TYPE, "`join` takes strings and integers, and item {d} is {s}", S("`join` takes strings and integers, and item "), run_unum(r, (uint64_t)i + 1), S(" is "), value_type_name(item));
        }
        TRY(run_charge(r, pos, piece.len + sep.len));
        if (buf_str(&b, piece) != OK) return run_values_oom(r, pos);
    }
    value_string(out, buf_view(&b));
    return OK;
}

// `lines(s)`: `s` split at each `\n`, with no empty last piece after a final
// newline.
static STATUS_MUST_USE Status bi_lines(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_LINES, &at(args, 0), S("its first argument"), &s));
    TRY(run_charge(r, pos, s.len));
    Table *o = NULL;
    TRY(builtin_new_list(r, pos, &o));
    size_t k = 0;
    while (k < s.len) {
        const char *nl = memchr(&at(s, k), '\n', s.len - k);
        size_t end = nl != NULL ? (size_t)(nl - s.ptr) : s.len;
        TRY(builtin_push_str(r, pos, o, str_sub(s, k, end)));
        k = nl != NULL ? end + 1 : s.len;
    }
    value_table(out, o);
    return OK;
}

static bool builtin_is_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0x0b || c == 0x0c;
}

// `words(s)`: the runs between ASCII whitespace.
static STATUS_MUST_USE Status bi_words(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_WORDS, &at(args, 0), S("its first argument"), &s));
    TRY(run_charge(r, pos, s.len));
    Table *o = NULL;
    TRY(builtin_new_list(r, pos, &o));
    size_t k = 0;
    while (k < s.len) {
        while (k < s.len && builtin_is_space(str_byte(s, k))) k++;
        size_t from = k;
        while (k < s.len && !builtin_is_space(str_byte(s, k))) k++;
        if (k > from) TRY(builtin_push_str(r, pos, o, str_sub(s, from, k)));
    }
    value_table(out, o);
    return OK;
}

// `starts_with`, `ends_with` and `contains`.
static STATUS_MUST_USE Status builtin_looks(Run *r, Pos pos, Builtin b, ConstValues args, Value *out) {
    Str s = {0};
    Str t = {0};
    TRY(builtin_string_arg(r, pos, b, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, b, &at(args, 1), S("what it looks for"), &t));
    TRY(builtin_charge_bytes(r, pos, s.len));
    if (b == BUILTIN_STARTS_WITH) {
        value_bool(out, str_starts_with(s, t));
        return OK;
    }
    if (b == BUILTIN_ENDS_WITH) {
        value_bool(out, s.len >= t.len && str_eq(str_sub(s, s.len - t.len, s.len), t));
        return OK;
    }
    Finder f;
    finder_init(&f, r, pos, t);
    bool found = false;
    size_t i = 0;
    TRY(finder_find(&f, s, 0, &found, &i));
    value_bool(out, found);
    return OK;
}

static STATUS_MUST_USE Status bi_starts_with(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_looks(r, pos, BUILTIN_STARTS_WITH, args, out);
}

static STATUS_MUST_USE Status bi_ends_with(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_looks(r, pos, BUILTIN_ENDS_WITH, args, out);
}

static STATUS_MUST_USE Status bi_contains(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_looks(r, pos, BUILTIN_CONTAINS, args, out);
}

// `find(s, t[, from])`: the 1-based index of the first `t` in `s` at or after
// `from`, or nil.
static STATUS_MUST_USE Status bi_find(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str t = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_FIND, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_FIND, &at(args, 1), S("what it looks for"), &t));
    size_t from = 0;
    if (args.len > 2) {
        int64_t f = 0;
        TRY(builtin_int_arg(r, pos, BUILTIN_FIND, &at(args, 2), S("where it starts"), &f));
        if (f < 1 || f > (int64_t)s.len + 1) return run_failf(r, pos, CLASS_ARGUMENT, "`find` starts at 1 to {d} (`#s + 1`), not {d}", S("`find` starts at 1 to "), run_unum(r, (uint64_t)s.len + 1), S(" (`#s + 1`), not "), run_num(r, f));
        from = (size_t)(f - 1);
    }
    TRY(builtin_charge_bytes(r, pos, s.len - from));
    Finder fd;
    finder_init(&fd, r, pos, t);
    bool found = false;
    size_t i = 0;
    TRY(finder_find(&fd, s, from, &found, &i));
    if (found) {
        value_int(out, (int64_t)i + 1);
    } else {
        value_nil(out);
    }
    return OK;
}

// The length of the UTF-8 sequence that starts with `c`: 0 for a byte no
// sequence starts with.
static size_t builtin_utf8_length(uint8_t c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 0;
}

// Whether a sequence of its length is valid UTF-8: not
// overlong, no surrogate half and at most U+10FFFF.
static bool builtin_utf8_valid(Str seq) {
    uint32_t v = str_byte(seq, 0);
    if (seq.len == 1) return true;
    v &= seq.len == 2 ? 0x1F : (seq.len == 3 ? 0x0F : 0x07);
    for (size_t k = 1; k < seq.len; k++) {
        uint8_t c = str_byte(seq, k);
        if ((c & 0xC0) != 0x80) return false;
        v = (v << 6) | (c & 0x3F);
    }
    if (seq.len == 2) return v >= 0x80;
    if (seq.len == 3) return v >= 0x800 && !(v >= 0xD800 && v <= 0xDFFF);
    return v >= 0x10000 && v <= 0x10FFFF;
}

static STATUS_MUST_USE Status bi_bad_utf8(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_BAD_UTF8, &at(args, 0), S("its text"), &s));
    TRY(builtin_charge_bytes(r, pos, s.len));
    size_t i = 0;
    while (i < s.len) {
        size_t n = builtin_utf8_length(str_byte(s, i));
        if (n == 0 || n > s.len - i || !builtin_utf8_valid(str_sub(s, i, i + n))) {
            value_int(out, (int64_t)i + 1);
            return OK;
        }
        i += n;
    }
    value_nil(out);
    return OK;
}

// Lua's `string.sub` range: 1-based, inclusive, negatives from the end,
// clamped to the string. Gives the start in `*start` and the length.
static size_t builtin_sub_range(size_t len, int64_t i, int64_t j, size_t *start) {
    int64_t n = (int64_t)len;
    int64_t a = i < 0 ? (n + i + 1 > 1 ? n + i + 1 : 1) : (i == 0 ? 1 : i);
    int64_t z = j < 0 ? n + j + 1 : (j < n ? j : n);
    if (a < 1) a = 1;
    if (z > n) z = n;
    *start = 0;
    if (a > z) return 0;
    *start = (size_t)(a - 1);
    return (size_t)(z - a + 1);
}

static STATUS_MUST_USE Status bi_sub(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    int64_t i = 0;
    int64_t j = -1;
    TRY(builtin_string_arg(r, pos, BUILTIN_SUB, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_int_arg(r, pos, BUILTIN_SUB, &at(args, 1), S("its start"), &i));
    if (args.len > 2) TRY(builtin_int_arg(r, pos, BUILTIN_SUB, &at(args, 2), S("its end"), &j));
    size_t start = 0;
    size_t n = builtin_sub_range(s.len, i, j, &start);
    TRY(run_charge(r, pos, n));
    value_string(out, str_sub(s, start, start + n));
    return OK;
}

static bool builtin_is_blank(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static Str builtin_trim_start(Str s) {
    size_t k = 0;
    while (k < s.len && builtin_is_blank(str_byte(s, k))) k++;
    return str_sub(s, k, s.len);
}

static Str builtin_trim_end(Str s) {
    size_t k = s.len;
    while (k > 0 && builtin_is_blank(str_byte(s, k - 1))) k--;
    return str_sub(s, 0, k);
}

static STATUS_MUST_USE Status builtin_trimmed(Run *r, Pos pos, Builtin b, ConstValues args, Value *out) {
    Str s = {0};
    TRY(builtin_string_arg(r, pos, b, &at(args, 0), S("its first argument"), &s));
    TRY(run_charge(r, pos, s.len));
    if (b != BUILTIN_TRIM_END) s = builtin_trim_start(s);
    if (b != BUILTIN_TRIM_START) s = builtin_trim_end(s);
    value_string(out, s);
    return OK;
}

static STATUS_MUST_USE Status bi_trim(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_trimmed(r, pos, BUILTIN_TRIM, args, out);
}

static STATUS_MUST_USE Status bi_trim_start(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_trimmed(r, pos, BUILTIN_TRIM_START, args, out);
}

static STATUS_MUST_USE Status bi_trim_end(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_trimmed(r, pos, BUILTIN_TRIM_END, args, out);
}

static STATUS_MUST_USE Status builtin_cased(Run *r, Pos pos, Builtin b, ConstValues args, Value *out) {
    Str s = {0};
    TRY(builtin_string_arg(r, pos, b, &at(args, 0), S("its first argument"), &s));
    TRY(run_charge(r, pos, s.len));
    char *p = arena_alloc(r->ctx.values, s.len, 1);
    if (p == NULL) return run_values_oom(r, pos);
    Bytes o = {p, s.len};
    for (size_t k = 0; k < s.len; k++) {
        uint8_t c = str_byte(s, k);
        if (b == BUILTIN_UPPER && c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        if (b == BUILTIN_LOWER && c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
        at(o, k) = (char)c;
    }
    Str t = {p, s.len};
    value_string(out, t);
    return OK;
}

static STATUS_MUST_USE Status bi_upper(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_cased(r, pos, BUILTIN_UPPER, args, out);
}

static STATUS_MUST_USE Status bi_lower(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_cased(r, pos, BUILTIN_LOWER, args, out);
}

static STATUS_MUST_USE Status bi_tostring(Run *r, Pos pos, ConstValues args, Value *out) {
    IntText it;
    Str t = {0};
    TRY(builtin_scalar_text(r, pos, &at(args, 0), &it, S("tostring"), &t));
    TRY(run_charge(r, pos, t.len));
    Str copy = {0};
    if (!arena_dup(r->ctx.values, t, &copy)) return run_values_oom(r, pos);
    value_string(out, copy);
    return OK;
}

static STATUS_MUST_USE Status bi_tonumber(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_TONUMBER, &at(args, 0), S("its first argument"), &s));
    TRY(run_charge(r, pos, s.len));
    int64_t n = 0;
    if (!cli_parse_integer(s, &n)) return run_failf(r, pos, CLASS_ARGUMENT, "`tonumber`: \"{s}\" is not an integer (an optional `-`, then decimal digits)", S("`tonumber`: \""), str_sub(s, 0, s.len < 40 ? s.len : 40), S("\" is not an integer (an optional `-`, then decimal digits)"));
    value_int(out, n);
    return OK;
}

static STATUS_MUST_USE Status bi_parse_int(Run *r, Pos pos, ConstValues args, Value *out) {
    const Value *v = &at(args, 0);
    if (v->tag == VALUE_NIL || v->tag == VALUE_INT) {
        *out = *v;
        return OK;
    }
    if (v->tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`parse_int` takes a string (or nil, or an integer), not {s}", S("`parse_int` takes a string (or nil, or an integer), not "), value_type_name(v));
    TRY(run_charge(r, pos, v->as.string.len));
    int64_t n = 0;
    if (!cli_parse_integer(builtin_trim_end(builtin_trim_start(v->as.string)), &n)) {
        value_nil(out);
        return OK;
    }
    value_int(out, n);
    return OK;
}

static STATUS_MUST_USE Status bi_rep(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    int64_t n = 0;
    TRY(builtin_string_arg(r, pos, BUILTIN_REP, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_int_arg(r, pos, BUILTIN_REP, &at(args, 1), S("how many times"), &n));
    if (n < 0) return run_failf(r, pos, CLASS_ARGUMENT, "`rep`: {d} times", S("`rep`: "), run_num(r, n), S(" times"));
    if (s.len > 0 && (uint64_t)n > UINT64_MAX / s.len) return run_budget_stop(&r->ctx, pos, BUDGET_ARENA);
    uint64_t total = (uint64_t)s.len * (uint64_t)n;
    TRY(run_charge(r, pos, total));
    char *p = arena_alloc(r->ctx.values, (size_t)total, 1);
    if (p == NULL) return run_values_oom(r, pos);
    Bytes o = {p, (size_t)total};
    for (size_t k = 0; k < total; k += s.len) memcpy(&at(o, k), s.ptr, s.len);
    Str t = {p, (size_t)total};
    value_string(out, t);
    return OK;
}

static STATUS_MUST_USE Status bi_substitute(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str old = {0};
    Str new_text = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_SUBSTITUTE, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_SUBSTITUTE, &at(args, 1), S("what it replaces"), &old));
    TRY(builtin_string_arg(r, pos, BUILTIN_SUBSTITUTE, &at(args, 2), S("what it puts instead"), &new_text));
    bool missing_ok = false;
    if (args.len > 3) {
        Table *m = NULL;
        TRY(builtin_map_arg(r, pos, BUILTIN_SUBSTITUTE, &at(args, 3), &m));
        for (size_t k = 0; k < ValueMap_len(&m->map); k++) {
            Str key = ValueMap_key_at(&m->map, k);
            Value x = ValueMap_value_at(&m->map, k);
            if (!str_eq(key, S("missing_ok"))) return run_failf(r, pos, CLASS_ARGUMENT, "`substitute` takes `missing_ok`, not `{s}`", S("`substitute` takes `missing_ok`, not `"), key, S("`"));
            if (x.tag != VALUE_BOOL) return run_failf(r, pos, CLASS_TYPE, "`substitute`: its `missing_ok` must be true or false, not {s}", S("`substitute`: its `missing_ok` must be true or false, not "), value_type_name(&x));
            missing_ok = x.as.boolean;
        }
    }
    if (old.len == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`substitute` of \"\" is an error: there is nothing to replace"));
    TRY(builtin_charge_bytes(r, pos, s.len));
    Finder f;
    finder_init(&f, r, pos, old);
    size_t n = 0;
    size_t k = 0;
    for (;;) {
        bool found = false;
        size_t i = 0;
        TRY(finder_find(&f, s, k, &found, &i));
        if (!found) break;
        n++;
        k = i + old.len;
    }
    if (n == 0) {
        if (missing_ok) {
            value_string(out, s);
            return OK;
        }
        return run_failf(r, pos, CLASS_ARGUMENT, "`substitute`: \"{s}\" is not in the text, so nothing was replaced; `{{missing_ok = true}}` if a miss is expected", S("`substitute`: \""), str_sub(old, 0, old.len < 40 ? old.len : 40), S("\" is not in the text, so nothing was replaced; `{missing_ok = true}` if a miss is expected"));
    }
    if (new_text.len > 0 && n > SIZE_MAX / new_text.len) return run_budget_stop(&r->ctx, pos, BUDGET_ARENA);
    size_t total = s.len - n * old.len + n * new_text.len;
    TRY(run_charge(r, pos, total));
    Buf b = buf_make(r->ctx.values);
    if (buf_reserve(&b, total) != OK) return run_values_oom(r, pos);
    k = 0;
    for (;;) {
        bool found = false;
        size_t i = 0;
        TRY(finder_find(&f, s, k, &found, &i));
        if (!found) break;
        if (buf_str(&b, str_sub(s, k, i)) != OK || buf_str(&b, new_text) != OK) return run_values_oom(r, pos);
        k = i + old.len;
    }
    if (buf_str(&b, str_sub(s, k, s.len)) != OK) return run_values_oom(r, pos);
    value_string(out, buf_view(&b));
    return OK;
}

static STATUS_MUST_USE Status bi_path(Run *r, Pos pos, ConstValues args, Value *out) {
    uint64_t total = 0;
    for (size_t i = 0; i < args.len; i++) {
        const Value *a = &at(args, i);
        if (a->tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`path`'s part {d} is {s}, not a string", S("`path`'s part "), run_unum(r, (uint64_t)i + 1), S(" is "), value_type_name(a), S(", not a string"));
        if (a->as.string.len > 0) total += a->as.string.len + 1;
    }
    if (total > 0) total--;
    TRY(run_charge(r, pos, total));
    Buf b = buf_make(r->ctx.values);
    if (buf_reserve(&b, (size_t)total) != OK) return run_values_oom(r, pos);
    for (size_t i = 0; i < args.len; i++) {
        Str part = at(args, i).as.string;
        if (part.len == 0) continue;
        if (b.len > 0 && buf_byte(&b, '/') != OK) return run_values_oom(r, pos);
        if (buf_str(&b, part) != OK) return run_values_oom(r, pos);
    }
    value_string(out, buf_view(&b));
    return OK;
}

static STATUS_MUST_USE Status bi_basename(Run *r, Pos pos, ConstValues args, Value *out) {
    Str p = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_BASENAME, &at(args, 0), S("its path"), &p));
    TRY(run_charge(r, pos, p.len));
    size_t k = p.len;
    while (k > 0 && str_byte(p, k - 1) != '/') k--;
    value_string(out, str_sub(p, k, p.len));
    return OK;
}

static STATUS_MUST_USE Status bi_is_name(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_IS_NAME, &at(args, 0), S("its name"), &s));
    TRY(run_charge(r, pos, s.len));
    value_bool(out, cli_fat_name(s));
    return OK;
}

static STATUS_MUST_USE Status bi_strip_suffix(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str t = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_STRIP_SUFFIX, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_STRIP_SUFFIX, &at(args, 1), S("its suffix"), &t));
    TRY(run_charge(r, pos, t.len));
    bool ends = s.len >= t.len && str_eq(str_sub(s, s.len - t.len, s.len), t);
    value_string(out, ends ? str_sub(s, 0, s.len - t.len) : s);
    return OK;
}

// `between(s, a, b)`: the text after the first `a` and before the first `b`
// after it, or nil.
static STATUS_MUST_USE Status bi_between(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str a = {0};
    Str b = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_BETWEEN, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_BETWEEN, &at(args, 1), S("what it starts after"), &a));
    TRY(builtin_string_arg(r, pos, BUILTIN_BETWEEN, &at(args, 2), S("what it ends before"), &b));
    Finder fa;
    finder_init(&fa, r, pos, a);
    bool found = false;
    size_t i = 0;
    TRY(finder_find(&fa, s, 0, &found, &i));
    value_nil(out);
    if (!found) return OK;
    size_t start = i + a.len;
    if (b.len == 0) {
        TRY(run_charge(r, pos, s.len));
        value_string(out, str_sub(s, start, s.len));
        return OK;
    }
    Finder fb;
    finder_init(&fb, r, pos, b);
    size_t end = 0;
    TRY(finder_find(&fb, s, start, &found, &end));
    if (!found) return OK;
    TRY(builtin_charge_bytes(r, pos, end + b.len));
    value_string(out, str_sub(s, start, end));
    return OK;
}

// The match behind `like`: the classic walk that backtracks only to the last
// `*`. Every byte examined is a step, charged each 1024.
static STATUS_MUST_USE Status builtin_like(Run *r, Pos pos, Str s, Str p, bool *out) {
    size_t si = 0;
    size_t pi = 0;
    bool has_star = false;
    size_t star = 0;
    size_t mark = 0;
    uint64_t looked = 0;
    while (si < s.len) {
        looked++;
        if (looked == 1024) {
            TRY(run_charge(r, pos, looked));
            looked = 0;
        }
        uint8_t pc = pi < p.len ? str_byte(p, pi) : 0;
        if (pi < p.len && (pc == '?' || (pc != '*' && pc == str_byte(s, si)))) {
            si++;
            pi++;
        } else if (pi < p.len && pc == '*') {
            has_star = true;
            star = pi;
            mark = si;
            pi++;
        } else if (has_star) {
            pi = star + 1;
            mark++;
            si = mark;
        } else {
            TRY(run_charge(r, pos, looked));
            *out = false;
            return OK;
        }
    }
    while (pi < p.len && str_byte(p, pi) == '*') pi++;
    TRY(run_charge(r, pos, looked + p.len));
    *out = pi == p.len;
    return OK;
}

static STATUS_MUST_USE Status bi_like(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str p = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_LIKE, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_LIKE, &at(args, 1), S("its pattern"), &p));
    bool got = false;
    TRY(builtin_like(r, pos, s, p, &got));
    value_bool(out, got);
    return OK;
}

enum { BUILTIN_PATH_PARTS_MOST = 64 };

SLICE_TYPE(PathParts, Str);
SLICE_TYPE(Reach, bool);

// `s` split on `/` into `room`; false past its 64 parts.
static bool builtin_split_parts(Str s, PathParts room, size_t *n) {
    *n = 0;
    Str left = s;
    bool done = false;
    Str part;
    while (cli_split(&left, &done, '/', &part)) {
        if (*n == room.len) return false;
        at(room, *n) = part;
        (*n)++;
    }
    return true;
}

static STATUS_MUST_USE Status bi_match_path(Run *r, Pos pos, ConstValues args, Value *out) {
    Str subject = {0};
    Str pattern = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_MATCH_PATH, &at(args, 0), S("its path"), &subject));
    TRY(builtin_string_arg(r, pos, BUILTIN_MATCH_PATH, &at(args, 1), S("its pattern"), &pattern));
    Str ps_buf[BUILTIN_PATH_PARTS_MOST];
    Str qs_buf[BUILTIN_PATH_PARTS_MOST];
    PathParts ps = {ps_buf, BUILTIN_PATH_PARTS_MOST};
    PathParts qs = {qs_buf, BUILTIN_PATH_PARTS_MOST};
    size_t np = 0;
    size_t nq = 0;
    if (!builtin_split_parts(subject, ps, &np)) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`match_path`: a path of more than 64 parts"));
    if (!builtin_split_parts(pattern, qs, &nq)) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`match_path`: a pattern of more than 64 parts"));
    TRY(run_charge(r, pos, subject.len + pattern.len));
    // `can[j]`: whether the pattern's first parts so far match the path's
    // first j.
    bool can_buf[BUILTIN_PATH_PARTS_MOST + 1];
    bool next_buf[BUILTIN_PATH_PARTS_MOST + 1];
    Reach can = {can_buf, BUILTIN_PATH_PARTS_MOST + 1};
    Reach next = {next_buf, BUILTIN_PATH_PARTS_MOST + 1};
    for (size_t j = 0; j < can.len; j++) at(can, j) = false;
    at(can, 0) = true;
    for (size_t qi = 0; qi < nq; qi++) {
        Str part = at(qs, qi);
        for (size_t j = 0; j < next.len; j++) at(next, j) = false;
        if (str_eq(part, S("**"))) {
            bool any = false;
            for (size_t j = 0; j < np + 1; j++) {
                any = any || at(can, j);
                at(next, j) = any;
            }
        } else {
            for (size_t j = 1; j < np + 1; j++) {
                if (at(can, j - 1)) TRY(builtin_like(r, pos, at(ps, j - 1), part, &at(next, j)));
            }
        }
        for (size_t j = 0; j < can.len; j++) at(can, j) = at(next, j);
    }
    value_bool(out, at(can, np));
    return OK;
}

// ── regular expressions ──

// The pattern's compiled program from the run's cache, compiled and cached if
// absent, at a step a byte (and a byte more to fold case). `who` names the
// call.
static STATUS_MUST_USE Status builtin_program(Run *r, Pos pos, Str who, Str pattern, RegexOptions options, RegexCached **out) {
    RegexCached *got = regex_cache_get(&r->patterns, pattern, options);
    if (got != NULL) {
        *out = got;
        return OK;
    }
    if (pattern.len > REGEX_PATTERN_MOST) return run_failf(r, pos, CLASS_ARGUMENT, "`{s}`'s pattern is {d} bytes, and one may be at most {d}", S("`"), who, S("`'s pattern is "), run_unum(r, pattern.len), S(" bytes, and one may be at most 256"));
    TRY(run_charge(r, pos, pattern.len + 1 + (options.nocase ? pattern.len : 0)));
    RegexRefusal refused;
    memset(&refused, 0, sizeof refused);
    size_t limit = run_sat_sub(r->ctx.budgets.machine_bytes, r->machine->held + regex_cache_held(&r->patterns));
    if (regex_cache_put(&r->patterns, pattern, options, limit, &refused, out) != OK) {
        if (!refused.refused) return run_machine_oom(r, pos);
        return run_failf(r, pos, CLASS_ARGUMENT, "`{s}`'s pattern, at its byte {d}: {s}", S("`"), who, S("`'s pattern, at its byte "), run_unum(r, (uint64_t)refused.at + 1), S(": "), refused.message);
    }
    return OK;
}

// An option that is true or false; `who` and `name` say whose and which.
static STATUS_MUST_USE Status builtin_flag_option(Run *r, Pos pos, Str who, Str name, const Value *x, bool *out) {
    if (x->tag == VALUE_BOOL) {
        *out = x->as.boolean;
        return OK;
    }
    return run_failf(r, pos, CLASS_TYPE, "`{s}`: its `{s}` must be true or false, not {s}", S("`"), who, S("`: its `"), name, S("` must be true or false, not "), value_type_name(x));
}

// The options map a call may end with: `{nocase = true}`, and `re_find`'s
// `from`.
static STATUS_MUST_USE Status builtin_regex_options(Run *r, Pos pos, Builtin b, ConstValues args, RegexOptions *o) {
    o->nocase = false;
    size_t k = b == BUILTIN_RE_REPLACE ? 3 : 2;
    if (args.len <= k) return OK;
    Table *m = NULL;
    TRY(builtin_map_arg(r, pos, b, &at(args, k), &m));
    for (size_t i = 0; i < ValueMap_len(&m->map); i++) {
        Str key = ValueMap_key_at(&m->map, i);
        Value x = ValueMap_value_at(&m->map, i);
        if (b == BUILTIN_RE_FIND && str_eq(key, S("from"))) continue;
        if (!str_eq(key, S("nocase"))) {
            // `re_find_all`'s stop has `opts_only`'s class (a fail) and
            // wording, unlike the other regex builtins'.
            if (b == BUILTIN_RE_FIND_ALL) return run_failf(r, pos, CLASS_FAIL, "`re_find_all` takes `nocase`, not `{s}`", S("`re_find_all` takes `nocase`, not `"), key, S("`"));
            return run_failf(r, pos, CLASS_ARGUMENT, "`{s}` takes `{s}`, not `{s}`", S("`"), builtin_name(b), b == BUILTIN_RE_FIND ? S("` takes `nocase` or `from`, not `") : S("` takes `nocase`, not `"), key, S("`"));
        }
        TRY(builtin_flag_option(r, pos, builtin_name(b), S("nocase"), &x, &o->nocase));
    }
    return OK;
}

// The searches of one call, charged as they go: each position examined at the
// program's states, and the bytes skipped a step for each 16 across all of
// them. `done` charges the part of 16 left over.
typedef struct {
    uint64_t skipped;
    uint64_t paid;
} Searches;

static STATUS_MUST_USE Status searches_from(Searches *each, Run *r, Pos pos, RegexCached *c, Str s, size_t from, bool *found, RegexFound *match) {
    uint64_t per = c->program.states;
    uint64_t left = run_sat_sub(r->ctx.budgets.steps, r->steps);
    uint64_t sk = left > UINT64_MAX - 2 ? UINT64_MAX : left + 2;
    RegexMost most = {left / per + 1, sk > UINT64_MAX / BUILTIN_BYTES_PER_STEP ? UINT64_MAX : sk * BUILTIN_BYTES_PER_STEP};
    RegexSearched got = regex_search_within(&c->program, &c->scratch, s, from, most);
    each->skipped += got.skipped;
    uint64_t owed = each->skipped / BUILTIN_BYTES_PER_STEP - each->paid;
    each->paid += owed;
    TRY(run_charge(r, pos, got.positions * per + owed));
    *found = got.found;
    *match = got.match;
    return OK;
}

static STATUS_MUST_USE Status searches_done(const Searches *each, Run *r, Pos pos) {
    if (each->skipped % BUILTIN_BYTES_PER_STEP != 0) return run_charge(r, pos, 1);
    return OK;
}

static Str builtin_group_text(Str s, const RegexGroup *g) {
    return g->matched ? str_sub(s, g->start, g->end) : S("");
}

// A match as `re_find` gives it: `{at, text[, groups, group]}`.
static STATUS_MUST_USE Status builtin_match_value(Run *r, Pos pos, const RegexCached *c, Str s, const RegexFound *f, Value *out) {
    Table *m = NULL;
    TRY(run_new_table(r, pos, &m));
    m->kind = TABLE_MAP;
    TRY(run_map_put_int(r, pos, m, S("at"), (int64_t)f->start + 1));
    TRY(run_map_put_str(r, pos, m, S("text"), str_sub(s, f->start, f->end)));
    size_t n = c->program.groups;
    if (n > 0) {
        Table *gs = NULL;
        TRY(builtin_new_list(r, pos, &gs));
        SLICE_TYPE(Groups, const RegexGroup);
        Groups all = {f->groups, REGEX_GROUPS_MOST};
        for (size_t k = 0; k < n; k++) TRY(builtin_push_str(r, pos, gs, builtin_group_text(s, &at(all, k))));
        Value gv;
        value_table(&gv, gs);
        TRY(run_map_put(r, pos, m, S("groups"), &gv));
        TRY(run_map_put(r, pos, m, S("group"), &at(gs->list, 0)));
    }
    value_table(out, m);
    return OK;
}

static STATUS_MUST_USE Status bi_re_find(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str pattern = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_RE_FIND, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_RE_FIND, &at(args, 1), S("its pattern"), &pattern));
    size_t from = 0;
    if (args.len > 2) {
        Table *m = NULL;
        TRY(builtin_map_arg(r, pos, BUILTIN_RE_FIND, &at(args, 2), &m));
        Value x;
        if (ValueMap_get(&m->map, S("from"), &x)) {
            if (x.tag != VALUE_INT) return run_failf(r, pos, CLASS_TYPE, "`re_find`: its `from` must be an integer, not {s}", S("`re_find`: its `from` must be an integer, not "), value_type_name(&x));
            int64_t n = x.as.integer;
            if (n < 1 || n > (int64_t)s.len + 1) return run_failf(r, pos, CLASS_ARGUMENT, "`re_find`: a `from` of {d}, and it must be 1 to {d} (the string's length and one)", S("`re_find`: a `from` of "), run_num(r, n), S(", and it must be 1 to "), run_unum(r, (uint64_t)s.len + 1), S(" (the string's length and one)"));
            from = (size_t)(n - 1);
        }
    }
    RegexOptions o;
    TRY(builtin_regex_options(r, pos, BUILTIN_RE_FIND, args, &o));
    RegexCached *c = NULL;
    TRY(builtin_program(r, pos, S("re_find"), pattern, o, &c));
    Searches each = {0, 0};
    bool found = false;
    RegexFound f;
    TRY(searches_from(&each, r, pos, c, s, from, &found, &f));
    TRY(searches_done(&each, r, pos));
    if (!found) {
        value_nil(out);
        return OK;
    }
    return builtin_match_value(r, pos, c, s, &f, out);
}

static STATUS_MUST_USE Status bi_re_find_all(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str pattern = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_RE_FIND_ALL, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_RE_FIND_ALL, &at(args, 1), S("its pattern"), &pattern));
    RegexOptions o;
    TRY(builtin_regex_options(r, pos, BUILTIN_RE_FIND_ALL, args, &o));
    RegexCached *c = NULL;
    TRY(builtin_program(r, pos, S("re_find_all"), pattern, o, &c));
    // A list once it holds a match, `{}` while it holds none.
    Table *t = NULL;
    TRY(run_new_table(r, pos, &t));
    Searches each = {0, 0};
    size_t k = 0;
    while (k <= s.len) {
        bool found = false;
        RegexFound f;
        TRY(searches_from(&each, r, pos, c, s, k, &found, &f));
        if (!found) break;
        TRY(run_charge(r, pos, 1));
        t->kind = TABLE_LIST;
        Value mv;
        TRY(builtin_match_value(r, pos, c, s, &f, &mv));
        TRY(run_list_push(r, pos, t, &mv));
        k = f.start + (f.end - f.start > 1 ? f.end - f.start : 1);
    }
    TRY(searches_done(&each, r, pos));
    value_table(out, t);
    return OK;
}

static STATUS_MUST_USE Status bi_re_match(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str pattern = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_RE_MATCH, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_RE_MATCH, &at(args, 1), S("its pattern"), &pattern));
    RegexOptions o;
    TRY(builtin_regex_options(r, pos, BUILTIN_RE_MATCH, args, &o));
    RegexCached *c = NULL;
    TRY(builtin_program(r, pos, S("re_match"), pattern, o, &c));
    Searches each = {0, 0};
    bool found = false;
    RegexFound f;
    TRY(searches_from(&each, r, pos, c, s, 0, &found, &f));
    TRY(searches_done(&each, r, pos));
    value_bool(out, found);
    return OK;
}

static STATUS_MUST_USE Status bi_re_replace(Run *r, Pos pos, ConstValues args, Value *out) {
    Str s = {0};
    Str pattern = {0};
    Str with = {0};
    TRY(builtin_string_arg(r, pos, BUILTIN_RE_REPLACE, &at(args, 0), S("its first argument"), &s));
    TRY(builtin_string_arg(r, pos, BUILTIN_RE_REPLACE, &at(args, 1), S("its pattern"), &pattern));
    TRY(builtin_string_arg(r, pos, BUILTIN_RE_REPLACE, &at(args, 2), S("its replacement"), &with));
    RegexOptions o;
    TRY(builtin_regex_options(r, pos, BUILTIN_RE_REPLACE, args, &o));
    RegexCached *c = NULL;
    TRY(builtin_program(r, pos, S("re_replace"), pattern, o, &c));
    size_t groups = c->program.groups;
    // How often each group is named, and the bytes the names take.
    size_t uses_buf[REGEX_GROUPS_MOST] = {0};
    SLICE_TYPE(Uses, size_t);
    Uses uses = {uses_buf, REGEX_GROUPS_MOST};
    size_t named = 0;
    for (size_t k = 0; k + 1 < with.len; k++) {
        uint8_t d = str_byte(with, k + 1);
        if (str_byte(with, k) != '\\' || d < '1' || d > '9') continue;
        size_t g = (size_t)(d - '1');
        if (g >= groups) {
            Str digit = str_sub(with, k + 1, k + 2);
            if (groups == 0) return run_failf(r, pos, CLASS_ARGUMENT, "`re_replace`'s replacement has `\\{c}`, and its pattern has no capture group", S("`re_replace`'s replacement has `\\"), digit, S("`, and its pattern has no capture group"));
            return run_failf(r, pos, CLASS_ARGUMENT, "`re_replace`'s replacement has `\\{c}`, and its pattern has only {d} capture group{s}", S("`re_replace`'s replacement has `\\"), digit, S("`, and its pattern has only "), run_unum(r, groups), S(" capture group"), groups == 1 ? S("") : S("s"));
        }
        at(uses, g)++;
        named += 2;
        k++;
    }
    TRY(builtin_charge_bytes(r, pos, s.len + with.len));
    Buf b = buf_make(r->ctx.values);
    size_t k = 0;
    bool replaced = false;
    Searches each = {0, 0};
    while (k <= s.len) {
        bool found = false;
        RegexFound f;
        TRY(searches_from(&each, r, pos, c, s, k, &found, &f));
        if (!found) break;
        replaced = true;
        SLICE_TYPE(Groups, const RegexGroup);
        Groups gs = {f.groups, REGEX_GROUPS_MOST};
        // A step a byte this match adds, charged before it is written.
        uint64_t grown = 0;
        for (size_t g = 0; g < groups; g++) {
            const RegexGroup *x = &at(gs, g);
            uint64_t len = x->matched ? x->end - x->start : 0;
            if (len > 0 && at(uses, g) > (UINT64_MAX - grown) / len) return run_budget_stop(&r->ctx, pos, BUDGET_ARENA);
            grown += at(uses, g) * len;
        }
        TRY(run_charge(r, pos, (f.start - k) + with.len - named + grown + 1));
        if (buf_str(&b, str_sub(s, k, f.start)) != OK) return run_values_oom(r, pos);
        size_t i = 0;
        while (i < with.len) {
            uint8_t ch = str_byte(with, i);
            uint8_t d = i + 1 < with.len ? str_byte(with, i + 1) : 0;
            if (ch == '\\' && i + 1 < with.len && d >= '1' && d <= '9') {
                const RegexGroup *x = &at(gs, (size_t)(d - '1'));
                if (x->matched && buf_str(&b, str_sub(s, x->start, x->end)) != OK) return run_values_oom(r, pos);
                i += 2;
            } else {
                if (buf_byte(&b, (char)ch) != OK) return run_values_oom(r, pos);
                i++;
            }
        }
        if (f.end > f.start) {
            k = f.end;
        } else {
            if (f.end < s.len && buf_byte(&b, (char)str_byte(s, f.end)) != OK) return run_values_oom(r, pos);
            k = f.end + 1;
        }
    }
    TRY(searches_done(&each, r, pos));
    if (!replaced) {
        value_string(out, s);
        return OK;
    }
    if (k < s.len) {
        TRY(run_charge(r, pos, s.len - k));
        if (buf_str(&b, str_sub(s, k, s.len)) != OK) return run_values_oom(r, pos);
    }
    value_string(out, buf_view(&b));
    return OK;
}
