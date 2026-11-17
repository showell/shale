// test_regex.c: regex.c through its own functions, what a pattern compiles
// to, finds and refuses. The first ten tests are Zig Shale's regex tests,
// the same inputs held to the same answers; the rest hold the refusals'
// texts, the cache, and the bound on a hostile search's work.
#include "test.h"
#include "../script/regex.c"

enum { TEST_REGEX_LIMIT = 64 * 1024 * 1024 };

SLICE_TYPE(Strs, const Str);

typedef struct {
    Arena arena;
    RegexProgram p;
    RegexScratch sc;
} Compiled;

static RegexOptions opts(bool nocase) {
    RegexOptions o = {nocase};
    return o;
}

static void compiled_make(Compiled *c, Str pattern, RegexOptions o) {
    c->arena = arena_make(TEST_REGEX_LIMIT);
    RegexRefusal refused;
    Status s = regex_compile(&c->arena, pattern, o, &c->p, &refused);
    if (s != OK) fprintf(stderr, "  /%.*s/ refused at %zu: %.*s\n", (int)pattern.len, pattern.ptr, refused.at, (int)refused.message.len, refused.message.ptr);
    EXPECT(s == OK);
    EXPECT(regex_scratch_make(&c->arena, &c->p, &c->sc) == OK);
}

// Where `pattern` first matches in `s`.
static RegexSearched found(Str pattern, Str s) {
    Compiled c;
    compiled_make(&c, pattern, opts(false));
    RegexSearched r = regex_search(&c.p, &c.sc, s, s.len + 1);
    arena_deinit(&c.arena);
    return r;
}

static RegexRefusal refusal(Str pattern) {
    Arena a = arena_make(TEST_REGEX_LIMIT);
    RegexProgram p;
    RegexRefusal r;
    Status s = regex_compile(&a, pattern, opts(false), &p, &r);
    arena_deinit(&a);
    EXPECT(s == STOP);
    EXPECT(r.refused);
    EXPECT(!a.hit);
    return r;
}

// The group `k` (from 1) of a match.
static RegexGroup group_of(RegexSearched r, size_t k) {
    RegexGroups gs = {r.match.groups, REGEX_GROUPS_MOST};
    return at(gs, k - 1);
}

