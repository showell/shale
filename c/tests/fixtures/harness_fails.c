// The harness held to its own failures: every case but the first fails, each
// in its own way, and `c/build.shale` holds the run to harness_fails.expected
// exactly (each failure's test and line, the count, exit 1).

#include "test.h"
#include "err.h"

TEST(passes) {
    EXPECT(1 + 1 == 2);
}

TEST(expect_fails) {
    EXPECT(1 + 1 == 3);
}

TEST(eq_fails) {
    EXPECT_EQ(3, 4);
}

TEST(str_fails) {
    EXPECT_STR(S("got"), S("want"));
}

TEST(no_trap_fails) {
    int x = 0;
    EXPECT_TRAPS(x = 1);
    (void)x;
}

TEST(no_death_fails) {
    EXPECT_DIES((void)0);
}

TEST(stops_at_its_first_failure) {
    EXPECT(0);
    EXPECT(1 == 3);
}

TEST(eq_i_fails) {
    int minus_one = -1;
    EXPECT_EQ_I(minus_one, 2);
}

// A stop of `cls` on `budget` recorded in `ctx` at 3:7, saying `message`:
// what EXPECT_OK_IN must say. A run's context with no arenas, since none of
// these allocates.
static STATUS_MUST_USE Status harness_stop(RunCtx *ctx, StopClass cls, BudgetKind budget, Str message) {
    Pos at = {3, 7};
    Status s = run_fail(ctx, at, cls, message);
    ctx->stop.budget = budget;
    return s;
}

TEST(ok_says_its_stop) {
    RunCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    EXPECT_OK_IN(&ctx, OK);
    EXPECT_OK_IN(&ctx, harness_stop(&ctx, CLASS_TYPE, BUDGET_NONE, S("`+` takes integers")));
}

TEST(ok_says_its_budget) {
    RunCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    EXPECT_OK_IN(&ctx, harness_stop(&ctx, CLASS_BUDGET, BUDGET_READS, S("the store-read budget (count) ran out")));
}

TEST(ok_says_no_stop_was_recorded) {
    RunCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    EXPECT_OK_IN(&ctx, STOP);
}

TEST_MAIN(
    TEST_CASE(passes),
    TEST_CASE(expect_fails),
    TEST_CASE(eq_fails),
    TEST_CASE(str_fails),
    TEST_CASE(no_trap_fails),
    TEST_CASE(no_death_fails),
    TEST_CASE(stops_at_its_first_failure),
    TEST_CASE(eq_i_fails),
    TEST_CASE(ok_says_its_stop),
    TEST_CASE(ok_says_its_budget),
    TEST_CASE(ok_says_no_stop_was_recorded)
)
