// buf.h: appends of bytes, strings and numbers; growth by vec.h's rule, in
// place when it can and by a copy when not; and a refused append
// leaves the buffer as it was.

#include "test.h"

#include "buf.h"

TEST(appends_bytes_and_strings_in_order) {
    Arena a;
    arena_init(&a, 1 << 20);
    Buf b = buf_make(&a);
    EXPECT_STR(buf_view(&b), S(""));
    EXPECT(buf_byte(&b, 'a') == OK);
    EXPECT(buf_str(&b, S("bc")) == OK);
    EXPECT(buf_lit(&b, "") == OK);
    EXPECT(buf_lit(&b, "def") == OK);
    EXPECT_STR(buf_view(&b), S("abcdef"));
    buf_clear(&b);
    EXPECT_STR(buf_view(&b), S(""));
    EXPECT(buf_lit(&b, "x") == OK);
    EXPECT_STR(buf_view(&b), S("x"));
    arena_deinit(&a);
}

TEST(numbers_print_in_decimal_the_extremes_among_them) {
    Arena a;
    arena_init(&a, 1 << 20);
    Buf b = buf_make(&a);
    EXPECT(buf_u64(&b, 0) == OK);
    EXPECT(buf_byte(&b, ' ') == OK);
    EXPECT(buf_u64(&b, 42) == OK);
    EXPECT(buf_byte(&b, ' ') == OK);
    EXPECT(buf_u64(&b, UINT64_MAX) == OK);
    EXPECT(buf_byte(&b, ' ') == OK);
    EXPECT(buf_i64(&b, -1) == OK);
    EXPECT(buf_byte(&b, ' ') == OK);
    EXPECT(buf_i64(&b, INT64_MIN) == OK);
    EXPECT(buf_byte(&b, ' ') == OK);
    EXPECT(buf_i64(&b, INT64_MAX) == OK);
    EXPECT_STR(buf_view(&b), S("0 42 18446744073709551615 -1 -9223372036854775808 9223372036854775807"));
    arena_deinit(&a);
}

TEST(it_grows_by_half_again_in_place_while_it_is_last) {
    Arena a;
    arena_init(&a, 1 << 20);
    Buf b = buf_make(&a);
    EXPECT(buf_byte(&b, 'x') == OK);
    // A need of 1 for bytes: 1 + 0 + 64.
    EXPECT_EQ(b.cap, 65);
    char *first = b.ptr;
    for (int i = 1; i < 65; i++) EXPECT(buf_byte(&b, 'x') == OK);
    EXPECT_EQ(b.cap, 65);
    EXPECT(buf_byte(&b, 'y') == OK);
    // A need of 66: 66 + 33 + 64.
    EXPECT_EQ(b.cap, 163);
    EXPECT(b.ptr == first);
    EXPECT_EQ(b.len, 66);
    EXPECT_EQ(a.used, 163);
    arena_deinit(&a);
}

TEST(it_moves_and_keeps_its_bytes_when_it_is_not_last) {
    Arena a;
    arena_init(&a, 1 << 20);
    Buf b = buf_make(&a);
    for (int i = 0; i < 65; i++) EXPECT(buf_byte(&b, (char)('a' + i % 26)) == OK);
    char *first = b.ptr;
    EXPECT(arena_alloc(&a, 1, 1) != NULL);
    EXPECT(buf_byte(&b, '!') == OK);
    EXPECT(b.ptr != first);
    EXPECT_EQ(b.cap, 163);
    Str v = buf_view(&b);
    EXPECT_EQ(v.len, 66);
    for (size_t i = 0; i < 65; i++) EXPECT_EQ(str_byte(v, i), 'a' + i % 26);
    EXPECT_EQ(str_byte(v, 65), '!');
    arena_deinit(&a);
}

TEST(a_refused_append_leaves_the_buffer_as_it_was) {
    Arena a;
    arena_init(&a, 100);
    Buf b = buf_make(&a);
    EXPECT(buf_lit(&b, "abc") == OK);
    EXPECT(!a.hit);
    char hundred[100];
    memset(hundred, 'z', sizeof hundred);
    EXPECT(buf_str(&b, (Str){hundred, sizeof hundred}) == STOP);
    EXPECT(a.hit);
    EXPECT_STR(buf_view(&b), S("abc"));
    EXPECT_EQ(b.cap, 68);
    EXPECT(buf_reserve(&b, SIZE_MAX) == STOP);
    EXPECT_STR(buf_view(&b), S("abc"));
    arena_deinit(&a);
}

