// settings_json.c: Claude Code's settings file (`~/.claude/settings.json`)
// as a JSON tree that keeps what it read: each object's members in their
// order, and each key, string, number and literal as its own source text,
// escapes and all. A file written back says what it said, laid out again
// with two spaces a level. `shale --secrets`'s deny reads the file, finds
// the deny rules it lacks, adds them and writes it (secrets_mode.c).
//
// Include buf.h and vec.h first (store.c includes them).

typedef enum {
    SETTINGS_OBJECT,
    SETTINGS_ARRAY,
    SETTINGS_STRING,
    // A number, `true`, `false` or `null`.
    SETTINGS_SCALAR,
} SettingsKind;

typedef struct SettingsNode SettingsNode;

typedef struct {
    // The key's source text, its quotes kept.
    Str key;
    SettingsNode *value;
} SettingsMember;

VEC_DEFINE(SettingsMembers, SettingsMember)
VEC_DEFINE(SettingsList, SettingsNode *)
VEC_DEFINE(SettingsStrs, Str)

typedef struct SettingsNode {
    SettingsKind kind;
    // A string's or a scalar's source text (a string's quotes kept).
    Str text;
    // An object's members, in their order.
    SettingsMembers members;
    // An array's items, in their order.
    SettingsList items;
} SettingsNode;

// The deepest nesting a settings file may have: past it the file is
// refused, so neither the reader nor the writer recurses without end.
enum { SETTINGS_DEPTH_MAX = 256 };

// A text being read: where, how deep, and why it does not parse (empty on
// an arena's refusal).
typedef struct {
    Arena *arena;
    Str text;
    size_t i;
    size_t depth;
    Str why;
    // The byte, from 0, where the reader gave up.
    size_t why_at;
} SettingsReader;

static STATUS_MUST_USE Status settings_bad(SettingsReader *r, Str why) {
    r->why = why;
    r->why_at = r->i;
    return STOP;
}

static bool settings_more(const SettingsReader *r) {
    return r->i < r->text.len;
}

static uint8_t settings_byte(const SettingsReader *r) {
    return str_byte(r->text, r->i);
}

static void settings_space(SettingsReader *r) {
    while (settings_more(r)) {
        uint8_t c = settings_byte(r);
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
        r->i++;
    }
}

