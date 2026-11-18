// test_lex.c: the lexer against Zig Shale's lexer tests, and what each stop
// says and where.

#include "test.h"

#include "../script/lex.h"

typedef struct {
    Arena arena;
    Lexer lx;
} LexRun;

static void lex_start(LexRun *r, Str src) {
    arena_init(&r->arena, 1 << 20);
    lexer_init(&r->lx, &r->arena, src);
}

// The next token, which must lex.
static void next_ok(LexRun *r, Token *t) {
    memset(t, 0, sizeof *t);
    EXPECT(lexer_next(&r->lx, t) == OK);
}

static Tag next_tag(LexRun *r) {
    Token t;
    next_ok(r, &t);
    return t.tag;
}

static uint32_t next_line(LexRun *r) {
    Token t;
    next_ok(r, &t);
    return t.line;
}

// Lexes `src` to its end or its error; true when it ends in an error.
static bool lex_fails(LexRun *r, Str src) {
    lex_start(r, src);
    for (;;) {
        Token t;
        if (lexer_next(&r->lx, &t) != OK) return true;
        if (t.tag == TAG_EOF) return false;
    }
}

typedef struct {
    Str src;
    Str says;
} SaysCase;

SLICE_TYPE(SaysCases, const SaysCase);

typedef struct {
    Str src;
    uint32_t line;
    uint32_t column;
} PlaceCase;

SLICE_TYPE(PlaceCases, const PlaceCase);

static bool contains(Str s, Str part) {
    for (size_t i = 0; i + part.len <= s.len; i++) {
        if (str_eq(str_sub(s, i, i + part.len), part)) return true;
    }
    return false;
}

TEST(names_keywords_ints_strings_every_symbol_and_comments) {
    LexRun r;
    lex_start(&r, S("local x = 12 -- a comment\nif x ~= 3 then print(\"a\\tb\") end // % .. == <= >= < > # [ ] { } , . ;"));
    static const Tag want[] = {
        TAG_KW_LOCAL, TAG_NAME, TAG_ASSIGN, TAG_INT, TAG_KW_IF, TAG_NAME, TAG_NOT_EQ, TAG_INT, TAG_KW_THEN,
        TAG_NAME, TAG_LPAREN, TAG_STRING, TAG_RPAREN, TAG_KW_END, TAG_SLASH_SLASH, TAG_PERCENT, TAG_DOT_DOT,
        TAG_EQ_EQ, TAG_LE, TAG_GE, TAG_LT, TAG_GT, TAG_HASH, TAG_LBRACKET, TAG_RBRACKET, TAG_LBRACE,
        TAG_RBRACE, TAG_COMMA, TAG_DOT, TAG_SEMICOLON, TAG_EOF,
    };
    SLICE_TYPE(Tags, const Tag);
    Tags tags = {want, sizeof want / sizeof(Tag)};
    for (size_t i = 0; i < tags.len; i++) EXPECT_EQ(next_tag(&r), at(tags, i));
    arena_deinit(&r.arena);
}

TEST(a_string_undoes_its_escapes_and_the_largest_integer_is_one) {
    LexRun r;
    lex_start(&r, S("\"a\\n\\t\\\\\\\"b\" 'c' 9223372036854775807 \"\\x41\\0\""));
    Token esc;
    next_ok(&r, &esc);
    EXPECT_STR(esc.text, S("a\n\t\\\"b"));
    Token quoted;
    next_ok(&r, &quoted);
    EXPECT_STR(quoted.text, S("c"));
    Token big;
    next_ok(&r, &big);
    EXPECT(big.value == INT64_MAX);
    EXPECT(!big.min_magnitude);
    Token hex;
    next_ok(&r, &hex);
    EXPECT_EQ(hex.text.len, 2);
    EXPECT_EQ(str_byte(hex.text, 0), 'A');
    EXPECT_EQ(str_byte(hex.text, 1), 0);
    Token e;
    next_ok(&r, &e);
    EXPECT_EQ(e.tag, TAG_EOF);
    EXPECT_EQ(e.line, 1);
    arena_deinit(&r.arena);
}

