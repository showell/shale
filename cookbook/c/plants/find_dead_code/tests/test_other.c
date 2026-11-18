// test_other.c: a plant of find_dead_code's self-test, a second test file.
#include "../lib.c"

static int pick(int x) {
    return x;
}

TEST(other_works) {
    EXPECT(pick(1) == 1);
}

TEST_MAIN(
    TEST_CASE(other_works)
)
