// regex.c: the regex compiler, matcher and pattern cache declared in regex.h. A
// program includes it once, after its own headers.
//
// A pattern is read into a tree of alternatives (each a sequence of nodes),
// then emitted by Thompson's construction, each split preferring its first
// way so every repetition is greedy. A loop whose turn can take nothing
// (`(a*)*`) ends at a turn that took nothing, as a backtracker's does: its
// threads carry a bit for it, so a state is an instruction and those bits.

#include "regex.h"
#include "vec.h"

// ── Sets ─────────────────────────────────────────────────────────────────

SLICE_TYPE(RegexWords, uint64_t);
SLICE_TYPE(RegexConstWords, const uint64_t);
SLICE_TYPE(RegexIds, uint8_t);
SLICE_TYPE(RegexGroups, RegexGroup);

// A bug in the C, not a script's stop: the process ends.
static void regex_bug(const char *what) { // cstr: a literal naming the bug
    c_bug_str("regex", (Str){what, strlen(what)});
}

static RegexWords regex_words(RegexSet *s) {
    RegexWords w = {s->words, 4};
    return w;
}

static RegexConstWords regex_cwords(const RegexSet *s) {
    RegexConstWords w = {s->words, 4};
    return w;
}

static RegexSet regex_set_empty(void) {
    RegexSet s;
    memset(&s, 0, sizeof s);
    return s;
}

static bool regex_set_has(const RegexSet *s, uint8_t b) {
    return (at(regex_cwords(s), b >> 6) >> (b & 63)) & 1;
}

static void regex_set_add(RegexSet *s, uint8_t b) {
    at(regex_words(s), b >> 6) |= (uint64_t)1 << (b & 63);
}

static void regex_set_remove(RegexSet *s, uint8_t b) {
    at(regex_words(s), b >> 6) &= ~((uint64_t)1 << (b & 63));
}

// Every byte from `lo` to `hi`, both included.
static void regex_set_range(RegexSet *s, uint8_t lo, uint8_t hi) {
    for (unsigned b = lo; b <= hi; b++) regex_set_add(s, (uint8_t)b);
}

static void regex_set_union(RegexSet *s, const RegexSet *t) {
    for (size_t k = 0; k < 4; k++) at(regex_words(s), k) |= at(regex_cwords(t), k);
}

static void regex_set_toggle_all(RegexSet *s) {
    for (size_t k = 0; k < 4; k++) at(regex_words(s), k) = ~at(regex_words(s), k);
}

// A word's bits counted in parallel, since TCC has no popcount builtin.
static unsigned regex_popcount(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ull);
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0full;
    return (unsigned)((x * 0x0101010101010101ull) >> 56);
}

static unsigned regex_set_count(const RegexSet *s) {
    unsigned n = 0;
    for (size_t k = 0; k < 4; k++) n += regex_popcount(at(regex_cwords(s), k));
    return n;
}

// The lowest byte in the set; the set is not empty.
static uint8_t regex_set_lowest(const RegexSet *s) {
    for (unsigned b = 0; b < 256; b++) {
        if (regex_set_has(s, (uint8_t)b)) return (uint8_t)b;
    }
    regex_bug("the lowest byte of an empty set");
    // Never reached; TCC doesn't know `abort` ends the process.
    return 0;
}

// ── ASCII ────────────────────────────────────────────────────────────────

static bool regex_is_digit(uint8_t c) {
    return c >= '0' && c <= '9';
}

static bool regex_is_upper(uint8_t c) {
    return c >= 'A' && c <= 'Z';
}

static bool regex_is_alpha(uint8_t c) {
    return regex_is_upper(c) || (c >= 'a' && c <= 'z');
}

static uint8_t regex_lower(uint8_t c) {
    return regex_is_upper(c) ? (uint8_t)(c + 32) : c;
}

static uint8_t regex_upper(uint8_t c) {
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c;
}

static bool regex_word_byte(uint8_t c) {
    return regex_is_alpha(c) || regex_is_digit(c) || c == '_';
}

// ── The tree ─────────────────────────────────────────────────────────────

typedef enum { REGEX_ONE, REGEX_STAR, REGEX_PLUS, REGEX_OPT } RegexQuant;

typedef enum {
    REGEX_NODE_BYTE,
    REGEX_NODE_ANY,
    REGEX_NODE_SET,
    REGEX_NODE_START,
    REGEX_NODE_END,
    REGEX_NODE_BOUNDARY,
    REGEX_NODE_NOT_BOUNDARY,
    REGEX_NODE_GROUP,
} RegexNodeKind;

// A sequence of nodes.
typedef struct {
    const struct RegexNode *ptr;
    size_t len;
} RegexSeq;

// A pattern's or a group's alternatives.
SLICE_TYPE(RegexAlt, const RegexSeq);

typedef struct RegexNode {
    RegexNodeKind kind;
    uint8_t c;
    uint16_t set;
    RegexQuant quant;
    // A group's alternatives, shared by the copies a count writes out.
    RegexAlt inner;
    // A group's number from 1; 0 for one that does not capture.
    uint8_t index;
} RegexNode;

VEC_DEFINE(RegexNodeVec, RegexNode)
VEC_DEFINE(RegexSeqVec, RegexSeq)
VEC_DEFINE(RegexSetVec, RegexSet)
VEC_DEFINE(RegexInstVec, RegexInst)

static RegexNode regex_node(RegexNodeKind kind) {
    RegexNode n;
    memset(&n, 0, sizeof n);
    n.kind = kind;
    return n;
}

static bool regex_is_place(RegexNodeKind k) {
    return k == REGEX_NODE_START || k == REGEX_NODE_END || k == REGEX_NODE_BOUNDARY || k == REGEX_NODE_NOT_BOUNDARY;
}

// ── The reader ───────────────────────────────────────────────────────────

typedef struct {
    Str p;
    size_t at;
    // The tree, given back once the program is emitted.
    Arena *tree;
    RegexRefusal *refused;
    RegexOptions options;
    // The sets, which the program keeps, in the program's arena.
    RegexSetVec sets;
    uint8_t groups;
} RegexReader;

static STATUS_MUST_USE Status regex_refuse(RegexRefusal *refused, size_t at, Str message) {
    refused->refused = true;
    refused->at = at;
    refused->message = message;
    return STOP;
}

// The byte `k` past the reader, or -1 past the pattern's end.
static int regex_peek(const RegexReader *r, size_t k) {
    if (r->at + k >= r->p.len) return -1;
    return str_byte(r->p, r->at + k);
}

