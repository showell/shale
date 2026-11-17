// The keep's tests: src/shale_keep.zig's, with the same inputs and answers.
// The random rounds draw from Zig's own generator (DefaultPrng, seed 233), so
// they are Zig's 3000 outputs, options and cuts, and their answers fold to the
// fingerprint Zig's test prints of the same rounds.
#include "test.h"

#include "keep.h"

// Zig's `std.Random.DefaultPrng`: Xoshiro256++ seeded by SplitMix64.
typedef struct {
    uint64_t s0;
    uint64_t s1;
    uint64_t s2;
    uint64_t s3;
} Prng;

static uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t splitmix_next(uint64_t *s) {
    *s += 0x9e3779b97f4a7c15ULL;
    uint64_t z = *s;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void prng_init(Prng *p, uint64_t seed) {
    uint64_t g = seed;
    p->s0 = splitmix_next(&g);
    p->s1 = splitmix_next(&g);
    p->s2 = splitmix_next(&g);
    p->s3 = splitmix_next(&g);
}

static uint64_t prng_next(Prng *p) {
    uint64_t r = rotl(p->s0 + p->s3, 23) + p->s0;
    uint64_t t = p->s1 << 17;
    p->s2 ^= p->s0;
    p->s3 ^= p->s1;
    p->s1 ^= p->s2;
    p->s0 ^= p->s3;
    p->s2 ^= t;
    p->s3 = rotl(p->s3, 45);
    return r;
}

// The 128-bit product of two 64-bit numbers, as its high and low halves:
// Zig's `mulWide(u64, ...)`, by hand, since TCC has no 128-bit integer.
static void mul_wide(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
    uint64_t a_lo = a & 0xffffffffULL;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = b & 0xffffffffULL;
    uint64_t b_hi = b >> 32;
    uint64_t ll = a_lo * b_lo;
    uint64_t lh = a_lo * b_hi;
    uint64_t hl = a_hi * b_lo;
    uint64_t hh = a_hi * b_hi;
    uint64_t mid = (ll >> 32) + (lh & 0xffffffffULL) + (hl & 0xffffffffULL);
    *lo = (ll & 0xffffffffULL) | (mid << 32);
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

// Zig's `uintLessThan(usize, n)`: Lemire's method, as Zig writes it.
static uint64_t prng_less_than(Prng *p, uint64_t n) {
    uint64_t hi = 0;
    uint64_t lo = 0;
    mul_wide(prng_next(p), n, &hi, &lo);
    if (lo < n) {
        uint64_t t = (uint64_t)0 - n;
        if (t >= n) {
            t -= n;
            if (t >= n) t %= n;
        }
        while (lo < t) mul_wide(prng_next(p), n, &hi, &lo);
    }
    return hi;
}

// Zig's `uintLessThan(u8, n)`: `int(u8)` is one draw's low byte.
static uint8_t prng_less_than_u8(Prng *p, uint8_t n) {
    uint16_t m = (uint16_t)((uint8_t)prng_next(p) * (uint16_t)n);
    uint8_t l = (uint8_t)m;
    if (l < n) {
        uint8_t t = (uint8_t)(0 - n);
        if (t >= n) {
            t = (uint8_t)(t - n);
            if (t >= n) t = (uint8_t)(t % n);
        }
        while (l < t) {
            m = (uint16_t)((uint8_t)prng_next(p) * (uint16_t)n);
            l = (uint8_t)m;
        }
    }
    return (uint8_t)(m >> 8);
}

static uint64_t prng_at_most(Prng *p, uint64_t n) {
    return prng_less_than(p, n + 1);
}

TEST(prng_is_zigs) {
    // Zig's own test of Xoshiro256 seeded with 0, its first three draws.
    Prng p;
    prng_init(&p, 0);
    EXPECT_EQ(prng_next(&p), 0x53175d61490b23dfULL);
    EXPECT_EQ(prng_next(&p), 0x61da6f3dc380d507ULL);
    EXPECT_EQ(prng_next(&p), 0x5c0fdf91ec9a7bfcULL);
}

// The patterns of Zig's tests, matched as Shale's regex matches them on a
// line (no newline in it): `error|FAIL`, `^ok`, `x$`, `warn`, `error`.
typedef enum { PAT_ERROR_OR_FAIL, PAT_STARTS_OK, PAT_ENDS_X, PAT_WARN, PAT_ERROR } PatternKind;

static bool str_holds(Str s, Str t) {
    if (t.len > s.len) return false;
    for (size_t i = 0; i + t.len <= s.len; i++) {
        if (str_eq(str_sub(s, i, i + t.len), t)) return true;
    }
    return false;
}

static bool pattern_matches(void *ctx, Str line, uint64_t *work) {
    PatternKind k = *(PatternKind *)ctx;
    (*work)++;
    switch (k) {
    case PAT_ERROR_OR_FAIL:
        return str_holds(line, S("error")) || str_holds(line, S("FAIL"));
    case PAT_STARTS_OK:
        return str_starts_with(line, S("ok"));
    case PAT_ENDS_X:
        return line.len > 0 && at(line, line.len - 1) == 'x';
    case PAT_WARN:
        return str_holds(line, S("warn"));
    default:
        return str_holds(line, S("error"));
    }
}

static const PatternKind KINDS[5] = {PAT_ERROR_OR_FAIL, PAT_STARTS_OK, PAT_ENDS_X, PAT_WARN, PAT_ERROR};
SLICE_TYPE(Kinds, const PatternKind);

// The pattern `KINDS`'s `i`.
static KeepPattern pattern_of(size_t i) {
    Kinds kinds = {KINDS, 5};
    KeepPattern p = {(void *)&at(kinds, i), pattern_matches};
    return p;
}

static KeepPattern pattern_none(void) {
    KeepPattern p = {NULL, NULL};
    return p;
}

// What the keep keeps of `out` fed in pieces of `cut_at`: a line's end may
// come in a later piece. False when the arena refuses.
static bool kept_of(Arena *a, Str out, KeepOptions o, size_t cut_at, Kept *k) {
    Keep keep;
    if (keep_init(&keep, a, o) != OK) return false;
    size_t done = 0;
    while (done < out.len) {
        size_t n = out.len - done < cut_at ? out.len - done : cut_at;
        if (keep_feed(&keep, str_sub(out, done, done + n)) != OK) return false;
        done += n;
    }
    return keep_finish(&keep, k) == OK;
}

// The test oracle, Zig's: the output split into lines, filtered with grep's
// context rules, each cut, then the first `head` and the last `tail` with a
// mark where lines were dropped. Plainly right, and slow.
VEC_DEFINE(Strs, Str);
VEC_DEFINE(Bools, bool);

static STATUS_MUST_USE Status oracle(Arena *a, Str out, KeepOptions o, Str *said) {
    Strs all = Strs_make(a);
    size_t from = 0;
    for (size_t i = 0; i <= out.len; i++) {
        if (i == out.len || at(out, i) == '\n') {
            TRY(Strs_push(&all, str_sub(out, from, i)));
            from = i + 1;
        }
    }
    if (all.len > 0 && at(all, all.len - 1).len == 0) (void)Strs_pop(&all);
    uint64_t work = 0;
    Strs lines = Strs_make(a);
    for (size_t i = 0; i < all.len; i++) {
        Str l = at(all, i);
        Str head = str_sub(l, 0, l.len < KEEP_LINE_MOST ? l.len : KEEP_LINE_MOST);
        if (o.not_re.matches == NULL || !o.not_re.matches(o.not_re.ctx, head, &work)) TRY(Strs_push(&lines, l));
    }
    Bools hits = Bools_make(a);
    for (size_t i = 0; i < lines.len; i++) {
        Str l = at(lines, i);
        Str head = str_sub(l, 0, l.len < KEEP_LINE_MOST ? l.len : KEEP_LINE_MOST);
        TRY(Bools_push(&hits, o.re.matches == NULL || o.re.matches(o.re.ctx, head, &work)));
    }
    Strs passed = Strs_make(a);
    for (size_t i = 0; i < lines.len; i++) {
        bool keep = at(hits, i);
        for (size_t d = 1; !keep && d <= o.after && d <= i; d++) keep = at(hits, i - d);
        for (size_t d = 1; !keep && d <= o.before && i + d < lines.len; d++) keep = at(hits, i + d);
        if (!keep) continue;
        Str l = at(lines, i);
        if (l.len > o.width) {
            Buf b = buf_make(a);
            TRY(buf_str(&b, str_sub(l, 0, o.width)));
            TRY(buf_lit(&b, KEEP_CUT_MARK));
            l = buf_view(&b);
        }
        TRY(Strs_push(&passed, l));
    }
    Buf b = buf_make(a);
    size_t h = o.head < passed.len ? o.head : passed.len;
    for (size_t i = 0; i < h; i++) {
        TRY(buf_str(&b, at(passed, i)));
        TRY(buf_byte(&b, '\n'));
    }
    size_t rest = passed.len - h;
    size_t t = o.tail < rest ? o.tail : rest;
    if (rest > t) {
        TRY(buf_lit(&b, KEEP_CUT_MARK " "));
        TRY(buf_u64(&b, rest - t));
        TRY(rest - t == 1 ? buf_lit(&b, " line left out " KEEP_CUT_MARK "\n") : buf_lit(&b, " lines left out " KEEP_CUT_MARK "\n"));
    }
    for (size_t i = passed.len - t; i < passed.len; i++) {
        TRY(buf_str(&b, at(passed, i)));
        TRY(buf_byte(&b, '\n'));
    }
    *said = buf_view(&b);
    return OK;
}

// FNV-1a, 64 bits: Zig's `std.hash.Fnv1a_64`.
static void fnv(uint64_t *h, Str s) {
    for (size_t i = 0; i < s.len; i++) {
        *h ^= (uint8_t)at(s, i);
        *h *= 0x100000001b3ULL;
    }
}

static void fnv_u64(uint64_t *h, uint64_t n) {
    char le[8];
    Bytes b = {le, 8};
    for (size_t i = 0; i < 8; i++) at(b, i) = (char)(n >> (8 * i));
    fnv(h, (Str){le, 8});
}

static uint64_t count_newlines(Str s) {
    uint64_t n = 0;
    for (size_t i = 0; i < s.len; i++) n += at(s, i) == '\n';
    return n;
}

TEST(head_and_tail_and_the_filter_against_the_oracle_on_zigs_random_outputs) {
    Prng p;
    prng_init(&p, 233);
    static const Str pieces_all[11] = {S("ok"), S("error"), S("FAIL"), S("x"), S("\n"), S("\n"), S("\n"), S("\r\n"), S("\x00"), S(" "), S("warn")};
    SLICE_TYPE(Pieces, const Str);
    Pieces pieces = {pieces_all, 11};
    // The patterns' order: none, `error|FAIL`, `^ok`, `x$`, `warn`.
    uint64_t fp = 0xcbf29ce484222325ULL;
    for (int round = 0; round < 3000; round++) {
        Arena a = arena_make(1 << 24);
        Buf out = buf_make(&a);
        uint64_t n = prng_at_most(&p, 60);
        for (uint64_t i = 0; i < n; i++) EXPECT(buf_str(&out, at(pieces, prng_less_than(&p, pieces.len))) == OK);
        if (prng_less_than_u8(&p, 8) == 0) {
            for (int i = 0; i < 300; i++) EXPECT(buf_byte(&out, 'L') == OK);
        }
        KeepOptions o;
        keep_options_default(&o);
        o.head = prng_at_most(&p, 4);
        o.tail = prng_at_most(&p, 4);
        o.width = 1 + prng_at_most(&p, 12);
        o.before = prng_at_most(&p, 2);
        o.after = prng_at_most(&p, 2);
        uint64_t re = prng_less_than(&p, 5);
        o.re = re == 0 ? pattern_none() : pattern_of(re - 1);
        o.not_re = prng_less_than_u8(&p, 4) == 0 ? pattern_of(3) : pattern_none();
        size_t cut_at = 1 + prng_at_most(&p, 7);
        Kept got;
        EXPECT(kept_of(&a, buf_view(&out), o, cut_at, &got));
        Str want;
        EXPECT(oracle(&a, buf_view(&out), o, &want) == OK);
        if (!str_eq(got.out, want)) fprintf(stderr, "  round %d\n", round);
        EXPECT_STR(got.out, want);
        Str o_s = buf_view(&out);
        uint64_t total = count_newlines(o_s) + (o_s.len > 0 && at(o_s, o_s.len - 1) != '\n');
        EXPECT_EQ(got.total, total);
        EXPECT(got.kept <= o.head + o.tail);
        EXPECT(got.matched <= got.total);
        fnv(&fp, got.out);
        fnv_u64(&fp, got.total);
        fnv_u64(&fp, got.matched);
        fnv_u64(&fp, got.kept);
        arena_deinit(&a);
    }
    // Zig's test of the same 3000 rounds, folded the same way.
    EXPECT_EQ(fp, 0xa929a50cc0904effULL);
}

TEST(zigs_first_round_exactly) {
    // Round 0 as Zig's test drew it: head 0, tail 0, width 3, before 1, re `x$`.
    Arena a = arena_make(1 << 20);
    KeepOptions o;
    keep_options_default(&o);
    o.width = 3;
    o.before = 1;
    o.re = pattern_of(2);
    Kept k;
    Str out = S("\x00\nerrorerrorerror xx\x00 \n\nok\x00\nok\nok\nFAIL\r\nFAILerror\x00\nok\nx");
    EXPECT(kept_of(&a, out, o, 3, &k));
    EXPECT_STR(k.out, S("\xe2\x80\xa6 2 lines left out \xe2\x80\xa6\n"));
    EXPECT_EQ(k.total, 10);
    EXPECT_EQ(k.matched, 1);
    EXPECT_EQ(k.kept, 0);
    arena_deinit(&a);
}

TEST(the_edges) {
    Arena a = arena_make(1 << 24);
    KeepOptions o;
    keep_options_default(&o);
    o.head = 2;
    o.tail = 2;
    Kept empty;
    EXPECT(kept_of(&a, S(""), o, 4, &empty));
    EXPECT_STR(empty.out, S(""));
    EXPECT_EQ(empty.total, 0);
    Kept short_k;
    EXPECT(kept_of(&a, S("a\nb\nc"), o, 1, &short_k));
    EXPECT_STR(short_k.out, S("a\nb\nc\n"));
    EXPECT_EQ(short_k.kept, 3);
    Kept many;
    EXPECT(kept_of(&a, S("1\n2\n3\n4\n5\n6\n7\n"), o, 3, &many));
    EXPECT_STR(many.out, S("1\n2\n\xe2\x80\xa6 3 lines left out \xe2\x80\xa6\n6\n7\n"));
    EXPECT_EQ(many.total, 7);
    // A line past KEEP_LINE_MOST is matched on its first part and kept cut.
    Buf huge = buf_make(&a);
    EXPECT(buf_lit(&huge, "error ") == OK);
    // One append of the z's: GCC 13 at -O2 takes buf_byte's first growth
    // of a buffer for a write past an empty one (-Warray-bounds), wrongly.
    static char zs[KEEP_LINE_MOST + 10];
    memset(zs, 'z', sizeof zs);
    EXPECT(buf_str(&huge, (Str){zs, sizeof zs}) == OK);
    EXPECT(buf_lit(&huge, " tail\nok\n") == OK);
    KeepOptions ho;
    keep_options_default(&ho);
    ho.head = 5;
    ho.width = 8;
    ho.re = pattern_of(4);
    Kept h;
    EXPECT(kept_of(&a, buf_view(&huge), ho, 4096, &h));
    EXPECT_STR(h.out, S("error zz\xe2\x80\xa6\n"));
    EXPECT_EQ(h.matched, 1);
    EXPECT_EQ(h.total, 2);
    // With a width past KEEP_LINE_MOST, the line is held to its first
    // KEEP_LINE_MOST bytes and still marked cut, as Zig's `line_over` says.
    KeepOptions wide;
    keep_options_default(&wide);
    wide.head = 1;
    wide.width = 100000;
    Kept w;
    EXPECT(kept_of(&a, str_sub(buf_view(&huge), 6, 6 + KEEP_LINE_MOST + 10), wide, 4096, &w));
    EXPECT_EQ(w.out.len, KEEP_LINE_MOST + 4);
    EXPECT_STR(str_sub(w.out, KEEP_LINE_MOST, w.out.len), S("\xe2\x80\xa6\n"));
    EXPECT_EQ(w.total, 1);
    // head 0 and tail 0: nothing kept, the counts still counted.
    KeepOptions no;
    keep_options_default(&no);
    no.re = pattern_of(4);
    Kept none;
    EXPECT(kept_of(&a, S("a\nerror\nb\n"), no, 2, &none));
    EXPECT_STR(none.out, S("\xe2\x80\xa6 1 line left out \xe2\x80\xa6\n"));
    EXPECT_EQ(none.matched, 1);
    EXPECT_EQ(none.total, 3);
    EXPECT_EQ(none.kept, 0);
    arena_deinit(&a);
}

TEST(an_arena_refused_anywhere_stops_and_never_answers_wrong) {
    // Zig's test refuses each allocation in turn; an arena takes few chunks,
    // so here every limit from 0 up is tried, each refusing wherever the keep
    // reaches it. A refusal is a STOP with `hit` set, never another answer.
    Str out = S("a\nb\nc\nerror one\nd\ne\nf\ng\nerror two\nh\n");
    KeepOptions o;
    keep_options_default(&o);
    o.re = pattern_of(4);
    o.before = 3;
    o.after = 1;
    o.head = 8;
    o.tail = 1;
    size_t refused = 0;
    size_t limit = 0;
    for (;; limit += 8) {
        Arena a = arena_make(limit);
        Kept k;
        bool ok = kept_of(&a, out, o, 5, &k);
        if (ok) {
            EXPECT(!a.hit);
            EXPECT_STR(k.out, S("a\nb\nc\nerror one\nd\ne\nf\ng\n\xe2\x80\xa6 1 line left out \xe2\x80\xa6\nh\n"));
            arena_deinit(&a);
            break;
        }
        EXPECT(a.hit);
        refused++;
        arena_deinit(&a);
    }
    EXPECT(refused > 5);
}

TEST(memory_stays_bounded_over_a_flood_of_lines) {
    // A million lines through a tail of 3: the slots are written over, never
    // taken anew, so the arena holds a few chunks, not the output.
    Arena a = arena_make(1 << 20);
    KeepOptions o;
    keep_options_default(&o);
    o.tail = 3;
    o.head = 1;
    Keep k;
    EXPECT(keep_init(&k, &a, o) == OK);
    for (int i = 0; i < 1000000; i++) EXPECT(keep_feed(&k, S("a line of output that is kept, then dropped\n")) == OK);
    Kept kept;
    EXPECT(keep_finish(&k, &kept) == OK);
    EXPECT_EQ(kept.total, 1000000);
    EXPECT_EQ(kept.kept, 4);
    EXPECT(a.held < 64 * 1024);
    EXPECT(!a.hit);
    arena_deinit(&a);
}

TEST_MAIN(TEST_CASE(prng_is_zigs), TEST_CASE(head_and_tail_and_the_filter_against_the_oracle_on_zigs_random_outputs),
          TEST_CASE(zigs_first_round_exactly), TEST_CASE(the_edges), TEST_CASE(an_arena_refused_anywhere_stops_and_never_answers_wrong),
          TEST_CASE(memory_stays_bounded_over_a_flood_of_lines))
