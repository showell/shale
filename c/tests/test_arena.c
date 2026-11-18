// arena.h: the budget holds, chunks start at the least and double, big
// allocations come and go on their own, and only the last allocation moves
// in place.

#include "test.h"

#include "arena.h"

TEST(the_budget_refuses_past_its_limit_and_says_it_was_hit) {
    bool refused = false;
    Arena a;
    arena_init(&a, 64 * 1024, &refused);
    EXPECT(arena_alloc(&a, 40 * 1024, 1) != NULL);
    EXPECT(!a.hit);
    EXPECT(!refused);
    EXPECT(arena_alloc(&a, 40 * 1024, 1) == NULL);
    EXPECT(a.hit);
    EXPECT(refused);
    EXPECT(a.held <= a.limit);
    arena_deinit(&a);
}

TEST(a_refusal_sets_the_arenas_own_flag_and_no_other) {
    bool mine = false;
    bool other = false;
    Arena a;
    arena_init(&a, 0, &mine);
    Arena b;
    arena_init(&b, 1 << 20, &other);
    EXPECT(arena_alloc(&a, 1, 1) == NULL);
    EXPECT(mine);
    EXPECT(!other);
    EXPECT(!b.hit);
    // A deinit keeps the arena's limit and its refusal.
    arena_deinit(&a);
    EXPECT(a.refused == &mine);
    EXPECT_EQ(a.limit, 0);
    EXPECT(!a.hit);
    mine = false;
    EXPECT(arena_alloc(&a, 1, 1) == NULL);
    EXPECT(mine);
    EXPECT(!other);
    arena_deinit(&b);
    // An arena with no refusal to set is a misuse.
    Arena none;
    EXPECT_DIES(arena_init(&none, 1, NULL));
}

TEST(chunks_start_at_the_least_and_double) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    EXPECT(arena_alloc(&a, 16000, 1) != NULL);
    EXPECT_EQ(a.held, 16384);
    EXPECT(arena_alloc(&a, 1000, 1) != NULL);
    EXPECT_EQ(a.held, 16384 + 32768);
    EXPECT(arena_alloc(&a, 40000, 1) != NULL);
    EXPECT_EQ(a.held, 16384 + 32768 + 65536);
    EXPECT_EQ(a.used, 16000 + 1000 + 40000);
    EXPECT(a.bigs == NULL);
    arena_deinit(&a);
}

TEST(a_chunk_near_the_limit_takes_only_what_is_left) {
    bool refused = false;
    Arena a;
    arena_init(&a, 20000, &refused);
    EXPECT(arena_alloc(&a, 16000, 1) != NULL);
    EXPECT_EQ(a.held, 16384);
    // Twice the last would be 32768; 3616 is what the limit has left.
    EXPECT(arena_alloc(&a, 1000, 1) != NULL);
    EXPECT_EQ(a.held, 20000);
    EXPECT(!a.hit);
    EXPECT(arena_alloc(&a, 3000, 1) == NULL);
    EXPECT(a.hit);
    EXPECT_EQ(a.held, 20000);
    // What still fits the last chunk is still given.
    EXPECT(arena_alloc(&a, 2600, 1) != NULL);
    EXPECT_EQ(a.held, 20000);
    arena_deinit(&a);
}

TEST(a_chunk_too_small_for_one_allocation_is_made_its_size) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    EXPECT(arena_alloc(&a, 30000, 8) != NULL);
    EXPECT_EQ(a.held, 30000 + 16 + 8);
    arena_deinit(&a);
}

TEST(a_big_allocation_has_its_own_chunk_and_gives_it_back) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    char *p = arena_alloc(&a, ARENA_BIG, 8);
    EXPECT(p != NULL);
    EXPECT(a.chunk == NULL);
    EXPECT(a.bigs != NULL);
    EXPECT_EQ(a.held, 16 + ARENA_BIG + 8);
    EXPECT_EQ((uintptr_t)p % 8, 0);
    memset(p, 1, ARENA_BIG);
    arena_free(&a, p, ARENA_BIG);
    EXPECT_EQ(a.held, 0);
    EXPECT(a.bigs == NULL);
    // One byte under is small, bumped out of an ordinary chunk.
    EXPECT(arena_alloc(&a, ARENA_BIG - 1, 1) != NULL);
    EXPECT(a.chunk != NULL);
    EXPECT(a.bigs == NULL);
    arena_deinit(&a);
}

