// kit.h: a plant of find_dead_code's goldens, a generator macro whose
// functions are not vec.h's.
#ifndef SHALE_KIT_H
#define SHALE_KIT_H

#define VEC_DEFINE(Name, T)                                                    \
    typedef struct {                                                           \
        T *ptr;                                                                \
    } Name;                                                                    \
    static inline Name Name##_make(void) {                                     \
        Name v = {0};                                                          \
        return v;                                                              \
    }                                                                          \
    static inline Name Name##_take(void) {                                     \
        Name v = {0};                                                          \
        return v;                                                              \
    }

// Named only by a test's helper of the tests' own file: test-only.
static inline int kit_for_tests(int x) {
    return x;
}

#endif
