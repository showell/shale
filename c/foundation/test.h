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
// `TEST_ONLY=a,b` in the environment runs only the cases it names (a plant's
// aimed tests, a tier's part of a file), each name one a case has.
//
// Test files that share a costly include (c/tests/ran.c, the interpreter)
// are parts of one binary instead: each ends with `TEST_PART`, the binary
// includes them all and ends with `TEST_PARTS_MAIN`, and it prints each
// part's count in its own line, as that file alone would.

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
    // Set in a death test's child, where a failure exits rather than jumps.
    bool in_child;
} TestState;

// How a death test's child ends when an EXPECT inside its `stmt` fails.
enum { TEST_CHILD_FAILED = 2 };

static inline TestState *test_state(void) {
    static TestState state;
    return &state;
}

// A table of literal strings a test loops over, counted by the compiler:
// `Lits paths = LITS("a", "b/c");` is a Lits of 2, so a case added to the
// list runs with no count to keep in step. The count sizes the loop over
// the cases only. A count the test asserts (how many a call found, how
// many lines it wrote) stays a literal number: one computed from the table
// agrees with any table, so it would hold whatever the code did.
SLICE_TYPE(Lits, const char *const); // cstr: a test's literals

#define LITS(...)                                                                          \
    ((Lits){(const char *const[]){__VA_ARGS__},                                            \
            sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *)})

#define TEST(name) static void test_##name(void)
#define TEST_CASE(name) {S(#name), test_##name}

static inline void test_fail(const char *file, int line, Str what) { // cstr: __FILE__
    TestState *t = test_state();
    fprintf(stderr, "FAIL %.*s (%s:%d): %.*s\n", (int)t->current.len, t->current.ptr, file, line, (int)what.len, what.ptr);
    // A jump in a child would land in its copy of test_run and run the
    // file's other tests there; the child ends, and the parent reads why.
    if (t->in_child) _exit(TEST_CHILD_FAILED);
    longjmp(t->back, 1);
}