static STATUS_MUST_USE Status regex_seq(RegexReader *r, bool in_group, RegexSeq *out);

// Alternatives up to the pattern's end or, in a group, its `)`.
static STATUS_MUST_USE Status regex_alt(RegexReader *r, bool in_group, RegexAlt *out) {
    RegexSeqVec seqs = RegexSeqVec_make(r->tree);
    for (;;) {
        RegexSeq s;
        TRY(regex_seq(r, in_group, &s));
        TRY(RegexSeqVec_push(&seqs, s));
        if (regex_peek(r, 0) == '|') {
            r->at++;
            continue;
        }
        out->ptr = seqs.ptr;
        out->len = seqs.len;
        return OK;
    }
}

static STATUS_MUST_USE Status regex_new_set(RegexReader *r, RegexSet s, RegexNode *out) {
    TRY(RegexSetVec_push(&r->sets, s));
    *out = regex_node(REGEX_NODE_SET);
    out->set = (uint16_t)(r->sets.len - 1);
    return OK;
}

// A byte node, or under `nocase` a set of a letter's two cases.
static STATUS_MUST_USE Status regex_byte_node(RegexReader *r, uint8_t c, RegexNode *out) {
    if (r->options.nocase && regex_is_alpha(c)) {
        RegexSet s = regex_set_empty();
        regex_set_add(&s, regex_lower(c));
        regex_set_add(&s, regex_upper(c));
        return regex_new_set(r, s, out);
    }
    *out = regex_node(REGEX_NODE_BYTE);
    out->c = c;
    return OK;
}

// The set of `\d \w \s` or their negations; false for any other byte.
static bool regex_shorthand(uint8_t c, RegexSet *out) {
    RegexSet s = regex_set_empty();
    switch (regex_lower(c)) {
    case 'd':
        regex_set_range(&s, '0', '9');
        break;
    case 'w':
        regex_set_range(&s, '0', '9');
        regex_set_range(&s, 'a', 'z');
        regex_set_range(&s, 'A', 'Z');
        regex_set_add(&s, '_');
        break;
    case 's':
        regex_set_add(&s, ' ');
        regex_set_add(&s, '\t');
        regex_set_add(&s, '\n');
        regex_set_add(&s, '\r');
        regex_set_add(&s, 0x0b);
        regex_set_add(&s, 0x0c);
        break;
    default: return false;
    }
    if (regex_is_upper(c)) regex_set_toggle_all(&s);
    *out = s;
    return true;
}

// The byte a one-byte escape stands for: `\n \t \r`, or any byte that is not
// a letter or digit, itself.
static STATUS_MUST_USE Status regex_escaped_byte(RegexReader *r, size_t at, uint8_t c, uint8_t *out) {
    switch (c) {
    case 'n':
        *out = '\n';
        return OK;
    case 't':
        *out = '\t';
        return OK;
    case 'r':
        *out = '\r';
        return OK;
    default: break;
    }
    if (regex_is_digit(c)) return regex_refuse(r->refused, at, S("backreferences (`\\1`) are not in this subset"));
    if (regex_is_alpha(c)) return regex_refuse(r->refused, at, S("an escape this subset does not have: `\\d \\w \\s` (and `\\D \\W \\S`), `\\b \\B`, `\\n \\t \\r`, or `\\` before a byte that is not a letter or digit"));
    *out = c;
    return OK;
}

static STATUS_MUST_USE Status regex_escape(RegexReader *r, RegexNode *out) {
    size_t at = r->at;
    if (at + 1 >= r->p.len) return regex_refuse(r->refused, at, S("a pattern that ends in `\\`: `\\\\` is the byte"));
    uint8_t c = str_byte(r->p, at + 1);
    r->at += 2;
    RegexSet s;
    if (regex_shorthand(c, &s)) return regex_new_set(r, s, out);
    if (c == 'b') {
        *out = regex_node(REGEX_NODE_BOUNDARY);
        return OK;
    }
    if (c == 'B') {
        *out = regex_node(REGEX_NODE_NOT_BOUNDARY);
        return OK;
    }
    uint8_t b;
    TRY(regex_escaped_byte(r, at, c, &b));
    return regex_byte_node(r, b, out);
}

// `[...]`: bytes, ranges (`a-z`) and shorthands. A leading `^` negates it; a
// `]` first is a byte. Under `nocase`, letters fold before the negation.
static STATUS_MUST_USE Status regex_class(RegexReader *r, RegexNode *out) {
    size_t open = r->at;
    r->at++;
    RegexSet s = regex_set_empty();
    bool negated = regex_peek(r, 0) == '^';
    if (negated) r->at++;
    for (bool first = true;; first = false) {
        if (r->at >= r->p.len) return regex_refuse(r->refused, open, S("a `[` that is never closed"));
        size_t at = r->at;
        uint8_t c = str_byte(r->p, at);
        if (c == ']' && !first) {
            r->at++;
            break;
        }
        if (c == '[' && regex_peek(r, 1) == ':') return regex_refuse(r->refused, at, S("`[:alpha:]` and its kind are not in this subset: write the bytes (`[a-zA-Z]`), or `\\[` for the byte"));
        uint8_t lo = c;
        if (c == '\\') {
            if (at + 1 >= r->p.len) return regex_refuse(r->refused, open, S("a `[` that is never closed"));
            uint8_t e = str_byte(r->p, at + 1);
            r->at += 2;
            RegexSet sh;
            if (regex_shorthand(e, &sh)) {
                regex_set_union(&s, &sh);
                continue;
            }
            if (e == 'b' || e == 'B') return regex_refuse(r->refused, at, S("`\\b` in a class is not in this subset: a word's edge is a place, not a byte; write it outside the `[...]`"));
            TRY(regex_escaped_byte(r, at, e, &lo));
        } else {
            r->at++;
        }
        // A `-` just before `]` is a byte, not a range.
        if (r->at + 1 < r->p.len && regex_peek(r, 0) == '-' && regex_peek(r, 1) != ']') {
            size_t hi_at = r->at + 1;
            uint8_t hi = str_byte(r->p, hi_at);
            r->at += 2;
            if (hi == '\\') {
                if (hi_at + 1 >= r->p.len) return regex_refuse(r->refused, open, S("a `[` that is never closed"));
                uint8_t e = str_byte(r->p, hi_at + 1);
                RegexSet sh;
                if (regex_shorthand(e, &sh)) return regex_refuse(r->refused, hi_at, S("a range's end must be one byte, not a class"));
                TRY(regex_escaped_byte(r, hi_at, e, &hi));
                r->at++;
            }
            if (hi < lo) return regex_refuse(r->refused, at, S("a range whose end is before its start (`z-a`): write it `a-z`"));
            regex_set_range(&s, lo, hi);
        } else {
            regex_set_add(&s, lo);
        }
    }
    if (r->options.nocase) {
        RegexSet folded = s;
        for (unsigned b = 0; b < 256; b++) {
            if (regex_set_has(&s, (uint8_t)b) && regex_is_alpha((uint8_t)b)) {
                regex_set_add(&folded, regex_lower((uint8_t)b));
                regex_set_add(&folded, regex_upper((uint8_t)b));
            }
        }
        s = folded;
    }
    if (negated) regex_set_toggle_all(&s);
    return regex_new_set(r, s, out);
}