TEST(the_smallest_integers_magnitude_is_marked_and_one_past_is_refused) {
    LexRun r;
    lex_start(&r, S("9223372036854775808"));
    Token t;
    next_ok(&r, &t);
    EXPECT(t.min_magnitude);
    EXPECT(t.value == INT64_MIN);
    arena_deinit(&r.arena);
    EXPECT(lex_fails(&r, S("x = 9223372036854775809")));
    EXPECT_STR(r.lx.err.message, S("an integer literal too big for 64 bits: they run from -9223372036854775808 to 9223372036854775807"));
    EXPECT(!r.lx.err.refused);
    arena_deinit(&r.arena);
    // A leading zero makes it no spelling of the smallest's magnitude.
    EXPECT(lex_fails(&r, S("09223372036854775808")));
    arena_deinit(&r.arena);
}

TEST(each_lua_construct_refused_by_name) {
    static const SaysCase cases[] = {
        {S("x = 1.5"), S("float")}, {S("x = .5"), S("float")}, {S("x = 1e3"), S("float")},
        {S("x = 0x10"), S("hexadecimal")}, {S("x = a / b"), S("`//`")}, {S("s:upper()"), S("method")},
        {S("::top::"), S("labels")}, {S("f(...)"), S("varargs")}, {S("x = [[long]]"), S("a long string needs a level")},
        {S("--[[ long\ncomment ]]"), S("a long comment needs a level")}, {S("goto done"), S("goto")},
        {S("repeat x = 1 until x"), S("repeat")}, {S("x = a & b"), S("bitwise")}, {S("x = a << 1"), S("bitwise")},
        {S("x = ~a"), S("bitwise")}, {S("x = 2 ^ 3"), S("exponent")}, {S("x = \"\\q\""), S("escape")},
        {S("x = \"\\1\""), S("decimal")}, {S("x = \"\\u{41}\""), S("UTF-8")}, {S("x = \"\\z\""), S("ends on its line")},
    };
    SaysCases all = {cases, sizeof cases / sizeof(SaysCase)};
    LexRun r;
    for (size_t i = 0; i < all.len; i++) {
        EXPECT(lex_fails(&r, at(all, i).src));
        EXPECT(r.lx.err.refused);
        EXPECT(contains(r.lx.err.message, at(all, i).says));
        arena_deinit(&r.arena);
    }
}

TEST(plain_errors_each_with_its_line_and_column) {
    static const PlaceCase cases[] = {
        {S("x = \"abc"), 1, 5}, {S("\n  x = \"a\nb\""), 2, 7}, {S("x = 99999999999999999999"), 1, 5},
        {S("x = 1 @"), 1, 7}, {S("x = 12ab"), 1, 5}, {S("s = \"\\xZZ\""), 1, 6},
    };
    PlaceCases all = {cases, sizeof cases / sizeof(PlaceCase)};
    LexRun r;
    for (size_t i = 0; i < all.len; i++) {
        EXPECT(lex_fails(&r, at(all, i).src));
        EXPECT(!r.lx.err.refused);
        EXPECT_EQ(r.lx.err.line, at(all, i).line);
        EXPECT_EQ(r.lx.err.column, at(all, i).column);
        arena_deinit(&r.arena);
    }
}

TEST(a_stray_byte_is_shown_quoted_when_it_prints_and_in_hex_when_not) {
    LexRun r;
    EXPECT(lex_fails(&r, S("x = 1 @")));
    EXPECT_STR(r.lx.err.message, S("a character no token starts with, `@`"));
    arena_deinit(&r.arena);
    EXPECT(lex_fails(&r, S("x = \xc3\xa9")));
    EXPECT_STR(r.lx.err.message, S("a character no token starts with, the byte \\xC3"));
    EXPECT_EQ(r.lx.err.column, 5);
    arena_deinit(&r.arena);
    EXPECT(lex_fails(&r, S("\x01")));
    EXPECT_STR(r.lx.err.message, S("a character no token starts with, the byte \\x01"));
    arena_deinit(&r.arena);
}

