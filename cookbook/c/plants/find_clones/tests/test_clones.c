// test_clones.c: a plant of find_clones' self-test, a test's oracle that
// copies the code it judges, left out unless tests=1.
#include "../clones.c"

static int oracle_weights(const Items *items, int floor) {
    int total = 0;
    for (size_t i = 0; i < items->len; i++) {
        Item it = at(items->ptr, i);
        if (it.weight < floor) continue;
        total += it.weight * 2;
        log_line(S("weight"), it.weight);
    }
    if (total > 100) total = 100;
    return total;
}
