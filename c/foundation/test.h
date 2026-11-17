// test.h: the tests' harness. A test is `TEST(name) { ... }`; a file ends with
// `TEST_MAIN(TEST_CASE(a), TEST_CASE(b), ...)`, which runs every case named,
// in order, and prints the exact count:
//
//     c/tests/test_arena.c: 12 tests passed
//
// A failure names its test and its line and ends that test (the rest still
// run), and the run exits 1. There is no skip: a `TEST` left out of
// `TEST_MAIN` is a static function nothing calls, which GCC's `-Wall -Werror`
// refuses, and `c/build.shale` holds each file's printed count to its `TEST`s.

#ifndef SHALE_TEST_H
#define SHALE_TEST_H

// `fork` and `waitpid`, for EXPECT_DIES; a test file includes test.h first.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <setjmp.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "slice.h"

typedef struct {
    Str name;
    void (*fn)(void);
} TestCase;

SLICE_TYPE(TestCases, const TestCase);

typedef struct {
    jmp_buf back;
    Str current;
    size_t failed;
} TestState;

static inline TestState *test_state(void) {
    static TestState state;
    return &state;
}

#define TEST(name) static void test_##name(void)
#define TEST_CASE(name) {S(#name), test_##name}

static inline void test_fail(const char *file, int line, Str what) { // cstr: __FILE__
    TestState *t = test_state();
    fprintf(stderr, "FAIL %.*s (%s:%d): %.*s\n", (int)t->current.len, t->current.ptr, file, line, (int)what.len, what.ptr);
    longjmp(t->back, 1);
}

#define EXPECT(cond)                                                    \
    do {                                                                \
        if (!(cond)) test_fail(__FILE__, __LINE__, S(#cond));           \
    } while (0)

#define EXPECT_EQ(a, b)                                                                     \
    do {                                                                                    \
        uint64_t test_a_ = (uint64_t)(a), test_b_ = (uint64_t)(b);                          \
        if (test_a_ != test_b_) {                                                           \
            fprintf(stderr, "  %s is %llu, %s is %llu\n", #a, (unsigned long long)test_a_,  \
                    #b, (unsigned long long)test_b_);                                       \
            test_fail(__FILE__, __LINE__, S(#a " == " #b));                                 \
        }                                                                                   \
    } while (0)

#define EXPECT_STR(a, b)                                                                    \
    do {                                                                                    \
        Str test_a_ = (a), test_b_ = (b);                                                   \
        if (!str_eq(test_a_, test_b_)) {                                                    \
            fprintf(stderr, "  got  \"%.*s\"\n  want \"%.*s\"\n", (int)test_a_.len,         \
                    test_a_.ptr, (int)test_b_.len, test_b_.ptr);                            \
            test_fail(__FILE__, __LINE__, S(#a " is " #b));                                 \
        }                                                                                   \
    } while (0)

// What a bad index calls in an EXPECT_TRAPS child: it exits with
// TEST_TRAPPED, so the parent knows the trap and not something else ended it.
enum { TEST_TRAPPED = 42 };

static inline void test_trap(const char *file, int line, size_t i, size_t len) { // cstr: __FILE__
    (void)file;
    (void)line;
    (void)i;
    (void)len;
    _exit(TEST_TRAPPED);
}

// Runs `stmt` in a child, its stderr dropped and no core dumped (a core piped
// to apport takes a second, and a pipe ignores RLIMIT_CORE, so the child is
// made not dumpable), and gives how the child ended. `trap` arms the bounds
// trap in the child. A child, not a longjmp: a jump back into the test's frame
// would clobber what `stmt` made there (GCC's -Wclobbered).
#define TEST_IN_CHILD(stmt, trap, status)                                           \
    do {                                                                            \
        fflush(stdout);                                                             \
        fflush(stderr);                                                             \
        pid_t test_pid_ = fork();                                                   \
        if (test_pid_ == 0) {                                                       \
            if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) _exit(3);                  \
            if (freopen("/dev/null", "w", stderr) == NULL) _exit(3);                \
            if (trap) *slice_trap_slot() = test_trap;                               \
            stmt;                                                                   \
            _exit(0);                                                               \
        }                                                                           \
        EXPECT(test_pid_ > 0 && waitpid(test_pid_, &(status), 0) == test_pid_);     \
    } while (0)

// `stmt` must trap on a bad index (slice.h); the test fails if it finishes.
#define EXPECT_TRAPS(stmt)                                                          \
    do {                                                                            \
        int test_status_ = 0;                                                       \
        TEST_IN_CHILD(stmt, true, test_status_);                                    \
        if (!(WIFEXITED(test_status_) && WEXITSTATUS(test_status_) == TEST_TRAPPED)) \
            test_fail(__FILE__, __LINE__, S("`" #stmt "` did not trap"));           \
    } while (0)

// `stmt` must end the process with SIGABRT (a bad index with no trap, an
// arena misuse).
#define EXPECT_DIES(stmt)                                                           \
    do {                                                                            \
        int test_status_ = 0;                                                       \
        TEST_IN_CHILD(stmt, false, test_status_);                                   \
        if (!(WIFSIGNALED(test_status_) && WTERMSIG(test_status_) == SIGABRT))      \
            test_fail(__FILE__, __LINE__, S("`" #stmt "` did not abort"));          \
    } while (0)

static inline int test_run(TestCases cases, Str file) {
    TestState *t = test_state();
    volatile size_t ran = 0;
    for (volatile size_t k = 0; k < cases.len; k++) {
        const TestCase *c = &at(cases, k);
        t->current = c->name;
        if (setjmp(t->back) == 0) {
            c->fn();
        } else {
            t->failed++;
        }
        ran++;
    }
    if (ran == 0) {
        fprintf(stderr, "%.*s: no tests\n", (int)file.len, file.ptr);
        return 1;
    }
    if (t->failed > 0) {
        fprintf(stderr, "%.*s: %zu of %zu tests failed\n", (int)file.len, file.ptr, t->failed, (size_t)ran);
        return 1;
    }
    printf("%.*s: %zu tests passed\n", (int)file.len, file.ptr, (size_t)ran);
    return 0;
}

#define TEST_MAIN(...)                                                              \
    int main(void) {                                                                \
        static const TestCase cases[] = {__VA_ARGS__};                              \
        TestCases all = {cases, sizeof cases / sizeof cases[0]};                    \
        return test_run(all, S(__FILE__));                                          \
    }

#endif