#define EXPECT(cond)                                                    \
    do {                                                                \
        if (!(cond)) test_fail(__FILE__, __LINE__, S(#cond));           \
    } while (0)

#define EXPECT_EQ(a, b)                                                                     \
    do {                                                                                    \
        uint64_t test_a_ = (uint64_t)(a);                                                   \
        uint64_t test_b_ = (uint64_t)(b);                                                   \
        if (test_a_ != test_b_) {                                                           \
            fprintf(stderr, "  %s is %llu, %s is %llu\n", #a, (unsigned long long)test_a_,  \
                    #b, (unsigned long long)test_b_);                                       \
            test_fail(__FILE__, __LINE__, S(#a " == " #b));                                 \
        }                                                                                   \
    } while (0)

// EXPECT_EQ for signed values: both sides as int64_t, printed signed. Under
// EXPECT_EQ's uint64_t a failing -1 prints as 18446744073709551615, and -1
// equals SIZE_MAX.
#define EXPECT_EQ_I(a, b)                                                                   \
    do {                                                                                    \
        int64_t test_a_ = (int64_t)(a);                                                     \
        int64_t test_b_ = (int64_t)(b);                                                     \
        if (test_a_ != test_b_) {                                                           \
            fprintf(stderr, "  %s is %lld, %s is %lld\n", #a, (long long)test_a_,           \
                    #b, (long long)test_b_);                                                \
            test_fail(__FILE__, __LINE__, S(#a " == " #b));                                 \
        }                                                                                   \
    } while (0)

#define EXPECT_STR(a, b)                                                                  \
    do {                                                                                    \
        Str test_a_ = (a);                                                                  \
        Str test_b_ = (b);                                                                  \
        if (!str_eq(test_a_, test_b_)) {                                                    \
            fprintf(stderr, "  got  \"%.*s\"\n  want \"%.*s\"\n", (int)test_a_.len,         \
                    test_a_.ptr, (int)test_b_.len, test_b_.ptr);                            \
            test_fail(__FILE__, __LINE__, S(#a " is " #b));                                 \
        }                                                                                   \
    } while (0)

// `expr` must give OK; when it doesn't, the failure says the stop that the
// run `ctx` (a `RunCtx *`) recorded: its place, class, budget if any and
// message, or that it recorded none. A host or file system test that fails then
// says why, where `EXPECT((expr) == OK)` says only that it stopped. It
// reads err.h's RunCtx at its use, so test.h itself still needs only slice.
#define EXPECT_OK_IN(ctx, expr)                                                             \
    do {                                                                                    \
        Status test_s_ = (expr);                                                            \
        if (test_s_ != OK) {                                                                \
            const RunCtx *test_c_ = (ctx);                                                  \
            if (test_c_->stopped) {                                                         \
                const Stop *test_t_ = &test_c_->stop;                                       \
                Str test_cls_ = stop_class_name(test_t_->cls);                              \
                Str test_bud_ = budget_name(test_t_->budget);                               \
                fprintf(stderr, "  stopped at %u:%u: %.*s%s%.*s: %.*s\n",                   \
                        (unsigned)test_t_->line, (unsigned)test_t_->column,                 \
                        (int)test_cls_.len, test_cls_.ptr, test_bud_.len > 0 ? " " : "",    \
                        (int)test_bud_.len, test_bud_.ptr, (int)test_t_->message.len,       \
                        test_t_->message.ptr);                                              \
            } else {                                                                        \
                fprintf(stderr, "  no stop recorded\n");                                    \
            }                                                                               \
            test_fail(__FILE__, __LINE__, S(#expr " is OK"));                               \
        }                                                                                   \
    } while (0)

// What a bad index calls in an EXPECT_TRAPS child: it exits with
// TEST_TRAPPED, so the parent knows the trap and not something else ended it.
enum { TEST_TRAPPED = 42 };

static inline void test_trap(const char *file, int line, uint64_t i, size_t len) { // cstr: __FILE__
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
// DRAGON: the process boundary. The child is a fork with no exec, so it must
// not outlive the test, and a failed EXPECT inside `stmt` must not jump back
// into the child's copy of test_run and run the file's other tests there.
// REMEDY: the child takes SIGKILL as its death signal and ends at once if
// the parent was gone before it asked (`getppid` no longer the parent); a
// failed EXPECT exits it with TEST_CHILD_FAILED; a child that cannot be set
// up exits 3. test_test catches a plant of each of the first two.
#define TEST_IN_CHILD(stmt, trap, status)                                           \
    do {                                                                            \
        fflush(stdout);                                                             \
        fflush(stderr);                                                             \
        pid_t test_parent_ = getpid();                                              \
        pid_t test_pid_ = fork();                                                   \
        if (test_pid_ == 0) {                                                       \
            if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0) _exit(3);           \
            if (getppid() != test_parent_) _exit(3);                                \
            test_state()->in_child = true;                                          \
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

// Whether `name` is one of the comma-separated names in `only`.
static inline bool test_named(Str only, Str name) {
    size_t from = 0;
    for (size_t i = 0; i <= only.len; i++) {
        if (i < only.len && str_byte(only, i) != ',') continue;
        if (str_eq(str_sub(only, from, i), name)) return true;
        from = i + 1;
    }
    return false;
}

// A test file's cases and its name: what a binary runs and counts as one
// file. A file of its own is one part (`TEST_MAIN`); a binary of several
// files, each a `TEST_PART`, runs each in turn (`TEST_PARTS_MAIN`), so the
// interpreter they share is compiled once for all of them.
// `cases` is a function, its array static inside it, so a part names no
// array of its own beside its `TestPart`.
typedef struct {
    TestCases (*cases)(void);
    Str file;
} TestPart;

SLICE_TYPE(TestParts, const TestPart *const);

// Whether every comma-separated name in `only` names a case of one of the
// parts; the first that doesn't goes in `missing`.
static inline bool test_all_named(TestParts parts, Str only, Str *missing) {
    size_t from = 0;
    for (size_t i = 0; i <= only.len; i++) {
        if (i < only.len && str_byte(only, i) != ',') continue;
        Str name = str_sub(only, from, i);
        bool found = false;
        for (size_t p = 0; p < parts.len; p++) {
            TestCases cases = at(parts, p)->cases();
            for (size_t k = 0; k < cases.len; k++) {
                if (str_eq(at(cases, k).name, name)) found = true;
            }
        }
        if (!found) {
            *missing = name;
            return false;
        }
        from = i + 1;
    }
    return true;
}

// Runs one case, a failure counted. The setjmp is alone in this function,
// so no local of its caller lives across it (GCC's -Wclobbered at -O2).
static inline void test_run_case(const TestCase *c) {
    TestState *t = test_state();
    t->current = c->name;
    if (setjmp(t->back) == 0) {
        c->fn();
    } else {
        test_state()->failed++;
    }
}

// Runs one part's cases, or with `TEST_ONLY` set only those it names, and
// says the part's count in a line of its own. A part none of whose cases
// `TEST_ONLY` names runs nothing and says nothing. Gives how many ran, and
// sets `failed` when one failed.
static inline size_t test_run_part(const TestPart *part, const char *only_env, Str only, bool *failed) { // cstr: getenv's
    TestState *t = test_state();
    TestCases cases = part->cases();
    Str file = part->file;
    t->failed = 0;
    size_t ran = 0;
    for (size_t k = 0; k < cases.len; k++) {
        const TestCase *c = &at(cases, k);
        if (only_env != NULL && !test_named(only, c->name)) continue;
        test_run_case(c);
        ran++;
    }
    if (ran == 0) return 0;
    if (t->failed > 0) {
        fprintf(stderr, "%.*s: %zu of %zu tests failed\n", (int)file.len, file.ptr, t->failed, (size_t)ran);
        *failed = true;
    } else {
        printf("%.*s: %zu tests passed\n", (int)file.len, file.ptr, (size_t)ran);
    }
    // The next part's tests fork: a child that exits through stdio would
    // print this line again.
    fflush(stdout);
    return ran;
}

// Runs every part's cases in order, or with `TEST_ONLY` set, only those it
// names (`a,b`), so a planted fault runs only the tests aimed at it. A name
// in it that names no case of any part fails the run, so an aim that went
// stale can't pass by running nothing. Each part's count printed is of its
// cases that ran; `file` is the binary's, for the run's own failures.
static inline int test_run(TestParts parts, Str file) {
    const char *only_env = getenv("TEST_ONLY");
    Str only = {"", 0};
    if (only_env != NULL) only = (Str){only_env, strlen(only_env)};
    Str missing = {"", 0};
    if (only_env != NULL && !test_all_named(parts, only, &missing)) {
        fprintf(stderr, "%.*s: no test named `%.*s`\n", (int)file.len, file.ptr, (int)missing.len, missing.ptr);
        return 1;
    }
    size_t ran = 0;
    bool failed = false;
    for (size_t p = 0; p < parts.len; p++) ran += test_run_part(at(parts, p), only_env, only, &failed);
    if (ran == 0) {
        fprintf(stderr, "%.*s: no tests\n", (int)file.len, file.ptr);
        return 1;
    }
    return failed ? 1 : 0;
}

// A test file that is a part of a binary of several: it ends with
// `TEST_PART(name, TEST_CASE(a), ...)` where a file of its own ends with
// `TEST_MAIN`, and has no `main`.
#define TEST_PART(name, ...)                                                        \
    static TestCases test_part_cases_##name(void) {                                 \
        static const TestCase cases[] = {__VA_ARGS__};                              \
        TestCases all = {cases, sizeof cases / sizeof(TestCase)};                   \
        return all;                                                                 \
    }                                                                               \
    static const TestPart test_part_##name = {test_part_cases_##name, S_ITEM(__FILE__)}

#define TEST_PART_OF(name) &test_part_##name

// A file of its own: one part, the file.
#define TEST_MAIN(...)                                                              \
    TEST_PART(main_file, __VA_ARGS__);                                              \
    int main(void) {                                                                \
        static const TestPart *const parts[] = {&test_part_main_file};              \
        TestParts all = {parts, 1};                                                 \
        return test_run(all, S(__FILE__));                                          \
    }

// The binary of several parts: `TEST_PARTS_MAIN(TEST_PART_OF(a), ...)`, after
// it includes each part's file.
#define TEST_PARTS_MAIN(...)                                                        \
    int main(void) {                                                                \
        static const TestPart *const parts[] = {__VA_ARGS__};                       \
        TestParts all = {parts, sizeof parts / sizeof(const TestPart *)};           \
        return test_run(all, S(__FILE__));                                          \
    }

#endif
