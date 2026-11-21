// two.c: a plant of find_dead_code's goldens, the other file defining it.
static int sub_named(int x) {
    return x + 1;
}

static int sub_test_only(int x) {
    return x;
}
