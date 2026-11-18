// lengths.c: a plant of list_long_functions' self-test, functions of known
// lengths.
#include "test.h"

static int one_line(int x) { return x; }

static int two_lines(int x) {
    int y = x;
    return y;
}

// Four lines of code, and comments and blank lines that are no code.
static int four_with_comments(int x) {
    // A comment line.
    int a = x;

    // Another, then a blank line.
    int b = a;

    int c = b;
    // A third.
    return c;
}

static int six_lines(int x) {
    int a = x;
    int b = a + 1;
    int c = b + 1;
    int d = c + 1;
    // Past the comment, two more.
    int e = d + 1;
    return e;
}

TEST(a_long_test) {
    int a = 1;
    int b = a;
    int c = b;
    int d = c;
    int e = d;
    int f = e;
    int g = f;
    EXPECT(g == 1);
}

TEST_MAIN(
    TEST_CASE(a_long_test)
)
