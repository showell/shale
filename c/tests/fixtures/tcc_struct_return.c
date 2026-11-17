// tcc_struct_return.c: a canary for TCC 0.9.27's miscompile of a struct of 9
// to 15 bytes returned straight into another call as its argument, `f(g())`.
// c/docs/style.md ("Declarations") steers around it, and
// c/check_discipline.shale holds the rule. The bug is known upstream, in
// tinycc-devel's thread "A double-to-float conversion bug" of July 2023
// (https://lists.libreplanet.org/archive/html/tinycc-devel/2023-07/msg00027.html),
// and grischka fixed it on TinyCC's mob branch on 2023-07-31, after 0.9.27.
//
// It prints how many of five pairs of calls went wrong in each shape.
// c/build/discipline.shale runs it under TCC in both tiers, where the
// straight shape must still go wrong and the local one must not: the day TCC
// gets the straight one right, the run fails, saying the rule can be retired.
// The full tier runs it under GCC too, where both must be right.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum { INST_PLAIN, INST_SAVE } InstOp;

// Twelve bytes, a size TCC 0.9.27 gets wrong: regex.c's instruction once had
// this shape.
typedef struct {
    InstOp op;
    uint8_t c;
    uint16_t x;
    uint16_t y;
} Inst12;

// The `x` of the first two instructions `inst_taken` saw since the count was
// cleared.
static uint16_t seen_first;
static uint16_t seen_second;
static int seen_count;

static void inst_taken(void *e, Inst12 i, void *z) {
    (void)e;
    (void)z;
    if (seen_count == 0) seen_first = i.x;
    if (seen_count == 1) seen_second = i.x;
    seen_count++;
}

static Inst12 inst_made(uint16_t x) { // struct return: the canary's shape, on purpose
    Inst12 i = {INST_SAVE, 0, x, 0};
    return i;
}

// Whether two instructions, each returned straight into the call that takes
// it, arrive as they were made.
static bool straight_right(unsigned g) {
    seen_count = 0;
    uint16_t k = (uint16_t)(g - 1);
    inst_taken(NULL, inst_made((uint16_t)(2 * k)), NULL);
    inst_taken(NULL, inst_made((uint16_t)(2 * k + 1)), NULL);
    return seen_first == 2 * (g - 1) && seen_second == 2 * (g - 1) + 1;
}

// The same two, each kept in a local first.
static bool via_local_right(unsigned g) {
    seen_count = 0;
    uint16_t k = (uint16_t)(g - 1);
    Inst12 a = inst_made((uint16_t)(2 * k));
    inst_taken(NULL, a, NULL);
    Inst12 b = inst_made((uint16_t)(2 * k + 1));
    inst_taken(NULL, b, NULL);
    return seen_first == 2 * (g - 1) && seen_second == 2 * (g - 1) + 1;
}

int main(void) {
    int straight_wrong = 0;
    int local_wrong = 0;
    for (unsigned g = 1; g < 6; g++) {
        if (!straight_right(g)) straight_wrong++;
        if (!via_local_right(g)) local_wrong++;
    }
    printf("returned straight into a call: %d of 5 wrong; kept in a local first: %d of 5 wrong\n", straight_wrong, local_wrong);
    return 0;
}