static const Str REGEX_COUNT_FORM = S("a count is `{n}`, `{n,}` or `{n,m}`, each a number of digits: `\\{` is the byte");

// The digits at the reader as a number, `*got` false when there are none. A
// number past size_t is refused with the count's form.
static STATUS_MUST_USE Status regex_number(RegexReader *r, size_t at, size_t *out, bool *got) {
    size_t from = r->at;
    while (r->at < r->p.len && regex_is_digit(str_byte(r->p, r->at))) r->at++;
    *got = r->at > from;
    if (!*got) return OK;
    size_t n = 0;
    for (size_t k = from; k < r->at; k++) {
        size_t d = (size_t)(str_byte(r->p, k) - '0');
        if (n > (SIZE_MAX - d) / 10) return regex_refuse(r->refused, at, REGEX_COUNT_FORM);
        n = n * 10 + d;
    }
    *out = n;
    return OK;
}

// The count at `{`: `{n}`, `{n,}` (`*has_most` false) or `{n,m}`, read past its
// `}`.
static STATUS_MUST_USE Status regex_count_after(RegexReader *r, size_t *least, size_t *most, bool *has_most) {
    size_t at = r->at;
    r->at++;
    bool got;
    TRY(regex_number(r, at, least, &got));
    if (!got) return regex_refuse(r->refused, at, REGEX_COUNT_FORM);
    *most = *least;
    *has_most = true;
    if (regex_peek(r, 0) == ',') {
        r->at++;
        TRY(regex_number(r, at, most, has_most));
    }
    if (regex_peek(r, 0) != '}') return regex_refuse(r->refused, at, REGEX_COUNT_FORM);
    r->at++;
    if (*has_most && *most < *least) return regex_refuse(r->refused, at, S("a count whose most is below its least (`{5,2}`): write it `{2,5}`"));
    if (*least > REGEX_COUNT_MOST || (*has_most && *most > REGEX_COUNT_MOST)) return regex_refuse(r->refused, at, S("a count past 100 (`{n,m}`): write `+` or `*` for a part that may come any number of times"));
    return OK;
}

static const Str REGEX_PLACES = S("`^`, `$`, `\\b` and `\\B` are places, not bytes: they cannot repeat");

// The group at `(`, read past its `)`.
static STATUS_MUST_USE Status regex_group(RegexReader *r, RegexNode *out) {
    size_t at = r->at;
    uint8_t index = 0;
    if (regex_peek(r, 1) == '?') {
        if (regex_peek(r, 2) != ':') return regex_refuse(r->refused, at, S("`(?` is not in this subset but for `(?:...)`, a group that does not capture: no lookaround, no named groups"));
        r->at += 3;
    } else {
        if (r->groups == REGEX_GROUPS_MOST) return regex_refuse(r->refused, at, S("a tenth capture group: nine is the most (`\\9` is the last), so write this one `(?:...)` if it need not capture"));
        r->groups++;
        index = r->groups;
        r->at++;
    }
    RegexAlt inner;
    TRY(regex_alt(r, true, &inner));
    if (r->at >= r->p.len) return regex_refuse(r->refused, at, S("a `(` that is never closed"));
    r->at++;
    *out = regex_node(REGEX_NODE_GROUP);
    out->inner = inner;
    out->index = index;
    return OK;
}

// A sequence up to the end, a `|`, or, in a group, its `)`.
static STATUS_MUST_USE Status regex_seq(RegexReader *r, bool in_group, RegexSeq *out) {
    RegexNodeVec list = RegexNodeVec_make(r->tree);
    while (r->at < r->p.len) {
        size_t at = r->at;
        uint8_t c = str_byte(r->p, at);
        RegexNode node;
        switch (c) {
        case ')':
            if (in_group) {
                out->ptr = list.ptr;
                out->len = list.len;
                return OK;
            }
            return regex_refuse(r->refused, at, S("a `)` with no `(` before it: `\\)` is the byte"));
        case '|':
            out->ptr = list.ptr;
            out->len = list.len;
            return OK;
        case '(':
            TRY(regex_group(r, &node));
            break;
        case '{': return regex_refuse(r->refused, at, S("a count (`{n,m}`) with nothing before it to repeat: `\\{` is the byte"));
        case '*':
        case '+':
        case '?': return regex_refuse(r->refused, at, S("a `*`, `+` or `?` with nothing before it to repeat"));
        case '.':
            r->at++;
            node = regex_node(REGEX_NODE_ANY);
            break;
        case '^':
            r->at++;
            node = regex_node(REGEX_NODE_START);
            break;
        case '$':
            r->at++;
            node = regex_node(REGEX_NODE_END);
            break;
        case '[':
            TRY(regex_class(r, &node));
            break;
        case '\\':
            TRY(regex_escape(r, &node));
            break;
        default:
            r->at++;
            TRY(regex_byte_node(r, c, &node));
            break;
        }
        int q = regex_peek(r, 0);
        if (q == '*' || q == '+' || q == '?') {
            if (regex_is_place(node.kind)) return regex_refuse(r->refused, r->at, REGEX_PLACES);
            node.quant = q == '*' ? REGEX_STAR : q == '+' ? REGEX_PLUS : REGEX_OPT;
            r->at++;
            int after = regex_peek(r, 0);
            if (after == '?') return regex_refuse(r->refused, r->at, S("lazy repetition (`*?`, `+?`, `??`) is not in this subset: every repetition takes as much as it can"));
            if (after == '*' || after == '+') return regex_refuse(r->refused, r->at, S("a repetition repeated (`a**`, `a+*`): one is enough"));
            if (after == '{') return regex_refuse(r->refused, r->at, S("a repetition repeated (`a*{2}`): one is enough"));
        } else if (q == '{') {
            if (regex_is_place(node.kind)) return regex_refuse(r->refused, r->at, REGEX_PLACES);
            size_t least;
            size_t most;
            bool has_most;
            TRY(regex_count_after(r, &least, &most, &has_most));
            // The part written out: `least` times, then `most - least` times
            // optional, or for `{n,}` once more under a `*`.
            for (size_t k = 0; k < least; k++) TRY(RegexNodeVec_push(&list, node));
            RegexNode rest = node;
            if (has_most) {
                rest.quant = REGEX_OPT;
                for (size_t k = least; k < most; k++) TRY(RegexNodeVec_push(&list, rest));
            } else {
                rest.quant = REGEX_STAR;
                TRY(RegexNodeVec_push(&list, rest));
            }
            int after = regex_peek(r, 0);
            if (after == '?') return regex_refuse(r->refused, r->at, S("lazy repetition (`{2,5}?`) is not in this subset: every repetition takes as much as it can"));
            if (after == '*' || after == '+' || after == '{') return regex_refuse(r->refused, r->at, S("a repetition repeated (`a{2}*`): one is enough"));
            continue;
        }
        TRY(RegexNodeVec_push(&list, node));
    }
    out->ptr = list.ptr;
    out->len = list.len;
    return OK;
}

