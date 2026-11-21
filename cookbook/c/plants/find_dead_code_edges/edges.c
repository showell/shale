// edges.c: a plant of find_dead_code's goldens, the edges its self-test's
// plants leave: a dead function two dead ones name, a generator only dead
// code names, a name two files define that a third reaches.
#include "kit.h"

VEC_DEFINE(Bytes, char)
VEC_DEFINE(Words, int)

// Named by two dead functions, each named by nothing.
static int edges_shared_callee(int x) {
    return x;
}

static int edges_dead_one(int x) {
    return edges_shared_callee(x) + edges_dead_two(x);
}

static int edges_dead_two(int x) {
    Bytes b = Bytes_make();
    (void)b;
    return edges_shared_callee(x);
}

// Reached through a data item main names.
static int edges_by_data(int x) {
    return x;
}

static const int EDGES_TABLE[] = {
    EDGES_ONE,
};

#define EDGES_ONE edges_by_data(1)

// Reached by main and by a test: live, no finding.
static int edges_both(int x) {
    return x;
}

int main(void) {
    Words w = Words_take();
    (void)w;
    return EDGES_TABLE[0] + edges_both(1) + sub_named(1);
}