TEST(counted_repetition_is_the_part_written_out) {
    // {n}: exactly n.
    RegexSearched r = found(S("a{3}"), S("aaaa"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 3);
    EXPECT(!found(S("^a{3}$"), S("aa")).found);
    // {n,m}: as many as it can, up to m.
    r = found(S("a{2,4}"), S("aaaaa"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 4);
    EXPECT(!found(S("^a{2,4}$"), S("a")).found);
    // {n,}: at least n, then any number.
    r = found(S("a{2,}"), S("aaaaaa"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 6);
    EXPECT(!found(S("^a{2,}$"), S("a")).found);
    // {0} takes nothing; a class and a group count as one part each.
    r = found(S("ba{0}"), S("baa"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 1);
    r = found(S("\\d{4}"), S("2026-11"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 4);
    // A group repeated: its capture is its last turn's, as a backtracker's is.
    r = found(S("(ab){2}"), S("ababab"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 4);
    EXPECT(group_of(r, 1).matched);
    EXPECT_EQ(group_of(r, 1).start, 2);
    EXPECT_EQ(group_of(r, 1).end, 4);
    EXPECT(!group_of(r, 2).matched);
}

typedef struct {
    Str pattern;
    size_t at;
    Str message;
} Refused;

SLICE_TYPE(RefusedCases, const Refused);

static void expect_refusals(RefusedCases cases) {
    for (size_t k = 0; k < cases.len; k++) {
        Refused c = at(cases, k);
        RegexRefusal r = refusal(c.pattern);
        if (r.at != c.at || !str_eq(r.message, c.message)) fprintf(stderr, "  /%.*s/: at %zu: %.*s\n", (int)c.pattern.len, c.pattern.ptr, r.at, (int)r.message.len, r.message.ptr);
        EXPECT_EQ(r.at, c.at);
        EXPECT_STR(r.message, c.message);
    }
}

static const Str FORM = S("a count is `{n}`, `{n,}` or `{n,m}`, each a number of digits: `\\{` is the byte");

TEST(a_bad_count_is_refused_at_its_brace) {
    static const Refused cases[11] = {
        {S("{2}"), 0, S("a count (`{n,m}`) with nothing before it to repeat: `\\{` is the byte")},
        {S("a*{2}"), 2, S("a repetition repeated (`a*{2}`): one is enough")},
        {S("a{2}*"), 4, S("a repetition repeated (`a{2}*`): one is enough")},
        {S("a{x}"), 1, S("a count is `{n}`, `{n,}` or `{n,m}`, each a number of digits: `\\{` is the byte")},
        {S("a{2"), 1, S("a count is `{n}`, `{n,}` or `{n,m}`, each a number of digits: `\\{` is the byte")},
        {S("a{,3}"), 1, S("a count is `{n}`, `{n,}` or `{n,m}`, each a number of digits: `\\{` is the byte")},
        {S("a{5,2}"), 1, S("a count whose most is below its least (`{5,2}`): write it `{2,5}`")},
        {S("a{101}"), 1, S("a count past 100 (`{n,m}`): write `+` or `*` for a part that may come any number of times")},
        {S("a{99999999999999999999}"), 1, S("a count is `{n}`, `{n,}` or `{n,m}`, each a number of digits: `\\{` is the byte")},
        {S("^{2}"), 1, S("`^`, `$`, `\\b` and `\\B` are places, not bytes: they cannot repeat")},
        {S("a{2,5}?"), 6, S("lazy repetition (`{2,5}?`) is not in this subset: every repetition takes as much as it can")},
    };
    RefusedCases all = {cases, sizeof cases / sizeof(Refused)};
    expect_refusals(all);
    EXPECT_STR(at(all, 3).message, FORM);
    // Counts nested past the program's bound: refused, never a crash.
    RegexRefusal r = refusal(S("((a{100}){100}){100}"));
    EXPECT_EQ(r.at, 0);
    EXPECT_STR(r.message, S("the pattern, its counts written out, is more than 8192 instructions: count less (`{n,m}`), or write `+` or `*`"));
}

// `loop` written `n` times into `room`.
static Str repeated(Bytes room, Str loop, size_t n) {
    size_t len = 0;
    for (size_t k = 0; k < n; k++) {
        Bytes dest = SLICE_SUB(Bytes, room, len, len + loop.len);
        memcpy(dest.ptr, loop.ptr, loop.len);
        len += loop.len;
    }
    Str s = {room.ptr, len};
    return s;
}

TEST(sixteen_empty_loops_compile_and_a_seventeenth_is_refused_and_four_nested_but_not_five) {
    char buf[200];
    Bytes room = {buf, sizeof buf};
    Str loop = S("(?:a*)*");
    RegexSearched r = found(repeated(room, loop, 16), S("b"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 0);
    RegexRefusal many = refusal(repeated(room, loop, 17));
    EXPECT_EQ(many.at, 0);
    EXPECT_STR(many.message, S("more than 16 loops whose turn can match nothing (`(a*)*`, `(a|)+`): write some so a turn takes a byte"));

    r = found(S("(?:(?:(?:(?:a*)*)*)*)*"), S("aab"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 2);
    RegexRefusal deep = refusal(S("(?:(?:(?:(?:(?:a*)*)*)*)*)*"));
    EXPECT_EQ(deep.at, 0);
    EXPECT_STR(deep.message, S("loops whose turn can match nothing (`(a*)*`), nested more than four deep"));
}

TEST(control_escapes_are_the_control_bytes_alone_and_in_a_class) {
    RegexSearched r = found(S("\\n"), S("a\nb"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 1);
    r = found(S("\\t"), S("a\tb"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 1);
    r = found(S("\\r"), S("a\rb"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 1);
    r = found(S("[\\t\\r]"), S("ab\r"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 2);
    EXPECT(!found(S("\\n"), S("anb")).found);
}

TEST(a_class_holds_a_shorthand_an_escaped_byte_and_a_range_whose_end_is_escaped) {
    RegexSearched r = found(S("[\\d]"), S("abc7"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 3);
    EXPECT(!found(S("[\\d]"), S("abc")).found);
    r = found(S("[\\.]"), S("x."));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 1);
    EXPECT(!found(S("[\\.]"), S("xy")).found);
    // `!` through `/`, the end written `\/`.
    r = found(S("[!-\\/]"), S("a/"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 1);
    r = found(S("[!-\\/]"), S("a!"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 1);
    EXPECT(!found(S("[!-\\/]"), S("a0")).found);
}

TEST(a_pattern_of_more_than_256_bytes_is_refused_at_its_257th_byte_unread) {
    char buf[REGEX_PATTERN_MOST + 1];
    memset(buf, 'a', sizeof buf);
    Str whole = {buf, sizeof buf};
    RegexRefusal r = refusal(whole);
    EXPECT_EQ(r.at, REGEX_PATTERN_MOST);
    EXPECT_STR(r.message, S("a pattern of more than 256 bytes"));
    RegexSearched f = found(str_sub(whole, 0, REGEX_PATTERN_MOST), whole);
    EXPECT(f.found);
    EXPECT_EQ(f.match.end, REGEX_PATTERN_MOST);
}

// Whether the cache holds `pattern`, without marking it used.
static bool cache_holds(RegexCache *c, Str pattern) {
    RegexEntries es = regex_entries(c);
    for (size_t k = 0; k < es.len; k++) {
        if (at(es, k).present && str_eq(at(es, k).text, pattern)) return true;
    }
    return false;
}

static size_t cache_count(RegexCache *c) {
    size_t n = 0;
    RegexEntries es = regex_entries(c);
    for (size_t k = 0; k < es.len; k++) n += at(es, k).present;
    return n;
}

// Zig's FailingAllocator sweep, as the arena refuses: a compile, and a cache's
// put, at every limit below what they need. Each refusal is the arena's (no
// pattern refused), the cache is as it was (the entry it held before still
// there, nothing new, nothing more held), and the first limit that fits works.
// The second half of the Zig test, every resize refused, has no twin: the
// arena gives no way to refuse a resize alone.
TEST(compile_and_cache_put_leave_nothing_behind_when_the_arena_refuses) {
    Str pattern = S("(a|b)[x-z\\d]+c?");
    size_t limit = 0;
    for (;; limit++) {
        Arena a = arena_make(limit);
        RegexProgram p;
        RegexRefusal refused;
        Status s = regex_compile(&a, pattern, opts(false), &p, &refused);
        if (s == OK) {
            RegexScratch sc;
            Status made = regex_scratch_make(&a, &p, &sc);
            if (made == OK) {
                RegexSearched r = regex_search(&p, &sc, S("zbx9c"), 6);
                EXPECT(r.found);
                EXPECT_EQ(r.match.start, 1);
                EXPECT_EQ(r.match.end, 5);
                arena_deinit(&a);
                break;
            }
        }
        EXPECT(!refused.refused);
        EXPECT(a.hit);
        EXPECT(a.held <= limit);
        arena_deinit(&a);
    }
    size_t compile_least = limit;
    EXPECT(compile_least > 0);

    RegexCache c = regex_cache_make();
    RegexCached *other;
    RegexRefusal refused;
    EXPECT(regex_cache_put(&c, S("other"), opts(false), TEST_REGEX_LIMIT, &refused, &other) == OK);
    size_t held_before = regex_cache_held(&c);
    size_t refusals = 0;
    for (limit = 0;; limit++) {
        RegexCached *got = NULL;
        Status s = regex_cache_put(&c, pattern, opts(false), limit, &refused, &got);
        if (s == OK) {
            RegexSearched r = regex_search(&got->program, &got->scratch, S("zbx9c"), 6);
            EXPECT(r.found);
            EXPECT_EQ(cache_count(&c), 2);
            EXPECT(got->arena.held <= limit);
            break;
        }
        refusals++;
        EXPECT(!refused.refused);
        EXPECT(got == NULL);
        EXPECT_EQ(cache_count(&c), 1);
        EXPECT(cache_holds(&c, S("other")));
        EXPECT_EQ(regex_cache_held(&c), held_before);
    }
    // Every limit below the compile's need was refused, and the put's need is
    // the compile's and more: its scratch and the pattern's copy.
    EXPECT(refusals >= compile_least);
    EXPECT(regex_cache_get(&c, S("other"), opts(false)) != NULL);
    regex_cache_deinit(&c);
    EXPECT_EQ(regex_cache_held(&c), 0);
}

static RegexSearched searched(const RegexProgram *p, RegexScratch *sc, Str s, size_t from) {
    RegexMost most = {UINT64_MAX, UINT64_MAX};
    RegexSearched r = regex_search_within(p, sc, s, from, most);
    EXPECT(r.finished);
    return r;
}

TEST(a_search_that_skips_finds_what_one_examining_every_position_finds_from_every_start) {
    static const Str patterns[20] = {
        S("a"), S("ab|cd"), S("\\[([^\\]]*)\\]\\(([^)]*)\\)"), S("`([^`\n]+)`"), S("\\bstep(s)?\\b"),
        S("x+y"), S("(a)(b)?(c)"), S("[0-9]+"), S("\\Bb"), S("(?:ab)+c"),
        S("a$"), S("\\bcat\\b|dog"), S("(x|y)z*"), S(".b"), S("[^a]c"),
        S("(a*)*b"), S("\\n"), S("(a|ab)(c|bcd)"), S("\\$x"), S("b\\b"),
    };
    static const Str strings[18] = {
        S(""), S("a"), S("xyz"),
        S("see [the docs](a.md) and [x](y)"), S("`code` and `more` ``"), S("Steps: one step, two steps; misstep"),
        S("aab abc cd ab"), S("xxxy xy y x"), S("123 abc 45"),
        S("abb bab b"), S("ababab ababc abc"), S("a cat, a dog, concat"),
        S("a\nb\na"), S("[unclosed (paren"), S("abcbcd abcd"),
        S("$x $$x x"), S("ab.b cb\nb"), S("aaaaaaaaab aaa"),
    };
    Strs ps = {patterns, sizeof patterns / sizeof(Str)};
    Strs ss = {strings, sizeof strings / sizeof(Str)};
    EXPECT_EQ(ps.len, 20);
    EXPECT_EQ(ss.len, 18);
    bool skipped_somewhere = false;
    for (size_t pk = 0; pk < ps.len; pk++) {
        for (int nocase = 0; nocase < 2; nocase++) {
            Compiled c;
            compiled_make(&c, at(ps, pk), opts(nocase == 1));
            // The same program, with nothing to skip by.
            RegexProgram every = c.p;
            every.has_first = false;
            for (size_t sk = 0; sk < ss.len; sk++) {
                Str s = at(ss, sk);
                for (size_t from = 0; from <= s.len; from++) {
                    RegexSearched got = searched(&c.p, &c.sc, s, from);
                    RegexSearched want = searched(&every, &c.sc, s, from);
                    EXPECT_EQ(want.skipped, 0);
                    EXPECT_EQ(want.found, got.found);
                    if (got.found) {
                        EXPECT_EQ(want.match.start, got.match.start);
                        EXPECT_EQ(want.match.end, got.match.end);
                        for (size_t g = 1; g <= REGEX_GROUPS_MOST; g++) {
                            EXPECT_EQ(group_of(want, g).matched, group_of(got, g).matched);
                            EXPECT_EQ(group_of(want, g).start, group_of(got, g).start);
                            EXPECT_EQ(group_of(want, g).end, group_of(got, g).end);
                        }
                    }
                    // A skipped byte is a position not examined.
                    EXPECT(got.positions + got.skipped <= want.positions + 1);
                    if (got.skipped > 0) skipped_somewhere = true;
                }
            }
            arena_deinit(&c.arena);
        }
    }
    EXPECT(skipped_somewhere);
}

typedef struct {
    Str pattern;
    Str first;
    bool nocase;
} FirstCase;

SLICE_TYPE(FirstCases, const FirstCase);

TEST(a_pattern_that_can_match_nothing_or_begin_anywhere_skips_nothing_and_one_that_cannot_names_its_first_bytes) {
    static const Str none[10] = {S(""), S("a*"), S("^"), S("$"), S("\\b"), S("(a|)"), S("x?"), S("(a*)*"), S("a|b*"), S("[\\s\\S]")};
    Strs ns = {none, sizeof none / sizeof(Str)};
    for (size_t k = 0; k < ns.len; k++) {
        Compiled c;
        compiled_make(&c, at(ns, k), opts(false));
        EXPECT(!c.p.has_first);
        arena_deinit(&c.arena);
    }
    static const FirstCase cases[6] = {
        {S("ab|cd"), S("ac"), false},
        {S("\\bstep"), S("s"), false},
        {S("step"), S("sS"), true},
        {S("(?:x|y)+z"), S("xy"), false},
        {S("a?b"), S("ab"), false},
        {S("[0-2]x"), S("012"), false},
    };
    FirstCases fs = {cases, sizeof cases / sizeof(FirstCase)};
    for (size_t k = 0; k < fs.len; k++) {
        FirstCase f = at(fs, k);
        Compiled c;
        compiled_make(&c, f.pattern, opts(f.nocase));
        EXPECT(c.p.has_first);
        EXPECT_EQ(regex_set_count(&c.p.first), f.first.len);
        for (size_t b = 0; b < f.first.len; b++) EXPECT(regex_set_has(&c.p.first, str_byte(f.first, b)));
        arena_deinit(&c.arena);
    }
}

TEST(a_search_stops_unfinished_past_the_bytes_it_may_skip_as_past_the_positions_it_may_examine) {
    Compiled c;
    compiled_make(&c, S("z"), opts(false));
    Str s = S("aaaaaaaaaaz");
    RegexMost tight = {100, 9};
    RegexSearched shorter = regex_search_within(&c.p, &c.sc, s, 0, tight);
    EXPECT(!shorter.finished);
    EXPECT(!shorter.found);
    EXPECT_EQ(shorter.skipped, 10);
    RegexMost room = {100, 10};
    RegexSearched enough = regex_search_within(&c.p, &c.sc, s, 0, room);
    EXPECT(enough.finished);
    EXPECT(enough.found);
    EXPECT_EQ(enough.match.start, 10);
    EXPECT_EQ(enough.positions, 2);
    // Past the positions, unfinished the same way: a match of `z` takes two,
    // the byte and the end after it.
    RegexMost two = {2, UINT64_MAX};
    RegexSearched cut = regex_search_within(&c.p, &c.sc, S("az"), 0, two);
    EXPECT(cut.finished);
    EXPECT(cut.found);
    EXPECT_EQ(cut.skipped, 1);
    RegexMost one = {1, UINT64_MAX};
    RegexSearched cut_short = regex_search_within(&c.p, &c.sc, S("zz"), 1, one);
    EXPECT(!cut_short.found);
    EXPECT(!cut_short.finished);
    EXPECT_EQ(cut_short.positions, 1);
    arena_deinit(&c.arena);
}

// What the C holds beyond Zig Shale's regex tests.

TEST(every_refusal_of_the_reader_says_its_text_at_its_byte) {
    static const Refused cases[25] = {
        {S("(?=a)"), 0, S("`(?` is not in this subset but for `(?:...)`, a group that does not capture: no lookaround, no named groups")},
        {S("x(?"), 1, S("`(?` is not in this subset but for `(?:...)`, a group that does not capture: no lookaround, no named groups")},
        {S("[[:alpha:]]"), 1, S("`[:alpha:]` and its kind are not in this subset: write the bytes (`[a-zA-Z]`), or `\\[` for the byte")},
        {S("[a\\b]"), 2, S("`\\b` in a class is not in this subset: a word's edge is a place, not a byte; write it outside the `[...]`")},
        {S("a\\b*"), 3, S("`^`, `$`, `\\b` and `\\B` are places, not bytes: they cannot repeat")},
        {S("$+"), 1, S("`^`, `$`, `\\b` and `\\B` are places, not bytes: they cannot repeat")},
        {S("a(b"), 1, S("a `(` that is never closed")},
        {S("a)"), 1, S("a `)` with no `(` before it: `\\)` is the byte")},
        {S("a|*"), 2, S("a `*`, `+` or `?` with nothing before it to repeat")},
        {S("x[ab"), 1, S("a `[` that is never closed")},
        {S("[a\\"), 0, S("a `[` that is never closed")},
        {S("[a-\\"), 0, S("a `[` that is never closed")},
        {S("ab\\"), 2, S("a pattern that ends in `\\`: `\\\\` is the byte")},
        {S("[z-a]"), 1, S("a range whose end is before its start (`z-a`): write it `a-z`")},
        {S("[a-\\d]"), 3, S("a range's end must be one byte, not a class")},
        {S("a**"), 2, S("a repetition repeated (`a**`, `a+*`): one is enough")},
        {S("a+*"), 2, S("a repetition repeated (`a**`, `a+*`): one is enough")},
        {S("a?{2}"), 2, S("a repetition repeated (`a*{2}`): one is enough")},
        {S("a{2}{3}"), 4, S("a repetition repeated (`a{2}*`): one is enough")},
        {S("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)"), 27, S("a tenth capture group: nine is the most (`\\9` is the last), so write this one `(?:...)` if it need not capture")},
        {S("a\\q"), 1, S("an escape this subset does not have: `\\d \\w \\s` (and `\\D \\W \\S`), `\\b \\B`, `\\n \\t \\r`, or `\\` before a byte that is not a letter or digit")},
        {S("[\\q]"), 1, S("an escape this subset does not have: `\\d \\w \\s` (and `\\D \\W \\S`), `\\b \\B`, `\\n \\t \\r`, or `\\` before a byte that is not a letter or digit")},
        {S("(a)\\1"), 3, S("backreferences (`\\1`) are not in this subset")},
        {S("a*?"), 2, S("lazy repetition (`*?`, `+?`, `??`) is not in this subset: every repetition takes as much as it can")},
        {S("a??"), 2, S("lazy repetition (`*?`, `+?`, `??`) is not in this subset: every repetition takes as much as it can")},
    };
    RefusedCases all = {cases, sizeof cases / sizeof(Refused)};
    expect_refusals(all);
    // What the reader takes, beside what it refuses: a `]` first is a byte,
    // a `-` last is a byte, `\b` outside a class is a place.
    RegexSearched r = found(S("[]a]+"), S("x]a]"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 1);
    EXPECT_EQ(r.match.end, 4);
    r = found(S("[a-]+"), S("x-a-"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 4);
    r = found(S("\\bb"), S("ab b"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 3);
}

TEST(nocase_folds_bytes_and_classes_before_negation) {
    Compiled c;
    compiled_make(&c, S("[^a]x"), opts(true));
    RegexSearched r = regex_search(&c.p, &c.sc, S("AxaXbX"), 7);
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 4);
    arena_deinit(&c.arena);
    compiled_make(&c, S("Step\\w"), opts(true));
    r = regex_search(&c.p, &c.sc, S("a sTePs"), 8);
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 2);
    EXPECT_EQ(r.match.end, 7);
    arena_deinit(&c.arena);
}

// The cache as re_find uses it: eight entries, the least recently used
// evicted, keyed by text and nocase, and an evicted entry's memory given
// back.
TEST(the_cache_keeps_eight_evicts_the_least_recently_used_and_gives_back_what_it_evicts) {
    RegexCache c = regex_cache_make();
    char buf[8];
    Bytes room = {buf, sizeof buf};
    RegexRefusal refused;
    RegexCached *got;
    for (size_t k = 0; k < REGEX_CACHE_MOST + 1; k++) {
        at(room, 0) = 'p';
        at(room, 1) = (char)('a' + k);
        Str p = {buf, 2};
        EXPECT(regex_cache_get(&c, p, opts(false)) == NULL);
        // The entry keeps its own copy of the text, so the caller's buffer may
        // change.
        EXPECT(regex_cache_put(&c, p, opts(false), TEST_REGEX_LIMIT, &refused, &got) == OK);
        EXPECT(got->text.ptr != buf);
        EXPECT_STR(got->text, p);
        if (k == REGEX_CACHE_MOST - 1) {
            // `pa` used again, so `pb` is the least recently used.
            EXPECT(regex_cache_get(&c, S("pa"), opts(false)) != NULL);
        }
        if (k + 1 < REGEX_CACHE_MOST) EXPECT_EQ(cache_count(&c), k + 1);
    }
    EXPECT_EQ(cache_count(&c), REGEX_CACHE_MOST);
    EXPECT(cache_holds(&c, S("pa")));
    EXPECT(!cache_holds(&c, S("pb")));
    EXPECT(cache_holds(&c, S("pi")));
    // nocase is part of the key.
    EXPECT(regex_cache_get(&c, S("pa"), opts(true)) == NULL);
    EXPECT(regex_cache_get(&c, S("pa"), opts(false)) != NULL);
    // A refused pattern leaves the cache as it was.
    size_t held = regex_cache_held(&c);
    EXPECT(regex_cache_put(&c, S("a**"), opts(false), TEST_REGEX_LIMIT, &refused, &got) == STOP);
    EXPECT(refused.refused);
    EXPECT_EQ(regex_cache_held(&c), held);
    EXPECT_EQ(cache_count(&c), REGEX_CACHE_MOST);
    // A thousand patterns through the cache hold what eight of the largest do.
    size_t largest = 0;
    for (size_t k = 0; k < 1000; k++) {
        char digits[8];
        Bytes d = {digits, sizeof digits};
        at(d, 0) = 'q';
        at(d, 1) = (char)('0' + k / 100);
        at(d, 2) = (char)('0' + k / 10 % 10);
        at(d, 3) = (char)('0' + k % 10);
        Str p = {digits, 4};
        EXPECT(regex_cache_put(&c, p, opts(false), TEST_REGEX_LIMIT, &refused, &got) == OK);
        if (got->arena.held > largest) largest = got->arena.held;
        EXPECT(regex_cache_held(&c) <= REGEX_CACHE_MOST * largest);
    }
    EXPECT_EQ(cache_count(&c), REGEX_CACHE_MOST);
    regex_cache_deinit(&c);
    EXPECT_EQ(cache_count(&c), 0);
}

// Each group's capture is its own, numbered by its `(`; one the match did not
// pass through is unmatched. The answers are Zig Shale's re_find's.
TEST(each_group_captures_its_own_part) {
    RegexSearched r = found(S("\\[([^\\]]*)\\]\\(([^)]*)\\)"), S("(b## Head[link](to.md)"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 9);
    EXPECT_EQ(r.match.end, 22);
    EXPECT_EQ(group_of(r, 1).start, 10);
    EXPECT_EQ(group_of(r, 1).end, 14);
    EXPECT_EQ(group_of(r, 2).start, 16);
    EXPECT_EQ(group_of(r, 2).end, 21);
    EXPECT(!group_of(r, 3).matched);
    r = found(S("(a)(b)?(c)?"), S("abb bab b"));
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 2);
    EXPECT(group_of(r, 1).matched);
    EXPECT_EQ(group_of(r, 1).end, 1);
    EXPECT(group_of(r, 2).matched);
    EXPECT_EQ(group_of(r, 2).start, 1);
    EXPECT_EQ(group_of(r, 2).end, 2);
    EXPECT(!group_of(r, 3).matched);
    r = found(S("(w)(o)(r)(d)( )(W)(o)(r)(d)"), S("a word Word"));
    EXPECT(r.found);
    for (size_t g = 1; g <= REGEX_GROUPS_MOST; g++) {
        EXPECT(group_of(r, g).matched);
        EXPECT_EQ(group_of(r, g).start, 1 + g);
        EXPECT_EQ(group_of(r, g).end, 2 + g);
    }
}

// A loop whose turn can take nothing ends at a turn that took nothing, as a
// backtracker's does, so `(\w*(|.))*` takes the whole line, a byte at a time
// where `\w*` takes none. The answers are Zig Shale's re_find's.
TEST(an_empty_turn_ends_its_loop_as_a_backtrackers_does) {
    Compiled c;
    compiled_make(&c, S("(\\w*(|.))*"), opts(false));
    Str s = S("see [the docs](a.md) and [x](y)");
    RegexSearched r = regex_search(&c.p, &c.sc, s, s.len + 1);
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 0);
    EXPECT_EQ(r.match.end, 31);
    EXPECT(group_of(r, 1).matched);
    EXPECT_EQ(group_of(r, 1).start, 30);
    EXPECT_EQ(group_of(r, 1).end, 31);
    EXPECT(group_of(r, 2).matched);
    EXPECT_EQ(group_of(r, 2).start, 30);
    EXPECT_EQ(group_of(r, 2).end, 31);
    s = S("aab abc cd ab");
    r = regex_search(&c.p, &c.sc, s, s.len + 1);
    EXPECT(r.found);
    EXPECT_EQ(r.match.end, 13);
    EXPECT_EQ(group_of(r, 1).start, 11);
    EXPECT_EQ(group_of(r, 1).end, 13);
    EXPECT_EQ(group_of(r, 2).start, group_of(r, 2).end);
    s = S("123 abc 45");
    r = regex_search_from(&c.p, &c.sc, s, 4, s.len + 1);
    EXPECT(r.found);
    EXPECT_EQ(r.match.start, 4);
    EXPECT_EQ(r.match.end, 10);
    EXPECT_EQ(group_of(r, 1).start, 8);
    EXPECT_EQ(group_of(r, 1).end, 10);
    arena_deinit(&c.arena);
}

// The guarantee the DRAGONs in regex_add and regex_emit name: whatever the
// pattern, a search examines each position once and follows at most four
// times the program's states there, so its cost is bounded before it runs.
TEST(a_hostile_search_stays_linear) {
    static const Str patterns[5] = {S("(a*)*b"), S("(a|a)*b"), S("(?:a+a+)+b"), S("(a|aa|a?)*$x"), S("(?:(?:a*)*a*)*c")};
    Strs ps = {patterns, sizeof patterns / sizeof(Str)};
    enum { N = 20000 };
    static char text[N];
    memset(text, 'a', sizeof text);
    Str s = {text, N};
    for (size_t k = 0; k < ps.len; k++) {
        Compiled c;
        compiled_make(&c, at(ps, k), opts(false));
        RegexSearched r = regex_search(&c.p, &c.sc, s, UINT64_MAX);
        EXPECT(r.finished);
        EXPECT(!r.found);
        EXPECT(r.positions + r.skipped <= (uint64_t)N + 1);
        EXPECT(r.work <= r.positions * (4 * (uint64_t)c.p.states + 1));
        arena_deinit(&c.arena);
    }
}

TEST_MAIN(
    TEST_CASE(counted_repetition_is_the_part_written_out),
    TEST_CASE(a_bad_count_is_refused_at_its_brace),
    TEST_CASE(sixteen_empty_loops_compile_and_a_seventeenth_is_refused_and_four_nested_but_not_five),
    TEST_CASE(control_escapes_are_the_control_bytes_alone_and_in_a_class),
    TEST_CASE(a_class_holds_a_shorthand_an_escaped_byte_and_a_range_whose_end_is_escaped),
    TEST_CASE(a_pattern_of_more_than_256_bytes_is_refused_at_its_257th_byte_unread),
    TEST_CASE(compile_and_cache_put_leave_nothing_behind_when_the_arena_refuses),
    TEST_CASE(a_search_that_skips_finds_what_one_examining_every_position_finds_from_every_start),
    TEST_CASE(a_pattern_that_can_match_nothing_or_begin_anywhere_skips_nothing_and_one_that_cannot_names_its_first_bytes),
    TEST_CASE(a_search_stops_unfinished_past_the_bytes_it_may_skip_as_past_the_positions_it_may_examine),
    TEST_CASE(every_refusal_of_the_reader_says_its_text_at_its_byte),
    TEST_CASE(nocase_folds_bytes_and_classes_before_negation),
    TEST_CASE(the_cache_keeps_eight_evicts_the_least_recently_used_and_gives_back_what_it_evicts),
    TEST_CASE(each_group_captures_its_own_part),
    TEST_CASE(an_empty_turn_ends_its_loop_as_a_backtrackers_does),
    TEST_CASE(a_hostile_search_stays_linear)
)
