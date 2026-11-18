// quiet_function_case.c: a plant of hunt_names' self-test, function_case's near misses.
#include "test.h"

static int read_thing(void) {
    return 0;
}

int main(void) {
    return read_thing();
}