TEST(long_strings_take_their_level_drop_the_first_newline_and_keep_bytes_raw) {
    static const SaysCase cases[] = {
        {S("[=[a]]b]=]"), S("a]]b")}, {S("[==[x ]=] y]]]==]"), S("x ]=] y]]")}, {S("[===[]==]]===]"), S("]==]")},
        {S("[=[\nline one\nline two\n]=]"), S("line one\nline two\n")}, {S("[=[a\\n\"b\r\nc]=]"), S("a\\n\"b\r\nc")},
        {S("[=[]=]"), S("")}, {S("[=[\r\nx]=]"), S("x")},
    };
    SaysCases all = {cases, sizeof cases / sizeof(SaysCase)};
    LexRun r;
    for (size_t i = 0; i < all.len; i++) {
        lex_start(&r, at(all, i).src);
        Token t;
        next_ok(&r, &t);
        EXPECT_EQ(t.tag, TAG_STRING);
        EXPECT(t.is_long);
        EXPECT_STR(t.text, at(all, i).says);
        EXPECT_EQ(next_tag(&r), TAG_EOF);
        arena_deinit(&r.arena);
    }
    // What follows a long string of three lines is placed on its own line.
    lex_start(&r, S("x = [=[\none\ntwo]=] y\nz"));
    (void)next_tag(&r);
    (void)next_tag(&r);
    Token s;
    next_ok(&r, &s);
    EXPECT_EQ(s.line, 1);
    EXPECT_EQ(s.column, 5);
    EXPECT_EQ(s.long_string.level, 1);
    EXPECT_EQ(s.long_string.close_line, 3);
    EXPECT_EQ(s.long_string.close_column, 4);
    Token y;
    next_ok(&r, &y);
    EXPECT_EQ(y.line, 3);
    EXPECT_EQ(y.column, 8);
    EXPECT_EQ(next_line(&r), 4);
    arena_deinit(&r.arena);
}

TEST(a_long_string_never_closed_or_no_opener_stops_at_the_opener) {
    static const SaysCase cases[] = {
        {S("x = [=[abc]]"), S("a long string with no `]=]` to close it")},
        {S("x = [==[abc]=]"), S("a long string with no `]==]` to close it")},
        {S("x = [=a"), S("`[=` opens a long string, `[=[ ... ]=]`: a `[` must follow its `=`s")},
    };
    SaysCases all = {cases, sizeof cases / sizeof(SaysCase)};
    LexRun r;
    for (size_t i = 0; i < all.len; i++) {
        EXPECT(lex_fails(&r, at(all, i).src));
        EXPECT_STR(r.lx.err.message, at(all, i).says);
        EXPECT_EQ(r.lx.err.line, 1);
        EXPECT_EQ(r.lx.err.column, 5);
        arena_deinit(&r.arena);
    }
}

TEST(a_block_comment_is_passed_by_its_lines_after_placed_right) {
    LexRun r;
    lex_start(&r, S("x --[=[ a ]] b\n c ]=] y --[==[ ]=] ]==]\nz --[=x is a line comment\nw --[ too\nv"));
    static const PlaceCase want[] = {{S("x"), 1, 1}, {S("y"), 2, 8}, {S("z"), 3, 1}, {S("w"), 4, 1}, {S("v"), 5, 1}};
    PlaceCases all = {want, sizeof want / sizeof(PlaceCase)};
    for (size_t i = 0; i < all.len; i++) {
        Token t;
        next_ok(&r, &t);
        EXPECT_STR(t.text, at(all, i).src);
        EXPECT_EQ(t.line, at(all, i).line);
        EXPECT_EQ(t.column, at(all, i).column);
    }
    EXPECT_EQ(next_tag(&r), TAG_EOF);
    arena_deinit(&r.arena);
    EXPECT(lex_fails(&r, S("x = 1\n  --[=[ never closed ]]")));
    EXPECT_EQ(r.lx.err.line, 2);
    EXPECT_EQ(r.lx.err.column, 3);
    EXPECT_STR(r.lx.err.message, S("a long comment with no `]=]` to close it"));
    arena_deinit(&r.arena);
}

