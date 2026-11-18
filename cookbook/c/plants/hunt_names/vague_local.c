// vague_local.c: a plant of hunt_names' self-test, vague locals.
#include "test.h"

static int vague_local_sum(Str data) {
    int tmp = 0;
    return tmp + (int)data.len;
}
