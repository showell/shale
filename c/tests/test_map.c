// map.h: put, get and remove by Str key; the order keys were first set kept
// through growth and removal; a refused put leaves the map as it was.

#include "test.h"

#include "buf.h"
#include "map.h"

MAP_DEFINE(IntMap, int64_t)

TEST(put_then_get_and_a_missing_key_is_missing) {
    bool refused = false;
    Arena a;
    arena_init(&a, 1 << 20, &refused);
    IntMap m = IntMap_make(&a);
    int64_t v = 0;
    EXPECT(!IntMap_get(&m, S("x"), &v));
    EXPECT(IntMap_ptr(&m, S("x")) == NULL);
    EXPECT(IntMap_put(&m, S("x"), 1) == OK);
    EXPECT(IntMap_put(&m, S("y"), 2) == OK);
    EXPECT(IntMap_get(&m, S("x"), &v));
    EXPECT_EQ_I(v, 1);
    EXPECT(IntMap_get(&m, S("y"), &v));
    EXPECT_EQ_I(v, 2);
    EXPECT(!IntMap_get(&m, S("z"), &v));
    EXPECT_EQ(IntMap_len(&m), 2);
    *IntMap_ptr(&m, S("y")) = 20;
    EXPECT(IntMap_get(&m, S("y"), &v));
    EXPECT_EQ_I(v, 20);
    arena_deinit(&a);
}

TEST(keys_are_bytes_and_their_length) {
    bool refused = false;
    Arena a;
    arena_init(&a, 1 << 20, &refused);
    IntMap m = IntMap_make(&a);
    EXPECT(IntMap_put(&m, S(""), 1) == OK);
    EXPECT(IntMap_put(&m, S("a"), 2) == OK);
    EXPECT(IntMap_put(&m, S("a\0"), 3) == OK);
    EXPECT(IntMap_put(&m, S("A"), 4) == OK);
    EXPECT_EQ(IntMap_len(&m), 4);
    int64_t v = 0;
    EXPECT(IntMap_get(&m, S(""), &v) && v == 1);
    EXPECT(IntMap_get(&m, S("a"), &v) && v == 2);
    EXPECT(IntMap_get(&m, S("a\0"), &v) && v == 3);
    EXPECT(IntMap_get(&m, S("A"), &v) && v == 4);
    arena_deinit(&a);
}

TEST(setting_a_key_again_keeps_its_place) {
    bool refused = false;
    Arena a;
    arena_init(&a, 1 << 20, &refused);
    IntMap m = IntMap_make(&a);
    EXPECT(IntMap_put(&m, S("b"), 1) == OK);
    EXPECT(IntMap_put(&m, S("a"), 2) == OK);
    EXPECT(IntMap_put(&m, S("b"), 3) == OK);
    EXPECT_EQ(IntMap_len(&m), 2);
    EXPECT_STR(IntMap_key_at(&m, 0), S("b"));
    EXPECT_EQ_I(IntMap_value_at(&m, 0), 3);
    EXPECT_STR(IntMap_key_at(&m, 1), S("a"));
    EXPECT_EQ_I(IntMap_value_at(&m, 1), 2);
    EXPECT_TRAPS((void)IntMap_key_at(&m, 2));
    EXPECT_TRAPS((void)IntMap_value_at(&m, 2));
    arena_deinit(&a);
}

// "k<i>" in the arena, so the key outlives the loop that made it.
static Str key_of(Arena *a, int64_t i) {
    Buf b = buf_make(a);
    if (buf_byte(&b, 'k') != OK || buf_i64(&b, i) != OK) abort();
    return buf_view(&b);
}

TEST(a_thousand_keys_keep_their_order_through_every_growth) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    IntMap m = IntMap_make(&a);
    for (int64_t i = 0; i < 1000; i++) {
        // Every other one backwards, so the order is not the keys' sorted
        // order.
        int64_t k = i % 2 == 0 ? i : 1000 - i;
        EXPECT(IntMap_put(&m, key_of(&a, k), k * 10) == OK);
    }
    EXPECT_EQ(IntMap_len(&m), 1000);
    EXPECT(m.core.index_cap * 3 >= m.core.len * 4);
    for (int64_t i = 0; i < 1000; i++) {
        int64_t k = i % 2 == 0 ? i : 1000 - i;
        EXPECT_STR(IntMap_key_at(&m, (size_t)i), key_of(&a, k));
        int64_t v = -1;
        EXPECT(IntMap_get(&m, key_of(&a, k), &v));
        EXPECT_EQ_I(v, k * 10);
    }
    EXPECT(!IntMap_get(&m, S("k1000x"), &(int64_t){0}));
    arena_deinit(&a);
}