// Whether `n` can match without taking a byte.
static bool regex_nullable_alt(RegexAlt a);

// `quant` is the node's, or REGEX_ONE to ask of the part it repeats.
static bool regex_nullable(const RegexNode *n, RegexQuant quant) {
    if (quant == REGEX_STAR || quant == REGEX_OPT) return true;
    switch (n->kind) {
    case REGEX_NODE_BYTE:
    case REGEX_NODE_ANY:
    case REGEX_NODE_SET: return false;
    case REGEX_NODE_GROUP: return regex_nullable_alt(n->inner);
    default: return true;
    }
}

static bool regex_nullable_alt(RegexAlt a) {
    for (size_t k = 0; k < a.len; k++) {
        RegexSeq s = at(a, k);
        bool all = true;
        for (size_t j = 0; j < s.len; j++) {
            if (!regex_nullable(&at(s, j), at(s, j).quant)) {
                all = false;
                break;
            }
        }
        if (all) return true;
    }
    return false;
}

// ── The emitter ──────────────────────────────────────────────────────────

// A loop's turn, instructions `from` to `to`: the ones loop `id`'s bit tells
// apart.
typedef struct {
    uint8_t id;
    uint16_t from;
    uint16_t to;
} RegexRange;

VEC_DEFINE(RegexRangeVec, RegexRange)
SLICE_TYPE(RegexPcs, uint16_t);

typedef struct {
    // In the program's arena.
    RegexInstVec insts;
    RegexRefusal *refused;
    // Loops whose turn can take nothing, numbered as they are emitted.
    uint8_t loops;
    // In the tree's arena.
    RegexRangeVec ranges;
} RegexEmitter;

// The instruction `op` with its fields put at the program's end; `pc`, when
// not NULL, takes its place. It takes the fields rather than a RegexInst a
// function made: no function returns one by value (regex.h, RegexInst).
static STATUS_MUST_USE Status regex_emit(RegexEmitter *e, RegexOp op, uint8_t c, uint16_t x, uint16_t y, uint16_t *pc) {
    // DRAGON: the step budget. A search costs up to its states a position, and
    // a count writes its part out (`((a{100}){100}){100}` would be a million
    // instructions), so an unbounded program would make a search's cost
    // unbounded before it runs; a place past 65535 would also wrap the uint16_t
    // pcs.
    // REMEDY: this check refuses a program past REGEX_INSTS_MOST (8192), and
    // every instruction comes through here: regex_emit is the only push to
    // `insts`.
    if (e->insts.len >= REGEX_INSTS_MOST) return regex_refuse(e->refused, 0, S("the pattern, its counts written out, is more than 8192 instructions: count less (`{n,m}`), or write `+` or `*`"));
    RegexInst i = {(uint8_t)op, c, x, y};
    TRY(RegexInstVec_push(&e->insts, i));
    if (pc != NULL) *pc = (uint16_t)(e->insts.len - 1);
    return OK;
}

static uint16_t regex_here(const RegexEmitter *e) {
    return (uint16_t)e->insts.len;
}

static STATUS_MUST_USE Status regex_emit_node(RegexEmitter *e, const RegexNode *n);

// A split before each alternative but the last, the leftmost preferred, each
// ending in a jump past the rest.
static STATUS_MUST_USE Status regex_emit_alt(RegexEmitter *e, RegexAlt a) {
    uint16_t ends_buf[REGEX_PATTERN_MOST + 1];
    RegexPcs ends = {ends_buf, REGEX_PATTERN_MOST + 1};
    size_t n_ends = 0;
    for (size_t k = 0; k < a.len; k++) {
        RegexSeq s = at(a, k);
        if (k + 1 == a.len) {
            for (size_t j = 0; j < s.len; j++) TRY(regex_emit_node(e, &at(s, j)));
            break;
        }
        uint16_t split;
        TRY(regex_emit(e, REGEX_OP_SPLIT, 0, 0, 0, &split));
        at(e->insts, split).x = (uint16_t)(split + 1);
        for (size_t j = 0; j < s.len; j++) TRY(regex_emit_node(e, &at(s, j)));
        TRY(regex_emit(e, REGEX_OP_JMP, 0, 0, 0, &at(ends, n_ends)));
        n_ends++;
        at(e->insts, split).y = regex_here(e);
    }
    for (size_t k = 0; k < n_ends; k++) at(e->insts, at(ends, k)).x = regex_here(e);
    return OK;
}

static STATUS_MUST_USE Status regex_emit_atom(RegexEmitter *e, const RegexNode *n) {
    switch (n->kind) {
    case REGEX_NODE_BYTE: return regex_emit(e, REGEX_OP_BYTE, n->c, 0, 0, NULL);
    case REGEX_NODE_ANY: return regex_emit(e, REGEX_OP_ANY, 0, 0, 0, NULL);
    case REGEX_NODE_SET: return regex_emit(e, REGEX_OP_SET, 0, n->set, 0, NULL);
    case REGEX_NODE_START: return regex_emit(e, REGEX_OP_START, 0, 0, 0, NULL);
    case REGEX_NODE_END: return regex_emit(e, REGEX_OP_END, 0, 0, 0, NULL);
    case REGEX_NODE_BOUNDARY: return regex_emit(e, REGEX_OP_BOUNDARY, 0, 0, 0, NULL);
    case REGEX_NODE_NOT_BOUNDARY: return regex_emit(e, REGEX_OP_BOUNDARY, 1, 0, 0, NULL);
    case REGEX_NODE_GROUP: break;
    }
    if (n->index == 0) return regex_emit_alt(e, n->inner);
    uint16_t k = (uint16_t)(n->index - 1);
    TRY(regex_emit(e, REGEX_OP_SAVE, 0, (uint16_t)(2 * k), 0, NULL));
    TRY(regex_emit_alt(e, n->inner));
    return regex_emit(e, REGEX_OP_SAVE, 0, (uint16_t)(2 * k + 1), 0, NULL);
}

