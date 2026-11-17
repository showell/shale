// test.h: a death test's child dies with its parent (its death signal is
// SIGKILL), and a failed EXPECT inside its statement ends the child with
// TEST_CHILD_FAILED rather than running the file's other tests there.

#include "test.h"

// Ends the process with 5 unless its death signal is SIGKILL.
static void exit_unless_death_signal_is_kill(void) {
    int sig = 0;
    if (prctl(PR_GET_PDEATHSIG, &sig, 0, 0, 0) != 0 || sig != SIGKILL) _exit(5);
}

// Ends the process with 6 when it is not marked as a child.
static void exit_unless_marked_a_child(void) {
    if (!test_state()->in_child) _exit(6);
}

TEST(a_death_tests_child_dies_with_its_parent) {
    int status = 0;
    TEST_IN_CHILD(exit_unless_death_signal_is_kill(), false, status);
    EXPECT(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(a_failed_expect_in_a_child_ends_the_child_with_its_own_code) {
    int status = 0;
    TEST_IN_CHILD(EXPECT(1 == 2), false, status);
    EXPECT(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), TEST_CHILD_FAILED);
    // The parent is not marked, and counts no failure of the child's.
    EXPECT(!test_state()->in_child);
    EXPECT_EQ(test_state()->failed, 0);
}

TEST(a_passing_child_is_marked_and_exits_zero) {
    int status = 0;
    TEST_IN_CHILD(exit_unless_marked_a_child(), false, status);
    EXPECT(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST_MAIN(
    TEST_CASE(a_death_tests_child_dies_with_its_parent),
    TEST_CASE(a_failed_expect_in_a_child_ends_the_child_with_its_own_code),
    TEST_CASE(a_passing_child_is_marked_and_exits_zero)
)
