// quiet_vague_local.c: a plant of hunt_names' self-test, vague_local's near misses.
#include "test.h"

static int quiet_sum(Str bytes, int data_len) {
    // int tmp = 0; is a comment's.
    int total = data_len;
    total += (int)bytes.len;
    return total;
}

static int quiet_give(int info_count) {
    int data_count = info_count;
    return data_count;
}
