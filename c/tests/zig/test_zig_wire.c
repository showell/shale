// The zig-ast wire's tests: Zig Shale's own, with the same bytes, then the
// edges the C reads by hand: identifiers, Zig's `parseInt`,
// a token block's little-endian records.
#include "test.h"

#include "../../host/zig_wire.c"

TEST(a_lines_fields_escape_tabs_newlines_crs_and_backslashes_and_read_back_as_written) {
    Arena a = arena_make(1 << 20);
    Buf w = buf_make(&a);
    EXPECT(zig_wire_field(&w, S("s")) == OK);
    EXPECT(zig_wire_tab(&w) == OK);
    EXPECT(buf_u64(&w, 3) == OK);
    EXPECT(zig_wire_tab(&w) == OK);
    EXPECT(zig_wire_field(&w, S("a\tb\nc\r\\d")) == OK);
    EXPECT(zig_wire_end(&w) == OK);
    EXPECT(zig_wire_source(&w, S("x\ty\n")) == OK);
    EXPECT(zig_wire_field(&w, S("end")) == OK);
    EXPECT(zig_wire_end(&w) == OK);
    Str body = buf_view(&w);
    EXPECT_STR(body, S("s\t3\ta\\tb\\nc\\r\\\\d\nsrc\t4\nx\ty\n\nend\n"));
    ZigWireLines l = {body, 0};
    Str room[4];
    ZigWireFields into = {room, 4};
    size_t n = 0;
    EXPECT(zig_wire_next(&l, &a, into, &n) == ZIG_WIRE_LINE);
    EXPECT_EQ(n, 3);
    size_t k = 0;
    EXPECT(zig_wire_int(at(into, 1), &k));
    EXPECT_EQ(k, 3);
    EXPECT_STR(at(into, 2), S("a\tb\nc\r\\d"));
    EXPECT(zig_wire_next(&l, &a, into, &n) == ZIG_WIRE_LINE);
    EXPECT(zig_wire_int(at(into, 1), &k));
    Str got = S("");
    EXPECT(zig_wire_block(&l, k, &got));
    EXPECT_STR(got, S("x\ty\n"));
    EXPECT(zig_wire_next(&l, &a, into, &n) == ZIG_WIRE_LINE);
    EXPECT_EQ(n, 1);
    EXPECT_STR(at(into, 0), S("end"));
    EXPECT(zig_wire_next(&l, &a, into, &n) == ZIG_WIRE_DONE);
    // A block the body does not hold, whole and with its newline, is none.
    EXPECT(!zig_wire_block(&l, 1, &got));
    arena_deinit(&a);
}

TEST(a_line_of_more_fields_than_there_is_room_for_a_line_with_no_newline_and_a_bad_escape_are_refused) {
    Arena a = arena_make(1 << 20);
    Str room[2];
    ZigWireFields into = {room, 2};
    size_t n = 0;
    ZigWireLines three = {S("a\tb\tc\n"), 0};
    EXPECT(zig_wire_next(&three, &a, into, &n) == ZIG_WIRE_BAD_FRAME);
    ZigWireLines two = {S("a\tb\n"), 0};
    EXPECT(zig_wire_next(&two, &a, into, &n) == ZIG_WIRE_LINE);
    EXPECT_EQ(n, 2);
    ZigWireLines open = {S("a\tb"), 0};
    EXPECT(zig_wire_next(&open, &a, into, &n) == ZIG_WIRE_BAD_FRAME);
    ZigWireLines bad = {S("a\\x\n"), 0};
    EXPECT(zig_wire_next(&bad, &a, into, &n) == ZIG_WIRE_BAD_ESCAPE);
    ZigWireLines trailing = {S("a\\\n"), 0};
    EXPECT(zig_wire_next(&trailing, &a, into, &n) == ZIG_WIRE_BAD_ESCAPE);
    // An empty line is one empty field, as Zig's split gives it.
    ZigWireLines empty = {S("\n"), 0};
    EXPECT(zig_wire_next(&empty, &a, into, &n) == ZIG_WIRE_LINE);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(at(into, 0).len, 0);
    arena_deinit(&a);
}

