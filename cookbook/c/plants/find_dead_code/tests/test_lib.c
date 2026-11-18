// test_lib.c: a plant of find_dead_code's self-test, a test file.
#include "../lib.c"

// A test's own helper, used by its test: no finding.
static int test_lib_helper(int x) {
    return lib_tested(x) + lib_header_tested(x);
}

// A test file's helper nobody calls: dead.
static int test_lib_stray(int x) {
    return x;
}

// A name another test file also defines, unused here: dead, though the
// other file's is used.
static int pick(int x) {
    return x;
}

TEST(lib_works) {
    EXPECT(test_lib_helper(1) == 3);
}

TEST_MAIN(
    TEST_CASE(lib_works)
)