TEST(next_or_comment_gives_each_comment_in_place_and_every_token_holds_its_bytes) {
    Str src = S("a -- b\n--[=[ c\n d ]=] e --[=x\n  --\n--[==[ ]=] ]==]");
    LexRun r;
    lex_start(&r, src);
    static const PlaceCase want[] = {
        {S("a"), 1, 1}, {S("-- b"), 1, 3}, {S("--[=[ c\n d ]=]"), 2, 1}, {S("e"), 3, 8},
        {S("--[=x"), 3, 10}, {S("--"), 4, 3}, {S("--[==[ ]=] ]==]"), 5, 1}, {S(""), 5, 16},
    };
    PlaceCases all = {want, sizeof want / sizeof(PlaceCase)};
    for (size_t i = 0; i < all.len; i++) {
        Token t;
        EXPECT(lexer_next_or_comment(&r.lx, &t) == OK);
        EXPECT_STR(t.text, at(all, i).src);
        EXPECT_STR(str_sub(src, t.start, t.stop), at(all, i).src);
        EXPECT_EQ(t.line, at(all, i).line);
        EXPECT_EQ(t.column, at(all, i).column);
        EXPECT_EQ(t.tag == TAG_COMMENT, i == 1 || i == 2 || i == 4 || i == 5 || i == 6);
    }
    arena_deinit(&r.arena);
    // A string's span holds its quotes and its escapes as written.
    Str s2 = S("ab 12 ~= \"a\\n\" 'b' [=[\nc]=] x");
    lex_start(&r, s2);
    static const SaysCase spans[] = {{S("ab"), S("")}, {S("12"), S("")}, {S("~="), S("")}, {S("\"a\\n\""), S("")}, {S("'b'"), S("")}, {S("[=[\nc]=]"), S("")}, {S("x"), S("")}};
    SaysCases sp = {spans, sizeof spans / sizeof(SaysCase)};
    for (size_t i = 0; i < sp.len; i++) {
        Token t;
        next_ok(&r, &t);
        EXPECT_STR(str_sub(s2, t.start, t.stop), at(sp, i).src);
    }
    Token e;
    next_ok(&r, &e);
    EXPECT_EQ(e.start, s2.len);
    EXPECT_EQ(e.stop, s2.len);
    arena_deinit(&r.arena);
}

TEST(a_string_across_its_line_or_never_closed_stops_at_its_quote) {
    LexRun r;
    EXPECT(lex_fails(&r, S("x = 'a\nb'")));
    EXPECT_STR(r.lx.err.message, S("a string runs past the end of its line: write \\n"));
    EXPECT_EQ(r.lx.err.column, 5);
    arena_deinit(&r.arena);
    EXPECT(lex_fails(&r, S("x = \"a\\")));
    EXPECT_STR(r.lx.err.message, S("a string with no closing quote"));
    arena_deinit(&r.arena);
    // The arena's refusal is no lex error: the caller reads it as the arena
    // budget.
    Arena tiny;
    arena_init(&tiny, 8);
    Lexer lx;
    lexer_init(&lx, &tiny, S("\"a string longer than the arena allows\""));
    Token t;
    EXPECT(lexer_next(&lx, &t) == STOP);
    EXPECT(lx.oom);
    arena_deinit(&tiny);
}

TEST_MAIN(TEST_CASE(names_keywords_ints_strings_every_symbol_and_comments),
          TEST_CASE(a_string_undoes_its_escapes_and_the_largest_integer_is_one),
          TEST_CASE(the_smallest_integers_magnitude_is_marked_and_one_past_is_refused),
          TEST_CASE(each_lua_construct_refused_by_name),
          TEST_CASE(plain_errors_each_with_its_line_and_column),
          TEST_CASE(a_stray_byte_is_shown_quoted_when_it_prints_and_in_hex_when_not),
          TEST_CASE(long_strings_take_their_level_drop_the_first_newline_and_keep_bytes_raw),
          TEST_CASE(a_long_string_never_closed_or_no_opener_stops_at_the_opener),
          TEST_CASE(a_block_comment_is_passed_by_its_lines_after_placed_right),
          TEST_CASE(next_or_comment_gives_each_comment_in_place_and_every_token_holds_its_bytes),
          TEST_CASE(a_string_across_its_line_or_never_closed_stops_at_its_quote))
