// kinds.h: a plant of map_file's self-test, one item of each kind a header holds.
#ifndef SHALE_KINDS_H
#define SHALE_KINDS_H

#include <stdint.h>
#include "slice.h"

// A one-line macro.
#define KINDS_MAX 8
#define KINDS_DEFINE(Name, T) \
    static inline Name Name##_make(void) { \
        Name v = {0}; \
        return v; \
    } \
    static inline T Name##_first(Name *v) { \
        return v->x; \
    }

#if defined(__linux__)
#define KINDS_LINUX 1
#endif

typedef struct {
    uint32_t a;
    uint32_t b;
} KindsPair;

typedef enum { KINDS_A, KINDS_B } KindsKind;

typedef union {
    uint64_t word;
    char bytes[8];
} KindsWord;

typedef void (*KindsHook)(int code);

enum {
    KINDS_FIRST = 1,
    KINDS_SECOND,
};

SLICE_TYPE(KindsPairs, KindsPair);
VEC_DEFINE(KindsVec, KindsPair)

_Static_assert(sizeof(KindsPair) == 8, "a pair is two words");

static inline uint32_t kinds_sum(KindsPair p) { return p.a + p.b; }

// A multi-line function, with a directive in its body and strings that hold
// what would be code.
static inline Str kinds_name(KindsKind k) {
    if (k == KINDS_A) return S("{ kinds_fake(1); }");
#ifdef KINDS_LINUX
    return kinds_label(k, '}');
#else
    return S("B");
#endif
}

static inline STATUS_MUST_USE Status kinds_check(KindsPair *p,
                                                 uint32_t most);

static const Str KINDS_NAMES[] = {
    S_ITEM("a"),
    S_ITEM("b"),
};
static const uint32_t KINDS_ONE = 1;

#endif