TEST(big_allocations_are_given_back_in_any_order) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    char *x = arena_alloc(&a, 100000, 1);
    char *y = arena_alloc(&a, 200000, 1);
    char *z = arena_alloc(&a, 300000, 1);
    EXPECT(x != NULL && y != NULL && z != NULL);
    arena_free(&a, y, 200000);
    EXPECT_EQ(a.held, (16 + 100000 + 1) + (16 + 300000 + 1));
    arena_free(&a, x, 100000);
    EXPECT_EQ(a.held, 16 + 300000 + 1);
    arena_free(&a, z, 300000);
    EXPECT_EQ(a.held, 0);
    arena_deinit(&a);
}

TEST(allocations_are_aligned_as_asked) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    char *p = arena_alloc(&a, 1, 1);
    char *q = arena_alloc(&a, 8, 8);
    EXPECT_EQ((uintptr_t)q % 8, 0);
    EXPECT_EQ_I(q - p, 8);
    EXPECT(arena_alloc(&a, 1, 1) != NULL);
    char *r = arena_alloc(&a, 16, 64);
    EXPECT_EQ((uintptr_t)r % 64, 0);
    arena_deinit(&a);
}

TEST(only_the_last_allocation_grows_in_place) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    char *p = arena_alloc(&a, 10, 1);
    EXPECT(arena_resize(&a, p, 10, 20));
    EXPECT_EQ(a.used, 20);
    char *q = arena_alloc(&a, 10, 1);
    EXPECT_EQ_I(q - p, 20);
    EXPECT(!arena_resize(&a, p, 20, 30));
    // Any allocation may shrink, in place.
    EXPECT(arena_resize(&a, p, 20, 5));
    EXPECT(arena_resize(&a, q, 10, 30));
    // Past the chunk, or to big, it never grows in place.
    EXPECT(!arena_resize(&a, q, 30, 20000));
    EXPECT(!arena_resize(&a, q, 30, ARENA_BIG));
    arena_deinit(&a);
}

TEST(freeing_the_last_allocation_unbumps_it_and_any_other_is_kept) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    char *p = arena_alloc(&a, 100, 1);
    arena_free(&a, p, 100);
    EXPECT(arena_alloc(&a, 100, 1) == p);
    char *q = arena_alloc(&a, 100, 1);
    arena_free(&a, p, 100);
    EXPECT(arena_alloc(&a, 1, 1) == q + 100);
    arena_deinit(&a);
}

TEST(a_refusal_by_the_system_is_a_hit_and_holds_nothing) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    a.backing_faults = true;
    a.backing_ok_left = 1;
    EXPECT(arena_alloc(&a, 10, 1) != NULL);
    EXPECT_EQ(a.held, 16384);
    EXPECT(!a.hit);
    EXPECT(arena_alloc(&a, 20000, 1) == NULL);
    EXPECT(a.hit);
    EXPECT_EQ(a.held, 16384);
    EXPECT(arena_alloc(&a, 100000, 1) == NULL);
    EXPECT_EQ(a.held, 16384);
    // What fits the chunk it has needs nothing of the system.
    EXPECT(arena_alloc(&a, 10, 1) != NULL);
    arena_deinit(&a);
}

TEST(a_zero_length_allocation_holds_nothing_and_is_not_null) {
    bool refused = false;
    Arena a;
    arena_init(&a, 0, &refused);
    EXPECT(arena_alloc(&a, 0, 1) != NULL);
    EXPECT_EQ(a.held, 0);
    EXPECT(!a.hit);
    EXPECT(arena_alloc(&a, 1, 1) == NULL);
    EXPECT(a.hit);
    arena_deinit(&a);
}

TEST(an_array_whose_size_overflows_is_refused) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    EXPECT(ARENA_ARRAY(&a, uint64_t, SIZE_MAX / 4) == NULL);
    EXPECT(a.hit);
    EXPECT_EQ(a.held, 0);
    uint64_t *xs = ARENA_ARRAY(&a, uint64_t, 4);
    EXPECT(xs != NULL);
    EXPECT_EQ((uintptr_t)xs % _Alignof(uint64_t), 0);
    arena_deinit(&a);
}

