// test_cli.c: the command line's budgets: a value past its field's type
// saturates, never wraps.
// The rest of cli.h's reading is in test_parse.c, beside the parser it needs.

#include "test.h"

#include "../script/cli.h"

TEST(a_budget_past_its_fields_type_saturates_and_never_wraps) {
    // Past 2^32: on a 32-bit size_t (the wasm build; `zig cc -target
    // x86-linux-musl` builds this file so) a cast would wrap it to 1, where it
    // must be SIZE_MAX. On x86-64 it fits a size_t whole.
    uint64_t past = ((uint64_t)1 << 32) + 1;
#if SIZE_MAX == UINT64_MAX
    size_t want = (size_t)past;
#else
    size_t want = SIZE_MAX;
#endif
    Budgets b;
    cli_defaults(&b);
    cli_set_budget(&b, SET_OUTPUT_BYTES, past);
    cli_set_budget(&b, SET_ARENA_BYTES, past);
    cli_set_budget(&b, SET_KEPT_BYTES, past);
    cli_set_budget(&b, SET_MACHINE_BYTES, past);
    EXPECT_EQ(b.output_bytes, want);
    EXPECT_EQ(b.arena_bytes, want);
    EXPECT_EQ(b.kept_bytes, want);
    EXPECT_EQ(b.machine_bytes, want);
    // A 32-bit field saturates at its own most on every target.
    cli_set_budget(&b, SET_CALL_DEPTH, past);
    cli_set_budget(&b, SET_PROGRAMS, past);
    EXPECT_EQ(b.call_depth, UINT32_MAX);
    EXPECT_EQ(b.programs, UINT32_MAX);
    // A 64-bit field takes it whole, and the budgets not named are left.
    cli_set_budget(&b, SET_STEPS, past);
    EXPECT_EQ(b.steps, past);
    EXPECT_EQ(b.reads, 10000);
    EXPECT_EQ(b.call_bytes, 64 * 1024 * 1024);
}

TEST(a_size_saturates_only_past_size_max) {
    EXPECT_EQ(cli_size_saturated(0), 0);
    EXPECT_EQ(cli_size_saturated(5), 5);
    EXPECT_EQ(cli_size_saturated(SIZE_MAX), SIZE_MAX);
    EXPECT_EQ(cli_size_saturated(UINT64_MAX), SIZE_MAX);
}

TEST(a_budget_of_no_settable_kind_ends_the_process) {
    Budgets b = budgets_default();
    cli_set_budget(&b, SET_STEPS, 9);
    EXPECT_EQ(cli_budget_value(&b, SET_STEPS), 9);
    EXPECT_DIES(cli_set_budget(&b, SET_COUNT, 9));
    EXPECT_DIES((void)cli_budget_value(&b, SET_COUNT));
    EXPECT_EQ(b.steps, 9);
}

TEST_MAIN(TEST_CASE(a_budget_past_its_fields_type_saturates_and_never_wraps),
          TEST_CASE(a_size_saturates_only_past_size_max),
          TEST_CASE(a_budget_of_no_settable_kind_ends_the_process))
