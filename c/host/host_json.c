// host_json.c: the Linux host's JSON, `host("json_read", s, opts?)` and
// `host("json_write", v)`.
//
// Shale's values are not JSON's, and where they differ the reader says so:
// an object is a map and an array a list, `{}` and `[]` both the empty
// table; a number with a fraction or an exponent is kept as its text, a
// string; an integer past 64 bits is refused; a member that is `null` is left
// out of its object and a `null` in an array refused, unless `{null = v}`
// stands a value in; a key twice in one object is refused. A step is charged
// for every 16 bytes read and one for each value made.
//
// Include run.h and builtins.c first.

// The most nesting a text may have unless `depth` says less; `depth` may not
// say more.
enum {
    JSON_DEPTH_DEFAULT = 64,
    JSON_DEPTH_MAX = 512,
};

// A text being read.
typedef struct {
    Run *r;
    Pos pos;
    Str s;
    size_t i;
    size_t depth_left;
    // `{null = v}`'s value, when given.
    bool has_null;
    Value null_as;
} JsonReader;

static void json_hex2(Msg *m, uint8_t c) {
    static const char hex[] = "0123456789abcdef";
    Str digits = {hex, 16};
    char pair[2] = {at(digits, c >> 4), at(digits, c & 15)};
    msg_str(m, (Str){pair, 2});
}