TEST(a_size_that_wraps_with_its_header_is_refused) {
    // A length whose chunk, 16 + len + align, wraps to a few bytes would get
    // a tiny chunk that claims to hold it.
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    EXPECT(arena_alloc(&a, SIZE_MAX, 1) == NULL);
    EXPECT(a.hit);
    EXPECT_EQ(a.held, 0);
    a.hit = false;
    // 16 + len + 1 is exactly 2^64, which wraps to 0.
    EXPECT(arena_alloc(&a, SIZE_MAX - 16, 1) == NULL);
    EXPECT(a.hit);
    a.hit = false;
    // The same with an alignment's room: 16 + len + 4096 is 2^64.
    EXPECT(arena_alloc(&a, SIZE_MAX - 16 - 4095, 4096) == NULL);
    EXPECT(a.hit);
    a.hit = false;
    // One under the wrap is refused by the limit, not let through.
    EXPECT(arena_alloc(&a, SIZE_MAX - 17, 1) == NULL);
    EXPECT(a.hit);
    EXPECT_EQ(a.held, 0);
    EXPECT(a.chunk == NULL);
    EXPECT(a.bigs == NULL);
    arena_deinit(&a);
}

TEST(an_array_whose_size_wraps_to_a_small_one_is_refused) {
    // `n * sizeof(T)` for 2^61 items wraps to 0 (the empty sentinel, which is
    // not NULL) or to 16 bytes.
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    size_t to_zero = SIZE_MAX / 8 + 1;
    size_t to_sixteen = SIZE_MAX / 8 + 3;
    EXPECT(ARENA_ARRAY(&a, uint64_t, to_zero) == NULL);
    EXPECT(a.hit);
    a.hit = false;
    EXPECT(ARENA_ARRAY(&a, uint64_t, to_sixteen) == NULL);
    EXPECT(a.hit);
    EXPECT_EQ(a.held, 0);
    EXPECT_EQ(a.used, 0);
    arena_deinit(&a);
}

TEST(the_arena_never_holds_more_than_its_limit_whatever_grows_in_it) {
    // Small and big allocations interleaved, some bigs freed: Zig's test held
    // std's arena to 1.7x and 2.0x its count; this one holds to its limit.
    const size_t limit = 1024 * 1024;
    bool arena_refused = false;
    Arena a;
    arena_init(&a, limit, &arena_refused);
    uint64_t seed = 7;
    size_t refused = 0;
    char *last_big = NULL;
    size_t last_len = 0;
    while (refused < 50) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        size_t len = (size_t)(seed >> 33) % 90000 + 1;
        char *p = arena_alloc(&a, len, 8);
        if (p == NULL) {
            refused++;
        } else if (len >= ARENA_BIG) {
            if (last_big != NULL) arena_free(&a, last_big, last_len);
            last_big = p;
            last_len = len;
        }
        EXPECT(a.held <= limit);
    }
    EXPECT(a.hit);
    arena_deinit(&a);
}

TEST(deinit_gives_back_every_chunk_and_keeps_the_limit) {
    bool refused = false;
    Arena a;
    arena_init(&a, 1000000, &refused);
    EXPECT(arena_alloc(&a, 10, 1) != NULL);
    EXPECT(arena_alloc(&a, 100000, 1) != NULL);
    arena_deinit(&a);
    EXPECT_EQ(a.held, 0);
    EXPECT(a.chunk == NULL);
    EXPECT(a.bigs == NULL);
    EXPECT_EQ(a.limit, 1000000);
}

TEST(dup_copies_into_the_arena_and_is_refused_when_full) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16384, &refused);
    Str out;
    EXPECT(arena_dup(&a, S("hello"), &out));
    EXPECT_STR(out, S("hello"));
    Str big = {"", 0};
    EXPECT(arena_dup(&a, big, &out));
    EXPECT_EQ(out.len, 0);
    char filler[20000];
    memset(filler, 'x', sizeof filler);
    EXPECT(!arena_dup(&a, (Str){filler, sizeof filler}, &out));
    EXPECT(a.hit);
    arena_deinit(&a);
}

