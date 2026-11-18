// vec.h: push, pop and index; growth by vec.h's rule for each item size;
// items kept through every move; a refusal leaves it as it was.

#include "test.h"

#include "vec.h"

VEC_DEFINE(I64s, int64_t)

typedef struct {
    int64_t a;
    int64_t b;
} Pair;

VEC_DEFINE(Pairs, Pair)

TEST(push_pop_and_at_agree) {
    Arena a;
    arena_init(&a, 1 << 20);
    I64s v = I64s_make(&a);
    EXPECT(I64s_push(&v, 5) == OK);
    EXPECT(I64s_push(&v, -6) == OK);
    EXPECT_EQ(v.len, 2);
    EXPECT_EQ_I(at(v, 1), -6);
    at(v, 0) = 7;
    I64sItems items = I64s_items(&v);
    EXPECT_EQ(items.len, 2);
    EXPECT_EQ_I(at(items, 0), 7);
    EXPECT_EQ_I(I64s_pop(&v), -6);
    EXPECT_EQ_I(I64s_pop(&v), 7);
    EXPECT_EQ(v.len, 0);
    EXPECT_TRAPS((void)I64s_pop(&v));
    EXPECT_TRAPS((void)at(v, 0));
    arena_deinit(&a);
}

TEST(it_grows_by_half_again_plus_a_line_of_items) {
    Arena a;
    arena_init(&a, 1 << 20);
    I64s v = I64s_make(&a);
    EXPECT(I64s_push(&v, 1) == OK);
    // A need of 1 for 8-byte items: 1 + 0 + 64 / 8.
    EXPECT_EQ(v.cap, 9);
    for (int64_t i = 2; i <= 9; i++) EXPECT(I64s_push(&v, i) == OK);
    EXPECT_EQ(v.cap, 9);
    EXPECT(I64s_push(&v, 10) == OK);
    // A need of 10: 10 + 5 + 8.
    EXPECT_EQ(v.cap, 23);
    Pairs p = Pairs_make(&a);
    Pair one = {1, 2};
    EXPECT(Pairs_push(&p, one) == OK);
    // For 16-byte items: 1 + 0 + 4.
    EXPECT_EQ(p.cap, 5);
    arena_deinit(&a);
}

TEST(items_survive_every_move_between_two_growing_vecs) {
    Arena a;
    arena_init(&a, 16 * 1024 * 1024);
    I64s x = I64s_make(&a);
    I64s y = I64s_make(&a);
    for (int64_t i = 0; i < 5000; i++) {
        EXPECT(I64s_push(&x, i) == OK);
        EXPECT(I64s_push(&y, -i) == OK);
    }
    for (size_t i = 0; i < 5000; i++) {
        EXPECT_EQ_I(at(x, i), (int64_t)i);
        EXPECT_EQ_I(at(y, i), -(int64_t)i);
    }
    arena_deinit(&a);
}

TEST(items_keep_their_alignment) {
    Arena a;
    arena_init(&a, 1 << 20);
    EXPECT(arena_alloc(&a, 1, 1) != NULL);
    Pairs p = Pairs_make(&a);
    for (int64_t i = 0; i < 100; i++) {
        Pair x = {i, i * 2};
        EXPECT(Pairs_push(&p, x) == OK);
        EXPECT_EQ((uintptr_t)p.ptr % _Alignof(Pair), 0);
    }
    EXPECT_EQ_I(at(p, 99).b, 198);
    arena_deinit(&a);
}

TEST(a_refused_push_leaves_the_vec_as_it_was) {
    // A first chunk of 120 bytes: the first buffer fits, its growth does not.
    Arena a;
    arena_init(&a, 120);
    I64s v = I64s_make(&a);
    for (int64_t i = 0; i < 9; i++) EXPECT(I64s_push(&v, i) == OK);
    EXPECT(!a.hit);
    EXPECT(I64s_push(&v, 99) == STOP);
    EXPECT(a.hit);
    EXPECT_EQ(v.len, 9);
    EXPECT_EQ(v.cap, 9);
    EXPECT_EQ_I(at(v, 8), 8);
    EXPECT(I64s_reserve(&v, SIZE_MAX) == STOP);
    EXPECT(I64s_reserve(&v, SIZE_MAX / 4) == STOP);
    EXPECT_EQ(v.len, 9);
    arena_deinit(&a);
}

TEST(a_capacity_whose_size_wraps_is_refused_with_room_to_grow_in_place) {
    // A reserve whose capacity times 8 wraps to 64 bytes, with the vec last
    // in a roomy arena, would fit in place and say OK for 3 * 2^61 + 8 items.
    Arena a;
    arena_init(&a, 1 << 20);
    I64s v = I64s_make(&a);
    EXPECT(I64s_push(&v, 1) == OK);
    EXPECT_EQ(v.cap, 9);
    size_t held = a.held;
    EXPECT(I64s_reserve(&v, SIZE_MAX / 4) == STOP);
    EXPECT(a.hit);
    EXPECT_EQ(v.cap, 9);
    EXPECT_EQ(v.len, 1);
    EXPECT_EQ(a.held, held);
    EXPECT_EQ_I(at(v, 0), 1);
    EXPECT_TRAPS((void)at(v, 1));
    arena_deinit(&a);
}

TEST(a_big_vec_holds_its_live_buffer_not_every_one_it_outgrew) {
    Arena a;
    arena_init(&a, 16 * 1024 * 1024);
    I64s v = I64s_make(&a);
    // 160 KB of items: past ARENA_BIG, so the last buffers are bigs.
    for (int64_t i = 0; i < 20000; i++) EXPECT(I64s_push(&v, i) == OK);
    EXPECT(a.held < 2 * v.cap * sizeof(int64_t));
    EXPECT(a.bigs != NULL);
    EXPECT_EQ_I(at(v, 19999), 19999);
    arena_deinit(&a);
}

TEST_MAIN(
    TEST_CASE(push_pop_and_at_agree),
    TEST_CASE(it_grows_by_half_again_plus_a_line_of_items),
    TEST_CASE(items_survive_every_move_between_two_growing_vecs),
    TEST_CASE(items_keep_their_alignment),
    TEST_CASE(a_refused_push_leaves_the_vec_as_it_was),
    TEST_CASE(a_capacity_whose_size_wraps_is_refused_with_room_to_grow_in_place),
    TEST_CASE(a_big_vec_holds_its_live_buffer_not_every_one_it_outgrew)
)