TEST(an_identifier_is_a_letter_or_underscore_then_word_bytes_and_no_keyword) {
    EXPECT(zig_wire_valid_id(S("count")));
    EXPECT(zig_wire_valid_id(S("_x9")));
    EXPECT(zig_wire_valid_id(S("While")));
    EXPECT(zig_wire_valid_id(S("constant")));
    EXPECT(!zig_wire_valid_id(S("")));
    EXPECT(!zig_wire_valid_id(S("9x")));
    EXPECT(!zig_wire_valid_id(S("a.b")));
    EXPECT(!zig_wire_valid_id(S("a-b")));
    EXPECT(!zig_wire_valid_id(S("const")));
    EXPECT(!zig_wire_valid_id(S("while")));
    EXPECT(!zig_wire_valid_id(S("addrspace")));
    EXPECT(!zig_wire_valid_id(S("threadlocal")));
}

TEST(a_count_is_decimal_with_an_optional_plus_and_inner_underscores) {
    size_t n = 7;
    EXPECT(zig_wire_int(S("0"), &n));
    EXPECT_EQ(n, 0);
    EXPECT(zig_wire_int(S("+12"), &n));
    EXPECT_EQ(n, 12);
    EXPECT(zig_wire_int(S("1_000"), &n));
    EXPECT_EQ(n, 1000);
    EXPECT(zig_wire_int(S("18446744073709551615"), &n));
    EXPECT_EQ(n, UINT64_MAX);
    n = 7;
    EXPECT(!zig_wire_int(S("18446744073709551616"), &n));
    EXPECT(!zig_wire_int(S(""), &n));
    EXPECT(!zig_wire_int(S("+"), &n));
    EXPECT(!zig_wire_int(S("-1"), &n));
    EXPECT(!zig_wire_int(S("_1"), &n));
    EXPECT(!zig_wire_int(S("1_"), &n));
    EXPECT(!zig_wire_int(S("1x"), &n));
    EXPECT(!zig_wire_int(S(" 1"), &n));
    EXPECT_EQ(n, 7);
}

TEST(a_token_record_is_its_tag_start_end_and_line_little_endian) {
    static const char rec[] = "\x05\x01\x00\x00\x00\x00\x01\x00\x00\x2a\x00\x00\x01" "\xff\xff\xff\xff\xff\x00\x00\x00\x80\x07\x00\x00\x00";
    Str block = {rec, 2 * ZIG_WIRE_TOKEN_BYTES};
    ZigWireToken t;
    zig_wire_token(block, 0, &t);
    EXPECT_EQ(t.tag, 5);
    EXPECT_EQ(t.start, 1);
    EXPECT_EQ(t.end, 256);
    EXPECT_EQ(t.line, 0x0100002a);
    zig_wire_token(block, 1, &t);
    EXPECT_EQ(t.tag, 255);
    EXPECT_EQ(t.start, UINT32_MAX);
    EXPECT_EQ(t.end, 0x80000000u);
    EXPECT_EQ(t.line, 7);
    // A record past the block is a bug in the caller, which `at` ends.
    EXPECT_DIES(zig_wire_token(block, 2, &t));
}

TEST_MAIN(TEST_CASE(a_lines_fields_escape_tabs_newlines_crs_and_backslashes_and_read_back_as_written),
          TEST_CASE(a_line_of_more_fields_than_there_is_room_for_a_line_with_no_newline_and_a_bad_escape_are_refused),
          TEST_CASE(an_identifier_is_a_letter_or_underscore_then_word_bytes_and_no_keyword),
          TEST_CASE(a_count_is_decimal_with_an_optional_plus_and_inner_underscores),
          TEST_CASE(a_token_record_is_its_tag_start_end_and_line_little_endian))
