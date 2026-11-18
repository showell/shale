// depths.c: a plant of list_deep_nesting's self-test, functions of known
// depths. A body's own statements are at depth 1, and each block a `{` opens
// inside it is one deeper.
#include "test.h"

static int depth_one(int x) { return x; }

// A braceless `if` is no level, on one line or over two, and an initializer's
// braces on one line are none either.
static int braceless(int x) {
    int pair[2] = {1, 2};
    if (x < 0) return pair[0];
    if (x > 9)
        return pair[1];
    return x;
}

// A brace in a string, a character literal or a comment is no level: depth 2.
static const char *braces_in_text(int x) {
    // { { { a comment's braces
    if (x > 0) {
        return "{{{ a string's braces";
    }
    if (x == '{') return "}";
    return "";
}

static int depth_three(int x) {
    int n = 0;
    for (int i = 0; i < x; i++) {
        if (i % 2 == 0) {
            n++;
        } else {
            n--;
        }
    }
    return n;
}

static int depth_four(int x) {
    int n = 0;
    for (int i = 0; i < x; i++) {
        for (int j = 0; j < i; j++) {
            if (j % 3 == 0) {
                n++;
            }
        }
    }
    return n;
}

static int depth_five(int x) {
    int n = 0;
    while (n < x) {
        switch (n % 4) {
        case 0: {
            if (x > 100) {
                n += 2;
            }
            break;
        }
        default:
            n++;
        }
    }
    return n;
}

TEST(a_nested_test) {
    if (depth_one(1) == 1) {
        EXPECT(braceless(3) == 3);
    }
}

TEST_MAIN(
    TEST_CASE(a_nested_test)
)
