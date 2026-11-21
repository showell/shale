// one.c: a plant of find_dead_code's goldens, one of two files defining a
// name main reaches from a third.
static int sub_named(int x) {
    return x;
}

static int sub_lonely(int x) {
    return x;
}