// One turn of loop `id`: `turn`, the atom, and its end (`again` or `first`),
// recorded as a range. Gives the end's place.
static STATUS_MUST_USE Status regex_turn_of(RegexEmitter *e, const RegexNode *n, uint8_t id, RegexOp end_op, uint16_t *end) {
    uint16_t from;
    TRY(regex_emit(e, REGEX_OP_TURN, 0, id, 0, &from));
    TRY(regex_emit_atom(e, n));
    TRY(regex_emit(e, end_op, 0, id, 0, end));
    RegexRange rg = {id, from, *end};
    return RegexRangeVec_push(&e->ranges, rg);
}

// Thompson's construction; each split prefers its first way, so every
// repetition is greedy.
static STATUS_MUST_USE Status regex_emit_node(RegexEmitter *e, const RegexNode *n) {
    if ((n->quant == REGEX_STAR || n->quant == REGEX_PLUS) && regex_nullable(n, REGEX_ONE)) {
        // A loop whose turn can take nothing: its threads carry a bit, "this
        // turn took something", so a new turn begun where an earlier one stood
        // is not lost to that one's visit. A turn that took nothing ends the
        // loop (`again`); a `+`'s first such turn goes on past it (`first`).
        // DRAGON: the step budget. Each such loop doubles the states of every
        // instruction inside it, so a search's cost per position grows as 2 to
        // the loops nested.
        // REMEDY: two caps bound it at 16 states an instruction: this check
        // refuses a 17th loop (REGEX_EMPTY_LOOPS_MOST), and regex_states
        // refuses an instruction inside more than four (REGEX_NEST_MOST).
        if (e->loops == REGEX_EMPTY_LOOPS_MOST) return regex_refuse(e->refused, 0, S("more than 16 loops whose turn can match nothing (`(a*)*`, `(a|)+`): write some so a turn takes a byte"));
        uint8_t id = e->loops;
        e->loops++;
        bool has_first = n->quant == REGEX_PLUS;
        uint16_t first = 0;
        if (has_first) TRY(regex_turn_of(e, n, id, REGEX_OP_FIRST, &first));
        uint16_t loop;
        TRY(regex_emit(e, REGEX_OP_SPLIT, 0, 0, 0, &loop));
        uint16_t again;
        TRY(regex_turn_of(e, n, id, REGEX_OP_AGAIN, &again));
        TRY(regex_emit(e, REGEX_OP_JMP, 0, loop, 0, NULL));
        at(e->insts, loop).x = (uint16_t)(loop + 1);
        at(e->insts, loop).y = regex_here(e);
        if (has_first) at(e->insts, first).y = regex_here(e);
        return OK;
    }
    switch (n->quant) {
    case REGEX_ONE: return regex_emit_atom(e, n);
    case REGEX_STAR: {
        uint16_t loop;
        TRY(regex_emit(e, REGEX_OP_SPLIT, 0, 0, 0, &loop));
        TRY(regex_emit_atom(e, n));
        TRY(regex_emit(e, REGEX_OP_JMP, 0, loop, 0, NULL));
        at(e->insts, loop).x = (uint16_t)(loop + 1);
        at(e->insts, loop).y = regex_here(e);
        return OK;
    }
    case REGEX_PLUS: {
        uint16_t top = regex_here(e);
        TRY(regex_emit_atom(e, n));
        uint16_t s;
        TRY(regex_emit(e, REGEX_OP_SPLIT, 0, top, 0, &s));
        at(e->insts, s).y = (uint16_t)(s + 1);
        return OK;
    }
    case REGEX_OPT: break;
    }
    uint16_t s;
    TRY(regex_emit(e, REGEX_OP_SPLIT, 0, 0, 0, &s));
    TRY(regex_emit_atom(e, n));
    at(e->insts, s).x = (uint16_t)(s + 1);
    at(e->insts, s).y = regex_here(e);
    return OK;
}

// ── Compiling ────────────────────────────────────────────────────────────

// Whether every alternative begins with `^`.
static bool regex_anchored_all(RegexAlt top) {
    for (size_t k = 0; k < top.len; k++) {
        RegexSeq s = at(top, k);
        if (s.len == 0 || at(s, 0).kind != REGEX_NODE_START) return false;
    }
    return true;
}

SLICE_TYPE(RegexSeen, bool);

// The bytes a match can begin with: every consuming instruction reachable
// from the start without taking a byte, each place and each loop's bit taken
// as passable, so the set holds at least every byte a match can begin with.
// `*has` false when `match` is reachable that way (a match can take nothing)
// or every byte is in it (nothing to skip). Its scratch is in `tree`.
static STATUS_MUST_USE Status regex_first_bytes(Arena *tree, RegexInsts insts, RegexSets sets, bool *has, RegexSet *out) {
    *has = false;
    bool *seen_buf = ARENA_ARRAY(tree, bool, insts.len);
    if (seen_buf == NULL) return STOP;
    RegexSeen seen = {seen_buf, insts.len};
    memset(seen.ptr, 0, insts.len * sizeof(bool));
    size_t stack_len = 2 * insts.len + 1;
    uint16_t *stack_buf = ARENA_ARRAY(tree, uint16_t, stack_len);
    if (stack_buf == NULL) return STOP;
    RegexPcs stack = {stack_buf, stack_len};
    size_t top = 1;
    at(stack, 0) = 0;
    RegexSet got = regex_set_empty();
    while (top > 0) {
        top--;
        uint16_t pc = at(stack, top);
        if (at(seen, pc)) continue;
        at(seen, pc) = true;
        RegexInst i = at(insts, pc);
        switch (i.op) {
        case REGEX_OP_BYTE:
            regex_set_add(&got, i.c);
            break;
        case REGEX_OP_ANY:
            regex_set_range(&got, 0, 255);
            regex_set_remove(&got, '\n');
            break;
        case REGEX_OP_SET:
            regex_set_union(&got, &at(sets, i.x));
            break;
        case REGEX_OP_MATCH: return OK;
        case REGEX_OP_JMP:
            at(stack, top) = i.x;
            top++;
            break;
        case REGEX_OP_SPLIT:
            at(stack, top) = i.x;
            at(stack, top + 1) = i.y;
            top += 2;
            break;
        case REGEX_OP_FIRST:
            at(stack, top) = (uint16_t)(pc + 1);
            at(stack, top + 1) = i.y;
            top += 2;
            break;
        default:
            at(stack, top) = (uint16_t)(pc + 1);
            top++;
            break;
        }
    }
    if (regex_set_count(&got) == 256) return OK;
    *has = true;
    *out = got;
    return OK;
}

