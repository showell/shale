// main_test.c: a plant of find_dead_code's goldens, a test program's own
// main, a root of the tests'.
static int main_test_helper(int x) {
    return edges_both(x) + kit_for_tests(x) + sub_test_only(x);
}

static int main_test_stray(int x) {
    return main_test_stray(x);
}

int main(void) {
    return main_test_helper(1);
}