static bool settings_is_hex(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool settings_is_digit(uint8_t c) {
    return c >= '0' && c <= '9';
}

// A string at `r->i`, its source text with its quotes into `*out`: escapes
// are checked but kept as they are.
static STATUS_MUST_USE Status settings_string(SettingsReader *r, Str *out) {
    size_t from = r->i;
    if (!settings_more(r) || settings_byte(r) != '"') return settings_bad(r, S("a string was expected"));
    r->i++;
    for (;;) {
        if (!settings_more(r)) return settings_bad(r, S("a string is not closed"));
        uint8_t c = settings_byte(r);
        if (c == '"') break;
        if (c < 0x20) return settings_bad(r, S("a control character is in a string"));
        r->i++;
        if (c != '\\') continue;
        if (!settings_more(r)) return settings_bad(r, S("a string is not closed"));
        uint8_t e = settings_byte(r);
        r->i++;
        if (e == 'u') {
            for (int k = 0; k < 4; k++) {
                if (!settings_more(r) || !settings_is_hex(settings_byte(r))) return settings_bad(r, S("a `\\u` escape needs four hex digits"));
                r->i++;
            }
        } else if (str_find_byte(S("\"\\/bfnrt"), (char)e) == NULL) {
            r->i--;
            return settings_bad(r, S("a string has an escape JSON does not know"));
        }
    }
    r->i++;
    *out = str_sub(r->text, from, r->i);
    return OK;
}

// The digits at `r->i`, at least one.
static STATUS_MUST_USE Status settings_digits(SettingsReader *r) {
    if (!settings_more(r) || !settings_is_digit(settings_byte(r))) return settings_bad(r, S("a number needs a digit here"));
    while (settings_more(r) && settings_is_digit(settings_byte(r))) r->i++;
    return OK;
}

// A number at `r->i`, as JSON writes one, its source text into `*out`.
static STATUS_MUST_USE Status settings_number(SettingsReader *r, Str *out) {
    size_t from = r->i;
    if (settings_byte(r) == '-') r->i++;
    if (settings_more(r) && settings_byte(r) == '0') {
        r->i++;
    } else {
        TRY(settings_digits(r));
    }
    if (settings_more(r) && settings_byte(r) == '.') {
        r->i++;
        TRY(settings_digits(r));
    }
    if (settings_more(r) && (settings_byte(r) == 'e' || settings_byte(r) == 'E')) {
        r->i++;
        if (settings_more(r) && (settings_byte(r) == '+' || settings_byte(r) == '-')) r->i++;
        TRY(settings_digits(r));
    }
    *out = str_sub(r->text, from, r->i);
    return OK;
}

static STATUS_MUST_USE Status settings_new(SettingsReader *r, SettingsKind kind, SettingsNode **out) {
    SettingsNode *n = ARENA_NEW(r->arena, SettingsNode);
    if (n == NULL) return STOP;
    memset(n, 0, sizeof *n);
    n->kind = kind;
    n->members = SettingsMembers_make(r->arena);
    n->items = SettingsList_make(r->arena);
    *out = n;
    return OK;
}

static STATUS_MUST_USE Status settings_value(SettingsReader *r, SettingsNode **out);

// An object's members after its `{`, through its `}`.
static STATUS_MUST_USE Status settings_object(SettingsReader *r, SettingsNode *n) {
    settings_space(r);
    if (settings_more(r) && settings_byte(r) == '}') {
        r->i++;
        return OK;
    }
    for (;;) {
        settings_space(r);
        SettingsMember m = {S(""), NULL};
        TRY(settings_string(r, &m.key));
        settings_space(r);
        if (!settings_more(r) || settings_byte(r) != ':') return settings_bad(r, S("a `:` was expected after a key"));
        r->i++;
        TRY(settings_value(r, &m.value));
        TRY(SettingsMembers_push(&n->members, m));
        settings_space(r);
        if (!settings_more(r)) return settings_bad(r, S("an object is not closed"));
        uint8_t c = settings_byte(r);
        r->i++;
        if (c == '}') return OK;
        if (c != ',') {
            r->i--;
            return settings_bad(r, S("a `,` or `}` was expected in an object"));
        }
    }
}

// An array's items after its `[`, through its `]`.
static STATUS_MUST_USE Status settings_array(SettingsReader *r, SettingsNode *n) {
    settings_space(r);
    if (settings_more(r) && settings_byte(r) == ']') {
        r->i++;
        return OK;
    }
    for (;;) {
        SettingsNode *item = NULL;
        TRY(settings_value(r, &item));
        TRY(SettingsList_push(&n->items, item));
        settings_space(r);
        if (!settings_more(r)) return settings_bad(r, S("a list is not closed"));
        uint8_t c = settings_byte(r);
        r->i++;
        if (c == ']') return OK;
        if (c != ',') {
            r->i--;
            return settings_bad(r, S("a `,` or `]` was expected in a list"));
        }
    }
}

static STATUS_MUST_USE Status settings_value(SettingsReader *r, SettingsNode **out) {
    settings_space(r);
    if (!settings_more(r)) return settings_bad(r, S("a value was expected"));
    uint8_t c = settings_byte(r);
    if (c == '{' || c == '[') {
        if (r->depth >= SETTINGS_DEPTH_MAX) return settings_bad(r, S("it nests deeper than 256"));
        r->depth++;
        r->i++;
        TRY(settings_new(r, c == '{' ? SETTINGS_OBJECT : SETTINGS_ARRAY, out));
        TRY(c == '{' ? settings_object(r, *out) : settings_array(r, *out));
        r->depth--;
        return OK;
    }
    if (c == '"') {
        TRY(settings_new(r, SETTINGS_STRING, out));
        return settings_string(r, &(*out)->text);
    }
    if (c == '-' || settings_is_digit(c)) {
        TRY(settings_new(r, SETTINGS_SCALAR, out));
        return settings_number(r, &(*out)->text);
    }
    for (int k = 0; k < 3; k++) {
        Str w = k == 0 ? S("true") : k == 1 ? S("false") : S("null");
        Str here = str_sub(r->text, r->i, r->text.len);
        if (str_starts_with(here, w)) {
            TRY(settings_new(r, SETTINGS_SCALAR, out));
            (*out)->text = str_sub(here, 0, w.len);
            r->i += w.len;
            return OK;
        }
    }
    return settings_bad(r, S("a value was expected"));
}

// `text` read as one JSON value into `*out`. STOP with `r->why` set for a
// text that does not parse, and with it empty when the arena refused.
static STATUS_MUST_USE Status settings_read(SettingsReader *r, Arena *a, Str text, SettingsNode **out) {
    memset(r, 0, sizeof *r);
    r->arena = a;
    r->text = text;
    r->why = S("");
    TRY(settings_value(r, out));
    settings_space(r);
    if (settings_more(r)) return settings_bad(r, S("there is more after the value"));
    return OK;
}

// The four hex digits of `s` from `i`; settings_string checked them.
static uint32_t settings_hex4(Str s, size_t i) {
    uint32_t v = 0;
    for (size_t k = i; k < i + 4; k++) {
        uint8_t c = str_byte(s, k);
        uint32_t d = c <= '9' ? (uint32_t)(c - '0') : c <= 'F' ? (uint32_t)(c - 'A' + 10) : (uint32_t)(c - 'a' + 10);
        v = v * 16 + d;
    }
    return v;
}

// What a string's source text `raw` (checked by settings_string) says, its
// escapes undone, a `\u` pair of surrogates made one character.
static STATUS_MUST_USE Status settings_string_value(Arena *a, Str raw, Str *out) {
    Buf b = buf_make(a);
    size_t i = 1;
    size_t end = raw.len - 1;
    while (i < end) {
        uint8_t c = str_byte(raw, i);
        if (c != '\\') {
            TRY(buf_byte(&b, (char)c));
            i++;
            continue;
        }
        uint8_t e = str_byte(raw, i + 1);
        i += 2;
        if (e != 'u') {
            Str from = S("\"\\/bfnrt");
            Str to = S("\"\\/\b\f\n\r\t");
            const char *p = str_find_byte(from, (char)e);
            TRY(buf_byte(&b, (char)str_byte(to, (size_t)(p - from.ptr))));
            continue;
        }
        uint32_t cp = settings_hex4(raw, i);
        i += 4;
        bool high = cp >= 0xd800 && cp <= 0xdbff;
        if (high && i + 6 <= end && str_byte(raw, i) == '\\' && str_byte(raw, i + 1) == 'u') {
            uint32_t lo = settings_hex4(raw, i + 2);
            if (lo >= 0xdc00 && lo <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                i += 6;
            }
        }
        TRY(buf_utf8(&b, cp));
    }
    *out = buf_view(&b);
    return OK;
}

// `value` as a JSON string's source text: quoted, with `"`, `\` and every
// control character escaped.
static STATUS_MUST_USE Status settings_string_text(Arena *a, Str value, Str *out) {
    Buf b = buf_make(a);
    TRY(buf_byte(&b, '"'));
    Str hex = S("0123456789abcdef");
    for (size_t i = 0; i < value.len; i++) {
        uint8_t c = str_byte(value, i);
        if (c == '"' || c == '\\') {
            TRY(buf_byte(&b, '\\'));
            TRY(buf_byte(&b, (char)c));
        } else if (c < 0x20) {
            TRY(buf_lit(&b, "\\u00"));
            TRY(buf_byte(&b, (char)str_byte(hex, c >> 4)));
            TRY(buf_byte(&b, (char)str_byte(hex, c & 15)));
        } else {
            TRY(buf_byte(&b, (char)c));
        }
    }
    TRY(buf_byte(&b, '"'));
    *out = buf_view(&b);
    return OK;
}

// The member of the object `n` whose key says `key`, NULL when none does,
// and how many do.
static STATUS_MUST_USE Status settings_member(Arena *a, const SettingsNode *n, Str key, SettingsNode **out, size_t *count) {
    *out = NULL;
    *count = 0;
    for (size_t k = 0; k < n->members.len; k++) {
        Str said = S("");
        TRY(settings_string_value(a, at(n->members, k).key, &said));
        if (!str_eq(said, key)) continue;
        if (*out == NULL) *out = at(n->members, k).value;
        (*count)++;
    }
    return OK;
}

// Where the deny rules go in a settings tree, and which of them it lacks.
typedef struct {
    // The `permissions` object and its `deny` list, NULL where missing.
    SettingsNode *permissions;
    SettingsNode *deny;
    // The rules asked for that `deny` does not hold, in the order asked.
    SettingsStrs missing;
} SettingsDenies;

// Which of `rules` the tree `root` lacks in `permissions.deny`. STOP with
// `*why` set for a tree that cannot hold them (its top level not an object,
// `permissions` not one, `deny` not a list, or either named twice), and with
// it empty when the arena refused.
static STATUS_MUST_USE Status settings_find_denies(Arena *a, SettingsNode *root, SettingsStrsItems rules, SettingsDenies *out, Str *why) {
    *why = S("");
    memset(out, 0, sizeof *out);
    out->missing = SettingsStrs_make(a);
    if (root->kind != SETTINGS_OBJECT) {
        *why = S("its top level is not an object");
        return STOP;
    }
    size_t count = 0;
    TRY(settings_member(a, root, S("permissions"), &out->permissions, &count));
    if (count > 1) {
        *why = S("its top level names `permissions` twice");
        return STOP;
    }
    if (out->permissions != NULL && out->permissions->kind != SETTINGS_OBJECT) {
        *why = S("its `permissions` is not an object");
        return STOP;
    }
    if (out->permissions != NULL) {
        TRY(settings_member(a, out->permissions, S("deny"), &out->deny, &count));
        if (count > 1) {
            *why = S("its `permissions` names `deny` twice");
            return STOP;
        }
        if (out->deny != NULL && out->deny->kind != SETTINGS_ARRAY) {
            *why = S("its `permissions.deny` is not a list");
            return STOP;
        }
    }
    for (size_t k = 0; k < rules.len; k++) {
        bool held = false;
        for (size_t j = 0; !held && out->deny != NULL && j < out->deny->items.len; j++) {
            const SettingsNode *item = at(out->deny->items, j);
            if (item->kind != SETTINGS_STRING) continue;
            Str said = S("");
            TRY(settings_string_value(a, item->text, &said));
            held = str_eq(said, at(rules, k));
        }
        if (!held) TRY(SettingsStrs_push(&out->missing, at(rules, k)));
    }
    return OK;
}

static STATUS_MUST_USE Status settings_add_member(Arena *a, SettingsNode *obj, Str key, SettingsKind kind, SettingsNode **out) {
    SettingsReader r;
    memset(&r, 0, sizeof r);
    r.arena = a;
    TRY(settings_new(&r, kind, out));
    SettingsMember m = {S(""), *out};
    TRY(settings_string_text(a, key, &m.key));
    return SettingsMembers_push(&obj->members, m);
}

// Adds `d->missing` to the end of `root`'s `permissions.deny`, making
// `permissions` (at the top level's end) and `deny` (at its end) where they
// are missing.
static STATUS_MUST_USE Status settings_add_denies(Arena *a, SettingsNode *root, SettingsDenies *d) {
    if (d->permissions == NULL) TRY(settings_add_member(a, root, S("permissions"), SETTINGS_OBJECT, &d->permissions));
    if (d->deny == NULL) TRY(settings_add_member(a, d->permissions, S("deny"), SETTINGS_ARRAY, &d->deny));
    SettingsReader r;
    memset(&r, 0, sizeof r);
    r.arena = a;
    for (size_t k = 0; k < d->missing.len; k++) {
        SettingsNode *item = NULL;
        TRY(settings_new(&r, SETTINGS_STRING, &item));
        TRY(settings_string_text(a, at(d->missing, k), &item->text));
        TRY(SettingsList_push(&d->deny->items, item));
    }
    return OK;
}

static STATUS_MUST_USE Status settings_indent(Buf *b, size_t level) {
    for (size_t k = 0; k < level; k++) TRY(buf_lit(b, "  "));
    return OK;
}

// `n` laid out with two spaces a level, its first line at `level`'s indent
// already written: `"key": value` a line, an empty object or list as `{}` or
// `[]`, as JavaScript's `JSON.stringify(v, null, 2)` lays one out.
static STATUS_MUST_USE Status settings_write_node(Buf *b, const SettingsNode *n, size_t level) {
    if (n->kind == SETTINGS_STRING || n->kind == SETTINGS_SCALAR) return buf_str(b, n->text);
    bool object = n->kind == SETTINGS_OBJECT;
    size_t len = object ? n->members.len : n->items.len;
    if (len == 0) return buf_str(b, object ? S("{}") : S("[]"));
    TRY(buf_str(b, object ? S("{\n") : S("[\n")));
    for (size_t k = 0; k < len; k++) {
        TRY(settings_indent(b, level + 1));
        if (object) {
            TRY(buf_str(b, at(n->members, k).key));
            TRY(buf_lit(b, ": "));
        }
        TRY(settings_write_node(b, object ? at(n->members, k).value : at(n->items, k), level + 1));
        TRY(buf_str(b, k + 1 < len ? S(",\n") : S("\n")));
    }
    TRY(settings_indent(b, level));
    return buf_byte(b, object ? '}' : ']');
}

// The whole tree as a settings file's text, ending with a newline.
static STATUS_MUST_USE Status settings_write(Arena *a, const SettingsNode *root, Str *out) {
    Buf b = buf_make(a);
    TRY(settings_write_node(&b, root, 0));
    TRY(buf_byte(&b, '\n'));
    *out = buf_view(&b);
    return OK;
}
