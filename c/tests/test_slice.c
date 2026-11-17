// slice.h: `at` reads and writes in bounds, traps outside them in every
// build, and evaluates its index once; `str_sub` holds both ends; the Str
// helpers answer both ways.

#include "test.h"

#include "slice.h"

SLICE_TYPE(Ints, int);

TEST(at_reads_and_writes_inside_the_length) {
    int xs[3] = {10, 20, 30};
    Ints s = {xs, 3};
    EXPECT_EQ(at(s, 0), 10);
    EXPECT_EQ(at(s, 2), 30);
    at(s, 1) = 25;
    // Through a second slice of the same memory: the write reached it.
    Ints again = {xs, 3};
    EXPECT_EQ(at(again, 1), 25);
}

TEST(at_traps_one_past_the_end_and_far_past_it) {
    int xs[3] = {10, 20, 30};
    Ints s = {xs, 3};
    EXPECT_TRAPS((void)at(s, 3));
    EXPECT_TRAPS((void)at(s, 1000000));
    // A negative index is a huge one, and traps too.
    int minus_one = -1;
    EXPECT_TRAPS((void)at(s, minus_one));
}

TEST(at_traps_on_an_empty_slice) {
    Ints s = {NULL, 0};
    EXPECT_TRAPS((void)at(s, 0));
}

TEST(at_traps_on_a_slice_cut_shorter_than_its_memory) {
    int xs[3] = {10, 20, 30};
    Ints s = {xs, 2};
    EXPECT_EQ(at(s, 1), 20);
    EXPECT_TRAPS((void)at(s, 2));
}

TEST(at_evaluates_its_index_once) {
    int xs[3] = {10, 20, 30};
    Ints s = {xs, 3};
    size_t i = 0;
    EXPECT_EQ(at(s, i++), 10);
    EXPECT_EQ(i, 1);
}

TEST(a_bad_index_with_no_trap_ends_the_process) {
    int xs[1] = {1};
    Ints s = {xs, 1};
    EXPECT_DIES((void)at(s, 1));
}

TEST(at_unchecked_reads_what_at_reads) {
    int xs[3] = {10, 20, 30};
    Ints s = {xs, 3};
    for (size_t i = 0; i < s.len; i++) EXPECT_EQ(at_unchecked(s, i, "i < s.len"), at(s, i));
}

TEST(S_is_the_literal_without_its_nul) {
    EXPECT_EQ(S("").len, 0);
    EXPECT_EQ(S("abc").len, 3);
    EXPECT_EQ(S("a\0b").len, 3);
    EXPECT_EQ(str_byte(S("a\0b"), 1), 0);
}

TEST(str_sub_takes_a_part_and_traps_past_either_end) {
    Str s = S("hello");
    EXPECT_STR(str_sub(s, 1, 4), S("ell"));
    EXPECT_STR(str_sub(s, 0, 5), s);
    EXPECT_STR(str_sub(s, 5, 5), S(""));
    EXPECT_TRAPS((void)str_sub(s, 0, 6));
    EXPECT_TRAPS((void)str_sub(s, 4, 3));
    EXPECT_TRAPS((void)str_sub(s, 6, 6));
}

TEST(a_part_of_an_empty_slice_keeps_its_null_and_never_adds_to_it) {
    // `NULL + 0` is undefined in C, and an empty Str may hold NULL (an empty
    // file's text, as `shale --check` reads it). GCC's UBSan doesn't flag
    // it, so the C never computes it, and the part holds the NULL as it was.
    Str empty = {NULL, 0};
    Str part = str_sub(empty, 0, 0);
    EXPECT(part.ptr == NULL);
    EXPECT_EQ(part.len, 0);
    Ints none = {NULL, 0};
    Ints sub = SLICE_SUB(Ints, none, 0, 0);
    EXPECT(sub.ptr == NULL);
    EXPECT_EQ(sub.len, 0);
    // A part from 0 of a slice that holds memory keeps its pointer too.
    Str s = S("abc");
    EXPECT(str_sub(s, 0, 2).ptr == s.ptr);
    EXPECT_TRAPS((void)str_sub(empty, 0, 1));
}

TEST(str_sub_evaluates_each_argument_once) {
    // A macro would check an argument with a side effect as one value and
    // use it as another (a part from 3 of length SIZE_MAX).
    Str s = S("abc");
    size_t k = 2;
    Str part = str_sub(s, k++, 3);
    EXPECT_EQ(k, 3);
    EXPECT_STR(part, S("c"));
    size_t to = 3;
    EXPECT_STR(str_sub(s, 1, to--), S("bc"));
    EXPECT_EQ(to, 2);
    EXPECT_TRAPS((void)str_sub(s, k++, 2));
}

TEST(an_index_past_32_bits_traps_and_never_wraps_into_range) {
    // On a 32-bit size_t (the wasm build) 2^32 + 1 would have been cut to
    // 1 before its check; it is compared in 64 bits.
    int xs[3] = {10, 20, 30};
    Ints s = {xs, 3};
    int64_t past = ((int64_t)1 << 32) + 1;
    EXPECT_TRAPS((void)at(s, past));
    EXPECT_TRAPS((void)str_sub(S("abc"), 0, past));
    EXPECT_EQ(at(s, (int64_t)1), 20);
}

TEST(SLICE_SUB_keeps_the_type_and_its_bounds) {
    int xs[4] = {1, 2, 3, 4};
    Ints s = {xs, 4};
    Ints mid = SLICE_SUB(Ints, s, 1, 3);
    EXPECT_EQ(mid.len, 2);
    EXPECT_EQ(at(mid, 0), 2);
    EXPECT_TRAPS((void)at(mid, 2));
}

TEST(str_eq_and_starts_with_answer_both_ways) {
    EXPECT(str_eq(S("abc"), S("abc")));
    EXPECT(!str_eq(S("abc"), S("abd")));
    EXPECT(!str_eq(S("ab"), S("abc")));
    EXPECT(str_eq(S(""), (Str){NULL, 0}));
    EXPECT(str_starts_with(S("the prelude's f"), S("the prelude's ")));
    EXPECT(!str_starts_with(S("the prelude"), S("the prelude's ")));
    EXPECT(str_starts_with(S("x"), S("")));
}

TEST(str_byte_reads_bytes_unsigned) {
    EXPECT_EQ(str_byte(S("\xff"), 0), 255);
    EXPECT_TRAPS((void)str_byte(S("a"), 1));
}

TEST_MAIN(
    TEST_CASE(at_reads_and_writes_inside_the_length),
    TEST_CASE(at_traps_one_past_the_end_and_far_past_it),
    TEST_CASE(at_traps_on_an_empty_slice),
    TEST_CASE(at_traps_on_a_slice_cut_shorter_than_its_memory),
    TEST_CASE(at_evaluates_its_index_once),
    TEST_CASE(a_bad_index_with_no_trap_ends_the_process),
    TEST_CASE(at_unchecked_reads_what_at_reads),
    TEST_CASE(S_is_the_literal_without_its_nul),
    TEST_CASE(str_sub_takes_a_part_and_traps_past_either_end),
    TEST_CASE(a_part_of_an_empty_slice_keeps_its_null_and_never_adds_to_it),
    TEST_CASE(str_sub_evaluates_each_argument_once),
    TEST_CASE(an_index_past_32_bits_traps_and_never_wraps_into_range),
    TEST_CASE(SLICE_SUB_keeps_the_type_and_its_bounds),
    TEST_CASE(str_eq_and_starts_with_answer_both_ways),
    TEST_CASE(str_byte_reads_bytes_unsigned)
)