// The program's parts once emitted: each instruction's loops, from the turns'
// ranges, and where its states start.
static STATUS_MUST_USE Status regex_states(Arena *a, RegexRefusal *refused, RegexInsts insts, const RegexRangeVec *ranges, RegexProgram *p) {
    RegexEncl *encl_buf = ARENA_ARRAY(a, RegexEncl, insts.len);
    if (encl_buf == NULL) return STOP;
    RegexEncls encl = {encl_buf, insts.len};
    memset(encl.ptr, 0, insts.len * sizeof(RegexEncl));
    for (size_t k = 0; k < ranges->len; k++) {
        RegexRange rg = at(*ranges, k);
        for (size_t pc = rg.from; pc <= rg.to; pc++) {
            RegexEncl *en = &at(encl, pc);
            RegexIds ids = {en->ids, en->n};
            if (ids.len > 0 && mem_find_byte(ids.ptr, rg.id, ids.len) != NULL) continue;
            if (en->n == REGEX_NEST_MOST) return regex_refuse(refused, 0, S("loops whose turn can match nothing (`(a*)*`), nested more than four deep"));
            RegexIds room = {en->ids, REGEX_NEST_MOST};
            at(room, en->n) = rg.id;
            en->n++;
        }
    }
    uint32_t *base_buf = ARENA_ARRAY(a, uint32_t, insts.len);
    if (base_buf == NULL) return STOP;
    RegexBases base = {base_buf, insts.len};
    uint32_t states = 0;
    for (size_t pc = 0; pc < insts.len; pc++) {
        at(base, pc) = states;
        states += (uint32_t)1 << at(encl, pc).n;
    }
    p->encl = encl;
    p->base = base;
    p->states = states;
    return OK;
}

static STATUS_MUST_USE Status regex_compile_in(Arena *a, Arena *tree, Str pattern, RegexOptions options, RegexProgram *out, RegexRefusal *refused) {
    RegexReader r = {.p = pattern, .tree = tree, .refused = refused, .options = options, .sets = RegexSetVec_make(a)};
    RegexAlt top;
    TRY(regex_alt(&r, false, &top));
    RegexEmitter e = {.insts = RegexInstVec_make(a), .refused = refused, .ranges = RegexRangeVec_make(tree)};
    TRY(regex_emit_alt(&e, top));
    TRY(regex_emit(&e, REGEX_OP_MATCH, 0, 0, 0, NULL));
    RegexProgram p;
    memset(&p, 0, sizeof p);
    p.insts.ptr = e.insts.ptr;
    p.insts.len = e.insts.len;
    TRY(regex_states(a, refused, p.insts, &e.ranges, &p));
    p.sets.ptr = r.sets.ptr;
    p.sets.len = r.sets.len;
    TRY(regex_first_bytes(tree, p.insts, p.sets, &p.has_first, &p.first));
    p.first_only = p.has_first && regex_set_count(&p.first) == 1 ? regex_set_lowest(&p.first) : -1;
    p.groups = r.groups;
    p.anchored = regex_anchored_all(top);
    *out = p;
    return OK;
}

// Compiles `pattern` into `a`. STOP with `refused->refused` set when the
// pattern is refused (where and why), and otherwise when an arena refused,
// `a->hit` set. What a failed compile took of `a` stays until its owner frees
// it.
static STATUS_MUST_USE Status regex_compile(Arena *a, Str pattern, RegexOptions options, RegexProgram *out, RegexRefusal *refused) {
    refused->refused = false;
    // DRAGON: the C stack. The reader, regex_nullable and the emitter each
    // recurse once a `(`, with no depth budget of their own, so a pattern of
    // thousands of `(` would overflow the stack.
    // REMEDY: this cap on the pattern's length (REGEX_PATTERN_MOST, 256 bytes),
    // checked before a byte is read, bounds each recursion at 256 levels.
    if (pattern.len > REGEX_PATTERN_MOST) return regex_refuse(refused, REGEX_PATTERN_MOST, S("a pattern of more than 256 bytes"));
    // DRAGON: the arena budget. The tree is a second arena beside `a`, so a
    // compile could hold more than the budget's owner gave if the tree had a
    // limit of its own.
    // REMEDY: the tree's limit is what `a` had left (`a->limit - a->held`), and
    // arena_deinit gives the tree back before this returns, so a compile holds
    // at most twice what was left, at its peak, and nothing after.
    Arena tree = arena_make(arena_sat_sub(a->limit, a->held));
    Status s = regex_compile_in(a, &tree, pattern, options, out, refused);
    if (s != OK && !refused->refused) arena_set_hit(a);
    arena_deinit(&tree);
    return s;
}

// ── Searching ────────────────────────────────────────────────────────────

static STATUS_MUST_USE Status regex_list_make(Arena *a, size_t n, RegexList *out) {
    RegexThread *threads = ARENA_ARRAY(a, RegexThread, n);
    if (threads == NULL) return STOP;
    uint64_t *stamp = ARENA_ARRAY(a, uint64_t, n);
    if (stamp == NULL) return STOP;
    memset(stamp, 0, n * sizeof(uint64_t));
    RegexList l = {{threads, n}, {stamp, n}, 1, 0};
    *out = l;
    return OK;
}

// A program's scratch, in `a`; STOP with `a->hit` set when it is refused.
static STATUS_MUST_USE Status regex_scratch_make(Arena *a, const RegexProgram *p, RegexScratch *out) {
    size_t n = p->states;
    TRY(regex_list_make(a, n, &out->a));
    TRY(regex_list_make(a, n, &out->b));
    RegexThread *stack = ARENA_ARRAY(a, RegexThread, n + 1);
    if (stack == NULL) return STOP;
    out->stack.ptr = stack;
    out->stack.len = n + 1;
    return OK;
}

static void regex_list_reset(RegexList *l) {
    l->len = 0;
    l->now++;
}

