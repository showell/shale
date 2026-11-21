// names.c: hunt_names' edge plants, names of every case.
#include "test.h"

#define lower_macro(x) ((x) + 1)
#define EDGE_LIMIT_MOST 4
#define EDGE_MAX_SIZE_BYTES 9
#define EDGE__DOUBLE_MAX_ 1
#define MAX_EDGE 2

enum {
    EDGE_MIN_A_B = 1,
    EDGE_SPAN_MAX_US = 2,
    EDGE_SPAN_MAX_X_MS = 3,
    edgeMixed = 4,
    EDGE_LEAST = 5,
};

typedef enum {
    EDGE_KIND_ONE,
    EDGE_KIND_TWO,
} EdgeKind;

typedef enum {
    EDGE_LONE,
} edge_lone_t;

typedef struct EdgeItem {
    int a;
} EdgeItem;

typedef struct {
    int b;
} ItemList;

typedef int (*EdgeFn)(int);

static int Vec2DLength(void) {
    return 0;
}

static int run(void) {
    return 0;
}

static int handle(void) {
    return 0;
}

static int run_handle(void) {
    return 0;
}

static int do_edge(void) {
    return 0;
}

static int undo_edge(void) {
    return 0;
}

static int edge_utf8Name(void) {
    return 0;
}

static const int edge_table[2] = {1, 2};
