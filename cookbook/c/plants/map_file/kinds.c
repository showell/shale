// kinds.c: a plant of map_file's self-test, the items a .c file holds.
#include "test.h"

static void (*kinds_hook)(int);

static int kinds_wrapped(int a,
                         int b) {
    int sum = kinds_add(a, b);
    // kinds_in_comment(sum) is no use.
    return sum + other.member + p->field;
}

TEST(kinds_test) {
    EXPECT(kinds_wrapped(1, 2) == 3);
}

TEST_MAIN(
    kinds_test
)

int main(void) {
    return kinds_wrapped(0, 0);
}