SLICE_TYPE(RegexCapSlots, size_t);

static RegexCapSlots regex_caps(RegexThread *t) {
    RegexCapSlots c = {t->caps, REGEX_CAPS};
    return c;
}

// A new thread at the program's start, its match begun at `pos`.
static RegexThread regex_start_thread(size_t pos) {
    RegexThread t;
    t.pc = 0;
    t.flags = 0;
    RegexCapSlots c = regex_caps(&t);
    for (size_t k = 0; k < c.len; k++) at(c, k) = SIZE_MAX;
    at(c, 0) = pos;
    return t;
}

// The state a thread is in: its instruction and the bits of the loops around
// it.
static uint32_t regex_state_of(const RegexProgram *p, uint16_t pc, uint16_t flags) {
    RegexEncl en = at(p->encl, pc);
    RegexIds ids = {en.ids, en.n};
    uint32_t k = 0;
    for (size_t i = 0; i < ids.len; i++) {
        if (flags & ((uint16_t)1 << at(ids, i))) k |= (uint32_t)1 << i;
    }
    return at(p->base, pc) + k;
}

static bool regex_at_edge(Str s, size_t pos) {
    bool before = pos > 0 && regex_word_byte(str_byte(s, pos - 1));
    bool after = pos < s.len && regex_word_byte(str_byte(s, pos));
    return before != after;
}

// Follows every path from `first` to a consuming instruction or `match`,
// adding the threads to `list` in order of preference at `pos`.
static void regex_add(const RegexProgram *p, RegexScratch *sc, RegexList *list, RegexThread first, size_t pos, Str s, uint64_t *work) {
    size_t top = 1;
    at(sc->stack, 0) = first;
    while (top > 0) {
        top--;
        RegexThread t = at(sc->stack, top);
        for (;;) {
            *work += 1;
            uint32_t st = regex_state_of(p, t.pc, t.flags);
            // DRAGON: the step budget. Without a bound on what a position
            // follows, an empty loop (`(a*)*`) would spin and alternatives
            // would multiply, and the lists and the stack would outgrow the
            // `states` (and one) they were made with.
            // REMEDY: this stamp follows a state at most once a position, so a
            // list gets at most one thread a state and the stack at most one a
            // split's state; `at` traps if a list or the stack ever overflows.
            if (at(list->stamp, st) == list->now) break;
            at(list->stamp, st) = list->now;
            RegexInst i = at(p->insts, t.pc);
            uint16_t bit = (uint16_t)((uint16_t)1 << (i.x & 15));
            bool took = (t.flags & bit) != 0;
            bool go = true;
            switch (i.op) {
            case REGEX_OP_TURN:
                t.flags &= (uint16_t)~bit;
                t.pc++;
                break;
            case REGEX_OP_AGAIN:
                go = took;
                if (go) t.pc++;
                break;
            case REGEX_OP_FIRST:
                t.pc = took ? (uint16_t)(t.pc + 1) : i.y;
                break;
            case REGEX_OP_JMP:
                t.pc = i.x;
                break;
            case REGEX_OP_SPLIT: {
                RegexThread second = t;
                second.pc = i.y;
                at(sc->stack, top) = second;
                top++;
                t.pc = i.x;
                break;
            }
            case REGEX_OP_SAVE:
                at(regex_caps(&t), 1 + (size_t)i.x) = pos;
                t.pc++;
                break;
            case REGEX_OP_START:
                go = pos == 0;
                if (go) t.pc++;
                break;
            case REGEX_OP_END:
                go = pos == s.len;
                if (go) t.pc++;
                break;
            case REGEX_OP_BOUNDARY:
                go = regex_at_edge(s, pos) == (i.c == 0);
                if (go) t.pc++;
                break;
            case REGEX_OP_BYTE:
            case REGEX_OP_ANY:
            case REGEX_OP_SET:
            case REGEX_OP_MATCH:
                at(list->threads, list->len) = t;
                list->len++;
                go = false;
                break;
            }
            if (!go) break;
        }
    }
}

// The first byte at or after `from` that a match can begin with, or SIZE_MAX.
// `only` is the set's one byte when it has one, else -1.
static size_t regex_next_first(const RegexSet *first, int only, Str s, size_t from) {
    if (from >= s.len) return SIZE_MAX;
    Str rest = str_sub(s, from, s.len);
    if (only >= 0) {
        const char *hit = mem_find_byte(rest.ptr, only, rest.len);
        return hit == NULL ? SIZE_MAX : from + (size_t)(hit - rest.ptr);
    }
    for (size_t i = 0; i < rest.len; i++) {
        if (regex_set_has(first, str_byte(rest, i))) return from + i;
    }
    return SIZE_MAX;
}

// A match's place and groups from the thread that reached `match`.
static RegexFound regex_found(RegexThread *t, size_t end) {
    RegexCapSlots caps = regex_caps(t);
    RegexFound f;
    f.start = at(caps, 0);
    f.end = end;
    RegexGroups groups = {f.groups, REGEX_GROUPS_MOST};
    for (size_t k = 0; k < groups.len; k++) {
        size_t a = at(caps, 1 + 2 * k);
        size_t b = at(caps, 2 + 2 * k);
        bool both = a != SIZE_MAX && b != SIZE_MAX;
        RegexGroup g = {both, both ? a : 0, both ? b : 0};
        at(groups, k) = g;
    }
    return f;
}