// Zig's `failAt`: `said`, then where in the text it is, by byte and by line
// and column (each byte before it a column, a newline a line).
static STATUS_MUST_USE Status json_fail_at(JsonReader *jr, size_t at_, Msg *said) {
    Run *r = jr->r;
    if (said->s != OK) return run_values_oom(r, jr->pos);
    uint64_t line = 1;
    uint64_t col = 1;
    size_t end = at_ < jr->s.len ? at_ : jr->s.len;
    for (size_t k = 0; k < end; k++) {
        if (at(jr->s, k) == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    return run_failf(r, jr->pos, CLASS_ARGUMENT, "`json_read`: {s} at byte {d} (line {d}, column {d})",
                     S("`json_read`: "), buf_view(&said->b), S(" at byte "), run_unum(r, at_), S(" (line "), run_unum(r, line), S(", column "), run_unum(r, col), S(")"));
}

static STATUS_MUST_USE Status json_fail_lit(JsonReader *jr, size_t at_, Str said) {
    Msg m;
    msg_init(&m, jr->r->ctx.values);
    msg_str(&m, said);
    return json_fail_at(jr, at_, &m);
}

static bool json_more(const JsonReader *jr) {
    return jr->i < jr->s.len;
}

static uint8_t json_byte(const JsonReader *jr) {
    return (uint8_t)at(jr->s, jr->i);
}

static void json_skip_space(JsonReader *jr) {
    while (json_more(jr)) {
        uint8_t c = json_byte(jr);
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
        jr->i++;
    }
}

static Str json_shown(uint8_t c) {
    switch (c) {
    case '{': return S("`{`");
    case '}': return S("`}`");
    case '[': return S("`[`");
    case ']': return S("`]`");
    case ',': return S("a comma");
    case ':': return S("a colon");
    case '"': return S("a quote");
    default: return S("a byte");
    }
}

static bool json_is_digit(uint8_t c) {
    return c >= '0' && c <= '9';
}

// Whether `s` is UTF-8: every sequence whole, none overlong, no
// surrogate half, nothing past U+10FFFF.
static bool json_utf8_valid(Str s) {
    size_t i = 0;
    while (i < s.len) {
        size_t n = builtin_utf8_length((uint8_t)at(s, i));
        if (n == 0 || n > s.len - i) return false;
        if (!builtin_utf8_valid(str_sub(s, i, i + n))) return false;
        i += n;
    }
    return true;
}

// Four bytes as Zig's `std.fmt.parseInt(u16, s, 16)` reads them: an optional
// `+` or `-`, then hex digits with `_` between them (never first or last).
// A `-` before any digit but zero is an overflow for a u16. False for what
// Zig refuses.
static bool json_parse_hex4(Str s, uint32_t *out) {
    bool neg = false;
    size_t from = 0;
    if (at(s, 0) == '+' || at(s, 0) == '-') {
        neg = at(s, 0) == '-';
        from = 1;
    }
    Str digits = str_sub(s, from, s.len);
    if (digits.len == 0) return false;
    if (at(digits, 0) == '_' || at(digits, digits.len - 1) == '_') return false;
    uint32_t v = 0;
    for (size_t k = 0; k < digits.len; k++) {
        uint8_t c = (uint8_t)at(digits, k);
        if (c == '_') continue;
        int d = lex_hex_digit(c);
        if (d < 0) return false;
        v = v * 16 + (uint32_t)d;
    }
    if (neg && v != 0) return false;
    *out = v;
    return true;
}

// The four hex digits at `jr->i`, after a `\u` at `at_`.
static STATUS_MUST_USE Status json_hex4(JsonReader *jr, size_t at_, uint32_t *out) {
    if (jr->s.len - jr->i < 4) return json_fail_lit(jr, at_, S("`\\u` needs four hex digits"));
    if (!json_parse_hex4(str_sub(jr->s, jr->i, jr->i + 4), out)) return json_fail_lit(jr, at_, S("`\\u` needs four hex digits"));
    jr->i += 4;
    return OK;
}

// The string at `jr->i` (its opening quote), its escapes decoded to UTF-8.
static STATUS_MUST_USE Status json_string(JsonReader *jr, Str *out) {
    Run *r = jr->r;
    size_t open = jr->i;
    jr->i++;
    Buf b = buf_make(r->ctx.values);
    while (true) {
        if (!json_more(jr)) return json_fail_lit(jr, open, S("a string that never closes"));
        uint8_t c = json_byte(jr);
        if (c == '"') {
            jr->i++;
            if (!json_utf8_valid(buf_view(&b))) return json_fail_lit(jr, open, S("a string that is not UTF-8"));
            *out = buf_view(&b);
            return OK;
        }
        if (c < 0x20) {
            Msg m;
            msg_init(&m, r->ctx.values);
            msg_str(&m, S("a control byte (0x"));
            json_hex2(&m, c);
            msg_str(&m, S(") inside a string, which JSON writes as an escape"));
            return json_fail_at(jr, jr->i, &m);
        }
        if (c != '\\') {
            if (buf_byte(&b, (char)c) != OK) return run_values_oom(r, jr->pos);
            jr->i++;
            continue;
        }
        if (jr->i + 1 >= jr->s.len) return json_fail_lit(jr, jr->i, S("a string that ends in a backslash"));
        uint8_t e = (uint8_t)at(jr->s, jr->i + 1);
        jr->i += 2;
        char plain = 0;
        switch (e) {
        case '"': plain = '"'; break;
        case '\\': plain = '\\'; break;
        case '/': plain = '/'; break;
        case 'b': plain = 8; break;
        case 'f': plain = 12; break;
        case 'n': plain = '\n'; break;
        case 'r': plain = '\r'; break;
        case 't': plain = '\t'; break;
        case 'u': break;
        default: {
            Msg m;
            msg_init(&m, r->ctx.values);
            msg_str(&m, S("the escape `\\"));
            char one = (char)e;
            msg_str(&m, (Str){&one, 1});
            msg_str(&m, S("`, which JSON has not"));
            return json_fail_at(jr, jr->i - 2, &m);
        }
        }
        if (e != 'u') {
            if (buf_byte(&b, plain) != OK) return run_values_oom(r, jr->pos);
            continue;
        }
        uint32_t cp = 0;
        TRY(json_hex4(jr, jr->i - 2, &cp));
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            // A high surrogate takes the low one after it.
            if (!str_starts_with(str_sub(jr->s, jr->i, jr->s.len), S("\\u"))) return json_fail_lit(jr, jr->i - 6, S("a high surrogate with no low one after it"));
            jr->i += 2;
            uint32_t lo = 0;
            TRY(json_hex4(jr, jr->i - 2, &lo));
            if (lo < 0xDC00 || lo > 0xDFFF) return json_fail_lit(jr, jr->i - 6, S("a high surrogate followed by no low one"));
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return json_fail_lit(jr, jr->i - 6, S("a low surrogate with no high one before it"));
        }
        if (buf_utf8(&b, cp) != OK) return run_values_oom(r, jr->pos);
    }
}

// Zig's `parseInt(i64, text, 10)` of a `-` and digits alone: false past 64
// bits. The magnitude is built negative, so the smallest i64 fits.
static bool json_parse_i64(Str text, int64_t *out) {
    bool neg = at(text, 0) == '-';
    int64_t v = 0;
    for (size_t k = neg ? 1 : 0; k < text.len; k++) {
        int64_t d = at(text, k) - '0';
        if (v < (INT64_MIN + d) / 10) return false;
        v = v * 10 - d;
    }
    if (!neg) {
        if (v == INT64_MIN) return false;
        v = -v;
    }
    *out = v;
    return true;
}

static STATUS_MUST_USE Status json_number(JsonReader *jr, Value *out) {
    size_t start = jr->i;
    if (json_byte(jr) == '-') jr->i++;
    if (!json_more(jr) || !json_is_digit(json_byte(jr))) return json_fail_lit(jr, start, S("a number needs a digit after its sign"));
    if (json_byte(jr) == '0' && jr->i + 1 < jr->s.len && json_is_digit((uint8_t)at(jr->s, jr->i + 1))) return json_fail_lit(jr, start, S("a number with a leading zero"));
    while (json_more(jr) && json_is_digit(json_byte(jr))) jr->i++;
    bool whole = true;
    if (json_more(jr) && json_byte(jr) == '.') {
        whole = false;
        jr->i++;
        if (!json_more(jr) || !json_is_digit(json_byte(jr))) return json_fail_lit(jr, start, S("a number needs a digit after its point"));
        while (json_more(jr) && json_is_digit(json_byte(jr))) jr->i++;
    }
    if (json_more(jr) && (json_byte(jr) == 'e' || json_byte(jr) == 'E')) {
        whole = false;
        jr->i++;
        if (json_more(jr) && (json_byte(jr) == '+' || json_byte(jr) == '-')) jr->i++;
        if (!json_more(jr) || !json_is_digit(json_byte(jr))) return json_fail_lit(jr, start, S("a number needs a digit in its exponent"));
        while (json_more(jr) && json_is_digit(json_byte(jr))) jr->i++;
    }
    Str text = str_sub(jr->s, start, jr->i);
    if (!whole) {
        Str kept = S("");
        if (!arena_dup(jr->r->ctx.values, text, &kept)) return run_values_oom(jr->r, jr->pos);
        value_string(out, kept);
        return OK;
    }
    int64_t n = 0;
    if (!json_parse_i64(text, &n)) {
        Msg m;
        msg_init(&m, jr->r->ctx.values);
        msg_str(&m, S("the integer "));
        msg_str(&m, text);
        msg_str(&m, S(" is past 64 bits"));
        return json_fail_at(jr, start, &m);
    }
    value_int(out, n);
    return OK;
}

static STATUS_MUST_USE Status json_word(JsonReader *jr, Str w, const Value *v, Value *out) {
    if (!str_starts_with(str_sub(jr->s, jr->i, jr->s.len), w)) {
        Msg m;
        msg_init(&m, jr->r->ctx.values);
        msg_str(&m, S("`"));
        msg_str(&m, w);
        msg_str(&m, S("` was expected"));
        return json_fail_at(jr, jr->i, &m);
    }
    jr->i += w.len;
    *out = *v;
    return OK;
}

static STATUS_MUST_USE Status json_enter(JsonReader *jr) {
    if (jr->depth_left == 0) return json_fail_lit(jr, jr->i, S("the text nests past its depth budget"));
    jr->depth_left--;
    return OK;
}

static STATUS_MUST_USE Status json_value(JsonReader *jr, Value *out);

static STATUS_MUST_USE Status json_fail_after(JsonReader *jr, Str before) {
    Msg m;
    msg_init(&m, jr->r->ctx.values);
    msg_str(&m, before);
    msg_str(&m, json_shown(json_byte(jr)));
    return json_fail_at(jr, jr->i, &m);
}

static STATUS_MUST_USE Status json_object_members(JsonReader *jr, Table *t) {
    Run *r = jr->r;
    while (true) {
        json_skip_space(jr);
        if (!json_more(jr) || json_byte(jr) != '"') return json_fail_lit(jr, jr->i, S("a member's name, a string, was expected"));
        size_t key_at = jr->i;
        Str key = S("");
        TRY(json_string(jr, &key));
        json_skip_space(jr);
        if (!json_more(jr) || json_byte(jr) != ':') return json_fail_lit(jr, jr->i, S("a colon after a member's name was expected"));
        jr->i++;
        Value v;
        TRY(json_value(jr, &v));
        Value was;
        if (ValueMap_get(&t->map, key, &was)) {
            Msg m;
            msg_init(&m, r->ctx.values);
            msg_str(&m, S("the key \""));
            msg_str(&m, key);
            msg_str(&m, S("\" is in this object twice"));
            return json_fail_at(jr, key_at, &m);
        }
        if (v.tag == VALUE_NIL && jr->has_null) v = jr->null_as;
        if (v.tag != VALUE_NIL) {
            TRY(run_map_put(r, jr->pos, t, key, &v));
            t->kind = TABLE_MAP;
        }
        json_skip_space(jr);
        if (!json_more(jr)) return json_fail_lit(jr, jr->i, S("the text ended inside an object"));
        if (json_byte(jr) == ',') {
            jr->i++;
            continue;
        }
        if (json_byte(jr) == '}') {
            jr->i++;
            return OK;
        }
        return json_fail_after(jr, S("a comma or `}` was expected, not "));
    }
}

// Opens the object or array whose first byte is at the cursor: one level
// deeper, its table made into `*out` and `*t`, and `*closed` true when
// `close` follows at once, past it. The caller reads what it holds and comes
// back out a level.
static STATUS_MUST_USE Status json_open(JsonReader *jr, char close, Value *out, Table **t, bool *closed) {
    TRY(json_enter(jr));
    jr->i++;
    TRY(run_new_table(jr->r, jr->pos, t));
    value_table(out, *t);
    json_skip_space(jr);
    *closed = json_more(jr) && json_byte(jr) == close;
    if (*closed) jr->i++;
    return OK;
}

static STATUS_MUST_USE Status json_object(JsonReader *jr, Value *out) {
    Table *t = NULL;
    bool closed = false;
    TRY(json_open(jr, '}', out, &t, &closed));
    Status s = closed ? OK : json_object_members(jr, t);
    jr->depth_left++;
    return s;
}

static STATUS_MUST_USE Status json_array_items(JsonReader *jr, Table *t) {
    while (true) {
        json_skip_space(jr);
        size_t at_ = jr->i;
        Value v;
        TRY(json_value(jr, &v));
        if (v.tag == VALUE_NIL) {
            if (!jr->has_null) return json_fail_lit(jr, at_, S("a null in an array, which a Shale list cannot hold: `{null = v}` names a value to stand for it"));
            v = jr->null_as;
        }
        TRY(run_list_push(jr->r, jr->pos, t, &v));
        t->kind = TABLE_LIST;
        json_skip_space(jr);
        if (!json_more(jr)) return json_fail_lit(jr, jr->i, S("the text ended inside an array"));
        if (json_byte(jr) == ',') {
            jr->i++;
            continue;
        }
        if (json_byte(jr) == ']') {
            jr->i++;
            return OK;
        }
        return json_fail_after(jr, S("a comma or `]` was expected, not "));
    }
}

static STATUS_MUST_USE Status json_array(JsonReader *jr, Value *out) {
    Table *t = NULL;
    bool closed = false;
    TRY(json_open(jr, ']', out, &t, &closed));
    Status s = closed ? OK : json_array_items(jr, t);
    jr->depth_left++;
    return s;
}

// One value, `null` given back as nil for the caller to place. A step is
// charged for each.
// DRAGON: the process's stack. Each array or object nests a call of this.
// REMEDY: `json_enter` refuses past `depth`, at most JSON_DEPTH_MAX (512),
// before the call that would nest; a frame here is a few hundred bytes.
static STATUS_MUST_USE Status json_value(JsonReader *jr, Value *out) {
    json_skip_space(jr);
    if (!json_more(jr)) return json_fail_lit(jr, jr->i, S("a value was expected and the text ended"));
    TRY(run_charge(jr->r, jr->pos, 1));
    uint8_t c = json_byte(jr);
    Value v;
    switch (c) {
    case '{': return json_object(jr, out);
    case '[': return json_array(jr, out);
    case '"': {
        Str s = S("");
        TRY(json_string(jr, &s));
        value_string(out, s);
        return OK;
    }
    case 't':
        value_bool(&v, true);
        return json_word(jr, S("true"), &v, out);
    case 'f':
        value_bool(&v, false);
        return json_word(jr, S("false"), &v, out);
    case 'n':
        value_nil(&v);
        return json_word(jr, S("null"), &v, out);
    default: break;
    }
    if (c == '-' || json_is_digit(c)) return json_number(jr, out);
    Msg m;
    msg_init(&m, jr->r->ctx.values);
    msg_str(&m, S("a value was expected, not "));
    msg_str(&m, json_shown(c));
    msg_str(&m, S(" (0x"));
    json_hex2(&m, c);
    msg_str(&m, S(")"));
    return json_fail_at(jr, jr->i, &m);
}

// `json_read`'s options into `jr`: `depth`, `size` and `null`, each checked
// in the map's order.
static STATUS_MUST_USE Status json_read_options(Run *r, Pos pos, const Value *opts, JsonReader *jr, uint64_t *size) {
    if (opts->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`json_read`'s options are a map, not {s}", S("`json_read`'s options are a map, not "), value_type_name(opts));
    Table *t = opts->as.table;
    if (t->kind == TABLE_LIST) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`json_read`'s options are a map, not a list"));
    uint64_t cap = r->ctx.budgets.call_bytes;
    for (size_t k = 0; k < ValueMap_len(&t->map); k++) {
        Str key = ValueMap_key_at(&t->map, k);
        Value x = ValueMap_value_at(&t->map, k);
        if (str_eq(key, S("depth")) || str_eq(key, S("size"))) {
            if (x.tag != VALUE_INT) return run_failf(r, pos, CLASS_TYPE, "`json_read`'s `{s}` is an integer, not {s}", S("`json_read`'s `"), key, S("` is an integer, not "), value_type_name(&x));
            int64_t n = x.as.integer;
            if (str_eq(key, S("depth"))) {
                if (n < 1 || n > JSON_DEPTH_MAX) return run_failf(r, pos, CLASS_ARGUMENT, "`json_read`'s `depth` is 1 to {d}, not {d}", S("`json_read`'s `depth` is 1 to 512, not "), run_num(r, n));
                jr->depth_left = (size_t)n;
            } else {
                if (n < 0 || (uint64_t)n > cap) {
                    return run_failf(r, pos, CLASS_ARGUMENT, "`json_read`'s `size` is 0 to the call bytes cap, {d}, not {d}",
                                     S("`json_read`'s `size` is 0 to the call bytes cap, "), run_unum(r, cap), S(", not "), run_num(r, n));
                }
                *size = (uint64_t)n;
            }
        } else if (str_eq(key, S("null"))) {
            jr->has_null = true;
            jr->null_as = x;
        } else {
            return run_failf(r, pos, CLASS_ARGUMENT, "`json_read` takes `depth`, `size` and `null`, not `{s}`", S("`json_read` takes `depth`, `size` and `null`, not `"), key, S("`"));
        }
    }
    return OK;
}

// `host("json_read", s, opts?)`: the text's one value as Shale's.
static STATUS_MUST_USE Status host_json_read(Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len < 1 || args.len > 2) return run_failf(r, pos, CLASS_ARGUMENT, "`json_read` takes a text and options, given {d} arguments", S("`json_read` takes a text and options, given "), run_unum(r, args.len), S(" arguments"));
    const Value *text = &at(args, 0);
    if (text->tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`json_read` takes a text, not {s}", S("`json_read` takes a text, not "), value_type_name(text));
    Str s = text->as.string;
    JsonReader jr;
    memset(&jr, 0, sizeof jr);
    jr.r = r;
    jr.pos = pos;
    jr.s = s;
    jr.depth_left = JSON_DEPTH_DEFAULT;
    uint64_t size = r->ctx.budgets.call_bytes;
    if (args.len == 2) TRY(json_read_options(r, pos, &at(args, 1), &jr, &size));
    if (s.len > size) {
        return run_failf(r, pos, CLASS_ARGUMENT, "`json_read`: the text is {d} bytes, past its size budget of {d}",
                         S("`json_read`: the text is "), run_unum(r, s.len), S(" bytes, past its size budget of "), run_unum(r, size));
    }
    TRY(builtin_charge_bytes(r, pos, s.len));
    TRY(json_value(&jr, out));
    json_skip_space(&jr);
    if (json_more(&jr)) return json_fail_lit(&jr, jr.i, S("more after the value: one text is one value"));
    return OK;
}

static STATUS_MUST_USE Status json_emit_string(Run *r, Pos pos, Buf *b, Str s) {
    if (!json_utf8_valid(s)) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`json_write`: a string that is not UTF-8, which JSON cannot hold"));
    Status w = buf_byte(b, '"');
    for (size_t k = 0; w == OK && k < s.len; k++) {
        uint8_t c = (uint8_t)at(s, k);
        switch (c) {
        case '"': w = buf_lit(b, "\\\""); break;
        case '\\': w = buf_lit(b, "\\\\"); break;
        case '\n': w = buf_lit(b, "\\n"); break;
        case '\r': w = buf_lit(b, "\\r"); break;
        case '\t': w = buf_lit(b, "\\t"); break;
        default:
            if (c < 0x20) {
                static const char hex[] = "0123456789abcdef";
                Str digits = {hex, 16};
                w = buf_lit(b, "\\u00");
                if (w == OK) w = buf_byte(b, at(digits, c >> 4));
                if (w == OK) w = buf_byte(b, at(digits, c & 15));
            } else {
                w = buf_byte(b, (char)c);
            }
        }
    }
    if (w == OK) w = buf_byte(b, '"');
    if (w != OK) return run_values_oom(r, pos);
    return OK;
}

// `v` as compact JSON onto `b`, a step a value.
// DRAGON: the process's stack. Each table nests a call of this, and a table
// can hold itself.
// REMEDY: `depth_left` starts at JSON_DEPTH_MAX, one less a level, and a
// table reached with none left is refused before it nests another call.
static STATUS_MUST_USE Status json_emit(Run *r, Pos pos, Buf *b, const Value *v, size_t depth_left) {
    TRY(run_charge(r, pos, 1));
    Status w = OK;
    switch (v->tag) {
    case VALUE_NIL: w = buf_lit(b, "null"); break;
    case VALUE_BOOL: w = v->as.boolean ? buf_lit(b, "true") : buf_lit(b, "false"); break;
    case VALUE_INT: w = buf_i64(b, v->as.integer); break;
    case VALUE_STRING: return json_emit_string(r, pos, b, v->as.string);
    case VALUE_TABLE: {
        if (depth_left == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`json_write`: the value nests past 512 deep (or holds itself)"));
        Table *t = v->as.table;
        if (t->kind == TABLE_EMPTY) {
            w = buf_lit(b, "[]");
        } else if (t->kind == TABLE_LIST) {
            if (buf_byte(b, '[') != OK) return run_values_oom(r, pos);
            for (size_t k = 0; k < t->list.len; k++) {
                if (k > 0 && buf_byte(b, ',') != OK) return run_values_oom(r, pos);
                TRY(json_emit(r, pos, b, &at(t->list, k), depth_left - 1));
            }
            w = buf_byte(b, ']');
        } else {
            if (buf_byte(b, '{') != OK) return run_values_oom(r, pos);
            for (size_t k = 0; k < ValueMap_len(&t->map); k++) {
                if (k > 0 && buf_byte(b, ',') != OK) return run_values_oom(r, pos);
                TRY(json_emit_string(r, pos, b, ValueMap_key_at(&t->map, k)));
                if (buf_byte(b, ':') != OK) return run_values_oom(r, pos);
                Value x = ValueMap_value_at(&t->map, k);
                TRY(json_emit(r, pos, b, &x, depth_left - 1));
            }
            w = buf_byte(b, '}');
        }
        break;
    }
    default: c_bug("host_json: a value of no tag");
    }
    if (w != OK) return run_values_oom(r, pos);
    return OK;
}

// `host("json_write", v)`: `v` as compact JSON, a map's members in its keys'
// order, an empty table `[]`, nil `null`.
static STATUS_MUST_USE Status host_json_write(Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`json_write` takes one value, given {d}", S("`json_write` takes one value, given "), run_unum(r, args.len));
    Buf b = buf_make(r->ctx.values);
    TRY(json_emit(r, pos, &b, &at(args, 0), JSON_DEPTH_MAX));
    TRY(builtin_charge_bytes(r, pos, b.len));
    value_string(out, buf_view(&b));
    return OK;
}
