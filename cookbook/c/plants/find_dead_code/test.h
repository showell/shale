// test.h: a plant of find_dead_code's self-test, the tests' runner, whose
// file is the tests' own.
#ifndef SHALE_TEST_H
#define SHALE_TEST_H

// Reached by the tests' macro: a runner's helper, no finding.
static inline void test_fail(int line) {
    (void)line;
}

#define EXPECT(c) ((c) ? (void)0 : test_fail(__LINE__))

// Named by nothing: dead, though the file is the tests'.
static inline void test_unused_kit(void) {
}

#endif
