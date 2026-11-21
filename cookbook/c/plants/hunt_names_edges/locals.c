// locals.c: hunt_names' edge plants, the locals and parameters a line reader finds.
#include "test.h"

static int edge_locals(int data[4], const char *const info, void (*misc)(int), int handle) {
    unsigned tmp = 0;
    struct Point *temp;
    const char *const stuff = "int thing = 0;";
    int **obj;
    char util[8];
    static const unsigned helper = 3;
    Str things;
    return tmp;
    tmp = 4; /* int data = 0; */
    int count = 0; // int info = 1;
    unsigned int misc = 2;
    const struct Point thing = {0};
    return (int)data[0] + handle + count;
}

static int edge_one_line(int data, Str info) { int tmp = data; return tmp; }

static int edge_wrapped(
    int first,
    Str stuff[],
    int obj) {
    return first + obj;
}

static void edge_none(void) {
}

int main(int argc, char **argv) {
    int data = argc;
    (void)argv;
    return data;
}
