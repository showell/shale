// vec.h: a plant of find_dead_code's self-test, a generator as c/foundation/vec.h's.
#ifndef SHALE_VEC_H
#define SHALE_VEC_H

#define VEC_DEFINE(Name, T)                                                    \
    typedef struct {                                                           \
        T *ptr;                                                                \
    } Name;                                                                    \
    static inline Name Name##_make(void) {                                     \
        Name v = {0};                                                          \
        return v;                                                              \
    }                                                                          \
    static inline void Name##_push(Name *v, T x) {                             \
        (void)v;                                                               \
        (void)x;                                                               \
    }

#endif
