// quiet_do_word.c: a plant of hunt_names' self-test, do_word's near misses.
#include "test.h"

static void store_done(int n) {
    do {
        n--;
    } while (n > 0);
}