TEST(appending_a_view_of_itself_past_ARENA_BIG_copies_what_it_held) {
    // `s .. s` is buf_str(b, buf_view(b)). The growth frees the big buffer
    // the view points into, so a copy from the view after it would read
    // freed memory.
    Arena a;
    arena_init(&a, 16 * 1024 * 1024);
    Buf b = buf_make(&a);
    for (int i = 0; i < 100000; i++) EXPECT(buf_byte(&b, (char)('a' + i % 26)) == OK);
    EXPECT(b.cap < 200000);
    char *before = b.ptr;
    EXPECT(buf_str(&b, buf_view(&b)) == OK);
    EXPECT(b.ptr != before);
    EXPECT_EQ(b.len, 200000);
    Str v = buf_view(&b);
    for (size_t i = 0; i < 200000; i++) EXPECT_EQ(str_byte(v, i), 'a' + (i % 100000) % 26);
    // A view of only a part of itself, from the middle, copies that part.
    buf_clear(&b);
    EXPECT(buf_lit(&b, "0123456789") == OK);
    EXPECT(buf_str(&b, str_sub(buf_view(&b), 3, 6)) == OK);
    EXPECT_STR(buf_view(&b), S("0123456789345"));
    arena_deinit(&a);
}

TEST(a_big_buffer_holds_its_live_bytes_not_every_one_it_outgrew) {
    Arena a;
    arena_init(&a, 16 * 1024 * 1024);
    Buf b = buf_make(&a);
    for (int i = 0; i < 1000000; i++) EXPECT(buf_byte(&b, 'q') == OK);
    EXPECT_EQ(b.len, 1000000);
    EXPECT(a.held < 2 * b.cap);
    arena_deinit(&a);
}

TEST(a_fit_gives_back_the_room_past_its_length_only_when_it_is_last) {
    Arena a;
    arena_init(&a, 1 << 20);
    Buf b = buf_make(&a);
    EXPECT(buf_lit(&b, "/tmp/x/") == OK);
    EXPECT(buf_lit(&b, "f1") == OK);
    EXPECT_EQ(b.cap, 7 + 3 + 64);
    size_t end = a.end;
    buf_fit(&b);
    EXPECT_EQ(b.cap, 9);
    EXPECT_EQ(a.end, end - (7 + 3 + 64 - 9));
    EXPECT_STR(buf_view(&b), S("/tmp/x/f1"));
    // The next allocation starts where the string ends.
    char *next = arena_alloc(&a, 1, 1);
    EXPECT(next == b.ptr + 9);
    // A buffer that is not the last keeps its room, and its arena is left
    // as it was.
    Buf c = buf_make(&a);
    EXPECT(buf_lit(&c, "abc") == OK);
    EXPECT(arena_alloc(&a, 1, 1) != NULL);
    end = a.end;
    buf_fit(&c);
    EXPECT_EQ(c.cap, 3 + 1 + 64);
    EXPECT_EQ(a.end, end);
    EXPECT_STR(buf_view(&c), S("abc"));
    // Nor does an empty one change.
    Buf e = buf_make(&a);
    buf_fit(&e);
    EXPECT(e.ptr == NULL);
    EXPECT_EQ(e.cap, 0);
    arena_deinit(&a);
}

TEST_MAIN(
    TEST_CASE(appends_bytes_and_strings_in_order),
    TEST_CASE(numbers_print_in_decimal_the_extremes_among_them),
    TEST_CASE(it_grows_by_half_again_in_place_while_it_is_last),
    TEST_CASE(it_moves_and_keeps_its_bytes_when_it_is_not_last),
    TEST_CASE(a_refused_append_leaves_the_buffer_as_it_was),
    TEST_CASE(appending_a_view_of_itself_past_ARENA_BIG_copies_what_it_held),
    TEST_CASE(a_big_buffer_holds_its_live_bytes_not_every_one_it_outgrew),
    TEST_CASE(a_fit_gives_back_the_room_past_its_length_only_when_it_is_last)
)