TEST(a_c_string_ends_with_a_nul_and_one_holding_a_nul_is_refused) {
    bool refused = false;
    Arena a;
    arena_init(&a, 16 * 1024 * 1024, &refused);
    char *ab = arena_cstr(&a, S("ab"));
    EXPECT(ab != NULL);
    EXPECT_STR(((Str){ab, strlen(ab)}), S("ab"));
    char *empty = arena_cstr(&a, S(""));
    EXPECT(empty != NULL);
    EXPECT_EQ(strlen(empty), 0);
    // A NUL inside is refused with no arena refusal, and nothing is kept:
    // the next string starts where the refused one would have.
    size_t held = a.held;
    EXPECT(arena_cstr(&a, ((Str){"a\0b", 3})) == NULL);
    EXPECT(!a.hit);
    EXPECT(!refused);
    EXPECT_EQ(a.held, held);
    char *cd = arena_cstr(&a, S("cd"));
    EXPECT(cd == empty + 1);
    EXPECT_STR(((Str){cd, strlen(cd)}), S("cd"));
    // A length whose NUL would wrap is the arena's refusal, before a read.
    EXPECT(arena_cstr(&a, ((Str){"x", SIZE_MAX})) == NULL);
    EXPECT(a.hit);
    EXPECT(refused);
    EXPECT_EQ(a.held, held);
    arena_deinit(&a);
}

TEST(a_size_sum_or_product_that_wraps_is_refused_and_one_that_fits_is_given) {
    bool refused = false;
    Arena a;
    arena_init(&a, 0, &refused);
    size_t out = 7;
    EXPECT(arena_size_add(&a, SIZE_MAX - 1, 1, &out));
    EXPECT_EQ(out, SIZE_MAX);
    EXPECT(arena_size_mul(&a, SIZE_MAX / 2, 2, &out));
    EXPECT_EQ(out, SIZE_MAX - 1);
    EXPECT(arena_size_mul(&a, SIZE_MAX, 0, &out));
    EXPECT_EQ(out, 0);
    EXPECT(!a.hit);
    out = 7;
    EXPECT(!arena_size_add(&a, SIZE_MAX, 1, &out));
    EXPECT(a.hit);
    EXPECT_EQ(out, 7);
    a.hit = false;
    EXPECT(!arena_size_mul(&a, SIZE_MAX / 2 + 1, 2, &out));
    EXPECT(a.hit);
    EXPECT_EQ(out, 7);
    EXPECT(refused);
    arena_deinit(&a);
}

TEST(a_misuse_ends_the_process) {
    bool refused = false;
    Arena a;
    arena_init(&a, 1000000, &refused);
    int x = 0;
    EXPECT_DIES((void)arena_alloc(&a, 8, 3));
    EXPECT_DIES(arena_free(&a, &x, ARENA_BIG));
    arena_deinit(&a);
}

TEST_MAIN(
    TEST_CASE(the_budget_refuses_past_its_limit_and_says_it_was_hit),
    TEST_CASE(a_refusal_sets_the_arenas_own_flag_and_no_other),
    TEST_CASE(chunks_start_at_the_least_and_double),
    TEST_CASE(a_chunk_near_the_limit_takes_only_what_is_left),
    TEST_CASE(a_chunk_too_small_for_one_allocation_is_made_its_size),
    TEST_CASE(a_big_allocation_has_its_own_chunk_and_gives_it_back),
    TEST_CASE(big_allocations_are_given_back_in_any_order),
    TEST_CASE(allocations_are_aligned_as_asked),
    TEST_CASE(only_the_last_allocation_grows_in_place),
    TEST_CASE(freeing_the_last_allocation_unbumps_it_and_any_other_is_kept),
    TEST_CASE(a_refusal_by_the_system_is_a_hit_and_holds_nothing),
    TEST_CASE(a_zero_length_allocation_holds_nothing_and_is_not_null),
    TEST_CASE(an_array_whose_size_overflows_is_refused),
    TEST_CASE(a_size_that_wraps_with_its_header_is_refused),
    TEST_CASE(an_array_whose_size_wraps_to_a_small_one_is_refused),
    TEST_CASE(the_arena_never_holds_more_than_its_limit_whatever_grows_in_it),
    TEST_CASE(deinit_gives_back_every_chunk_and_keeps_the_limit),
    TEST_CASE(dup_copies_into_the_arena_and_is_refused_when_full),
    TEST_CASE(a_c_string_ends_with_a_nul_and_one_holding_a_nul_is_refused),
    TEST_CASE(a_size_sum_or_product_that_wraps_is_refused_and_one_that_fits_is_given),
    TEST_CASE(a_misuse_ends_the_process)
)
