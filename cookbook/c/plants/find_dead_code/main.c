// main.c: a plant of find_dead_code's self-test, the program's entry.
#include "lib.c"

int main(void) {
    Ints xs = Ints_make();
    Ints_push(&xs, 1);
    Typed t;
    Holder h;
    (void)h;
    (void)t;
    // lib_in_comment(1) is no use.
    const char *s = "lib_in_comment(2)";
    (void)s;
    int n = LIB_CALL(1) + lib_live(2) + LIB_HOOKS[0](3);
    return n + hold.lib_member + hold_ptr->lib_member;
}