TEST(a_removal_keeps_the_others_order_and_finds_them_after) {
    bool refused = false;
    Arena a;
    arena_init(&a, 1 << 20, &refused);
    IntMap m = IntMap_make(&a);
    Str names[] = {S_ITEM("e"), S_ITEM("d"), S_ITEM("c"), S_ITEM("b"), S_ITEM("a")};
    MapKeys all = {names, sizeof names / sizeof *names};
    for (size_t i = 0; i < all.len; i++) EXPECT(IntMap_put(&m, at(all, i), (int64_t)i) == OK);
    EXPECT(IntMap_remove(&m, S("c")));
    EXPECT(!IntMap_remove(&m, S("c")));
    EXPECT(!IntMap_remove(&m, S("zz")));
    EXPECT_EQ(IntMap_len(&m), 4);
    EXPECT_STR(IntMap_key_at(&m, 0), S("e"));
    EXPECT_STR(IntMap_key_at(&m, 1), S("d"));
    EXPECT_STR(IntMap_key_at(&m, 2), S("b"));
    EXPECT_STR(IntMap_key_at(&m, 3), S("a"));
    EXPECT_EQ_I(IntMap_value_at(&m, 2), 3);
    EXPECT_EQ_I(IntMap_value_at(&m, 3), 4);
    int64_t v = -1;
    EXPECT(IntMap_get(&m, S("a"), &v) && v == 4);
    EXPECT(IntMap_get(&m, S("e"), &v) && v == 0);
    EXPECT(!IntMap_get(&m, S("c"), &v));
    // Set again, a removed key goes last.
    EXPECT(IntMap_put(&m, S("c"), 9) == OK);
    EXPECT_STR(IntMap_key_at(&m, 4), S("c"));
    EXPECT(IntMap_remove(&m, S("a")));
    EXPECT(IntMap_remove(&m, S("e")));
    EXPECT_STR(IntMap_key_at(&m, 0), S("d"));
    EXPECT_STR(IntMap_key_at(&m, 2), S("c"));
    EXPECT(IntMap_get(&m, S("c"), &v) && v == 9);
    arena_deinit(&a);
}

TEST(removing_every_key_leaves_an_empty_map_that_takes_keys_again) {
    bool refused = false;
    Arena a;
    arena_init(&a, 1 << 20, &refused);
    IntMap m = IntMap_make(&a);
    for (int64_t i = 0; i < 50; i++) EXPECT(IntMap_put(&m, key_of(&a, i), i) == OK);
    for (int64_t i = 0; i < 50; i++) EXPECT(IntMap_remove(&m, key_of(&a, i)));
    EXPECT_EQ(IntMap_len(&m), 0);
    int64_t v = 0;
    EXPECT(!IntMap_get(&m, key_of(&a, 7), &v));
    EXPECT(IntMap_put(&m, S("again"), 1) == OK);
    EXPECT_STR(IntMap_key_at(&m, 0), S("again"));
    arena_deinit(&a);
}

TEST(a_refused_put_leaves_the_map_as_it_was) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16384, &refused);
    Arena names;
    arena_init(&names, 1 << 20, &refused);
    IntMap m = IntMap_make(&a);
    size_t n = 0;
    while (IntMap_put(&m, key_of(&names, (int64_t)n), (int64_t)n) == OK) n++;
    EXPECT(a.hit);
    EXPECT(n > 10);
    EXPECT_EQ(IntMap_len(&m), n);
    int64_t v = -1;
    EXPECT(!IntMap_get(&m, key_of(&names, (int64_t)n), &v));
    for (size_t i = 0; i < n; i++) {
        v = -1;
        EXPECT(IntMap_get(&m, key_of(&names, (int64_t)i), &v));
        EXPECT_EQ_I(v, (int64_t)i);
        EXPECT_STR(IntMap_key_at(&m, i), key_of(&names, (int64_t)i));
    }
    // With room again, the put that was refused goes through, and goes last.
    a.limit = 1 << 20;
    EXPECT(IntMap_put(&m, key_of(&names, (int64_t)n), (int64_t)n) == OK);
    EXPECT_STR(IntMap_key_at(&m, n), key_of(&names, (int64_t)n));
    arena_deinit(&a);
    arena_deinit(&names);
}

TEST_MAIN(
    TEST_CASE(put_then_get_and_a_missing_key_is_missing),
    TEST_CASE(keys_are_bytes_and_their_length),
    TEST_CASE(setting_a_key_again_keeps_its_place),
    TEST_CASE(a_thousand_keys_keep_their_order_through_every_growth),
    TEST_CASE(a_removal_keeps_the_others_order_and_finds_them_after),
    TEST_CASE(removing_every_key_leaves_an_empty_map_that_takes_keys_again),
    TEST_CASE(a_refused_put_leaves_the_map_as_it_was)
)
