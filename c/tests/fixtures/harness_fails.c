// The harness held to its own failures: every case but the first fails, each
// in its own way, and `c/build.shale` holds the run to harness_fails.expected
// exactly (each failure's test and line, the count, exit 1).

#include "test.h"

TEST(passes) {
    EXPECT(1 + 1 == 2);
}

TEST(expect_fails) {
    EXPECT(1 + 1 == 3);
}

TEST(eq_fails) {
    EXPECT_EQ(3, 4);
}

TEST(str_fails) {
    EXPECT_STR(S("got"), S("want"));
}

TEST(no_trap_fails) {
    int x = 0;
    EXPECT_TRAPS(x = 1);
    (void)x;
}

TEST(no_death_fails) {
    EXPECT_DIES((void)0);
}

TEST(stops_at_its_first_failure) {
    EXPECT(0);
    EXPECT(1 == 3);
}

TEST_MAIN(
    TEST_CASE(passes),
    TEST_CASE(expect_fails),
    TEST_CASE(eq_fails),
    TEST_CASE(str_fails),
    TEST_CASE(no_trap_fails),
    TEST_CASE(no_death_fails),
    TEST_CASE(stops_at_its_first_failure)
)