// The leftmost match of `p` in `s` that starts at `from` or later, the one a
// backtracker finds first, with `^`, `$` and `\b` seeing the whole of `s`;
// unfinished past either of `most`'s counts. The answer goes into `*r`, and a
// miss never writes its `match`, so a search zeroes none of its 272 bytes. A
// caller that searches once a line (re_lines, `run`'s `re`) wants this form:
// GCC builds a RegexSearched returned by value apart and copies it out whole,
// a `rep movsq` a search, which the wrappers below still pay.
static void regex_search_into(const RegexProgram *p, RegexScratch *sc, Str s, size_t from, RegexMost most, RegexSearched *r) {
    if (from > s.len) regex_bug("a search from past the string's end");
    r->found = false;
    r->finished = true;
    RegexList *clist = &sc->a;
    RegexList *nlist = &sc->b;
    regex_list_reset(clist);
    bool matched = false;
    // Read only once `matched` is set, which sets it whole first.
    RegexThread match;
    size_t match_end = 0;
    uint64_t positions = 0;
    uint64_t skipped = 0;
    uint64_t work = 0;
    bool skips = !p->anchored && p->has_first;
    int only = skips ? p->first_only : -1;
    for (size_t pos = from;; pos++) {
        // No thread alive and none matched: a start can live only at a byte
        // that can begin a match, so the bytes before the next one are passed
        // over.
        if (skips && !matched && clist->len == 0) {
            size_t next = regex_next_first(&p->first, only, s, pos);
            skipped += (next == SIZE_MAX ? s.len : next) - pos;
            if (skipped > most.skipped) {
                r->finished = false;
                break;
            }
            if (next == SIZE_MAX) break;
            pos = next;
        }
        if (positions == most.positions) {
            r->finished = false;
            break;
        }
        positions++;
        // A new start at each position, least preferred, until a match.
        if (!matched && (!p->anchored || pos == 0)) regex_add(p, sc, clist, regex_start_thread(pos), pos, s, &work);
        regex_list_reset(nlist);
        for (size_t k = 0; k < clist->len; k++) {
            RegexThread t = at(clist->threads, k);
            work++;
            RegexInst i = at(p->insts, t.pc);
            if (i.op == REGEX_OP_MATCH) {
                // Every less preferred thread is cut.
                matched = true;
                match = t;
                match_end = pos;
                break;
            }
            if (pos == s.len) continue;
            uint8_t b = str_byte(s, pos);
            // `regex_add` leaves only consuming instructions and `match` on a
            // list.
            bool ok = i.op == REGEX_OP_BYTE ? b == i.c : i.op == REGEX_OP_ANY ? b != '\n' : regex_set_has(&at(p->sets, i.x), b);
            if (ok) {
                RegexThread next = t;
                next.pc = (uint16_t)(t.pc + 1);
                next.flags = UINT16_MAX;
                regex_add(p, sc, nlist, next, pos + 1, s, &work);
            }
        }
        RegexList *swap = clist;
        clist = nlist;
        nlist = swap;
        if (pos == s.len) break;
        if (clist->len == 0 && (matched || p->anchored)) break;
    }
    r->positions = positions;
    r->skipped = skipped;
    r->work = work;
    // An unfinished search found nothing, though a match had been seen.
    if (!r->finished || !matched) return;
    r->found = true;
    r->match = regex_found(&match, match_end);
}

static inline RegexSearched regex_search_within(const RegexProgram *p, RegexScratch *sc, Str s, size_t from, RegexMost most) {
    RegexSearched r;
    regex_search_into(p, sc, s, from, most, &r);
    return r;
}

static inline RegexSearched regex_search_from(const RegexProgram *p, RegexScratch *sc, Str s, size_t from, uint64_t positions_most) {
    RegexMost most = {positions_most, UINT64_MAX};
    return regex_search_within(p, sc, s, from, most);
}

static inline RegexSearched regex_search(const RegexProgram *p, RegexScratch *sc, Str s, uint64_t positions_most) {
    return regex_search_from(p, sc, s, 0, positions_most);
}

// ── The cache ────────────────────────────────────────────────────────────

static RegexEntries regex_entries(RegexCache *c) {
    RegexEntries e = {c->entries, REGEX_CACHE_MOST};
    return e;
}

static RegexCache regex_cache_make(void) {
    RegexCache c;
    memset(&c, 0, sizeof c);
    return c;
}

// The cached entry for the pattern, marked used, or NULL.
static RegexCached *regex_cache_get(RegexCache *c, Str pattern, RegexOptions options) {
    c->clock++;
    RegexEntries es = regex_entries(c);
    for (size_t k = 0; k < es.len; k++) {
        RegexCached *got = &at(es, k);
        if (got->present && got->nocase == options.nocase && str_eq(got->text, pattern)) {
            got->used = c->clock;
            return got;
        }
    }
    return NULL;
}

// What the cache's entries hold, for the machine budget's count.
static size_t regex_cache_held(RegexCache *c) {
    size_t held = 0;
    RegexEntries es = regex_entries(c);
    for (size_t k = 0; k < es.len; k++) {
        if (at(es, k).present) held += at(es, k).arena.held;
    }
    return held;
}

static STATUS_MUST_USE Status regex_cached_fill(RegexCached *got, Str pattern, RegexOptions options, RegexRefusal *refused) {
    TRY(regex_compile(&got->arena, pattern, options, &got->program, refused));
    TRY(regex_scratch_make(&got->arena, &got->program, &got->scratch));
    if (!arena_dup(&got->arena, pattern, &got->text)) return STOP;
    got->nocase = options.nocase;
    return OK;
}

// Compiles and caches a pattern `regex_cache_get` did not find, in an arena of
// its own of at most `limit` bytes, evicting the least recently used entry when
// all eight are taken, and gives it in `*out`. STOP with `refused->refused` set
// when the pattern is refused, and otherwise when `limit` was too small (the
// machine budget); either way the cache is as it was.
static STATUS_MUST_USE Status regex_cache_put(RegexCache *c, Str pattern, RegexOptions options, size_t limit, RegexRefusal *refused, RegexCached **out) {
    RegexCached fresh;
    memset(&fresh, 0, sizeof fresh);
    fresh.arena = arena_make(limit);
    if (regex_cached_fill(&fresh, pattern, options, refused) != OK) {
        arena_deinit(&fresh.arena);
        return STOP;
    }
    RegexEntries es = regex_entries(c);
    size_t slot = 0;
    for (size_t k = 0; k < es.len; k++) {
        const RegexCached *e = &at(es, k);
        uint64_t e_used = e->present ? e->used : 0;
        uint64_t slot_used = at(es, slot).present ? at(es, slot).used : 0;
        if (!e->present || e_used < slot_used) slot = k;
        if (!e->present) break;
    }
    RegexCached *dest = &at(es, slot);
    // DRAGON: use after free. The evicted entry's arena goes back to the system
    // here, so a RegexCached pointer, a program or a match's text taken from it
    // before this put dangles.
    // REMEDY: nothing enforces it; by convention a caller holds an entry only
    // between its get or put and the next put, and each regex builtin
    // (builtin_program's callers, in builtins_text.c and host_text.c) takes one
    // pattern and is done with it before it returns.
    if (dest->present) arena_deinit(&dest->arena);
    fresh.present = true;
    fresh.used = c->clock;
    *dest = fresh;
    *out = dest;
    return OK;
}

static void regex_cache_deinit(RegexCache *c) {
    RegexEntries es = regex_entries(c);
    for (size_t k = 0; k < es.len; k++) {
        if (at(es, k).present) arena_deinit(&at(es, k).arena);
    }
    *c = regex_cache_make();
}
