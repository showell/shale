// lib.c: a plant of find_dead_code's self-test, each way a function is used or
// is not.
#include "lib.h"

VEC_DEFINE(Ints, int)
VEC_DEFINE(Chars, char)
VEC_DEFINE(Typed, int)
SLICE_TYPE(Fields, int);

// A generator's type named only in a struct's field, which main names.
typedef struct {
    Fields fields;
} Holder;

// A prototype is no use of its function.
static int lib_proto_only(void);

static int lib_proto_only(void) {
    return 0;
}

static int lib_live(int x) {
    return lib_header_live(x);
}

// Taken by name into a table main reads: a pointer, never called, is a use.
static int lib_by_pointer(int x) {
    return x;
}

static int (*const LIB_HOOKS[])(int) = {
    lib_by_pointer,
};

// Named by a macro main uses.
static int lib_in_macro(int x) {
    return x;
}

#define LIB_CALL(x) lib_in_macro(x)

// Named by a macro nobody uses.
static int lib_in_unused_macro(int x) {
    return x;
}

#define LIB_UNUSED_CALL(x) lib_in_unused_macro(x)

// Calls itself alone.
static int lib_recursive(int x) {
    if (x <= 0) return lib_by_dead(x);
    return lib_recursive(x - 1);
}

// Called by dead code alone.
static int lib_by_dead(int x) {
    return x;
}

// Named only in main's comment and string.
static int lib_in_comment(int x) {
    return x;
}

// Named only as a member after `.` and `->`.
static int lib_member(int x) {
    return x;
}

// Called by a test alone.
static int lib_tested(int x) {
    return x;
}
