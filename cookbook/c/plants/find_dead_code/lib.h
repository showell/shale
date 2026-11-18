// lib.h: a plant of find_dead_code's self-test, a header's functions.
#ifndef SHALE_LIB_H
#define SHALE_LIB_H

#include "vec.h"

// Called by main: live.
static inline int lib_header_live(int x) { return x + 1; }

// Named by nothing: dead.
static inline int lib_header_dead(int x) { return x - 1; }

// Named by a test alone: test-only.
static inline int lib_header_tested(int x) { return x * 2; }

#endif
