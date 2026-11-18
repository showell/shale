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

// `f(w, n)`'s text, from a buffer of its own.
static Str steps_text(Arena *a, Status (*f)(Buf *, uint64_t), uint64_t n) {
    Buf w = buf_make(a);
    EXPECT(f(&w, n) == OK);
    return buf_view(&w);
}

TEST(the_steps_line_says_its_counts_as_the_time_line_says_seconds) {
    Arena a;
    bool refused = false;
    arena_init(&a, 1 << 16, &refused);
    // Whole below a thousand, then a tenth of the unit, cut short.
    EXPECT_STR(steps_text(&a, cli_steps_amount, 0), S("0"));
    EXPECT_STR(steps_text(&a, cli_steps_amount, 999), S("999"));
    EXPECT_STR(steps_text(&a, cli_steps_amount, 1000), S("1.0K"));
    EXPECT_STR(steps_text(&a, cli_steps_amount, 52374), S("52.3K"));
    EXPECT_STR(steps_text(&a, cli_steps_amount, 999999), S("999.9K"));
    EXPECT_STR(steps_text(&a, cli_steps_amount, 1000000), S("1.0M"));
    EXPECT_STR(steps_text(&a, cli_steps_amount, 77349012), S("77.3M"));
    EXPECT_STR(steps_text(&a, cli_steps_amount, 2000000000), S("2.0G"));
    EXPECT_STR(steps_text(&a, cli_steps_amount, UINT64_MAX), S("18446744073.7G"));
    // Calls in threes, the zeros inside a group kept.
    EXPECT_STR(steps_text(&a, cli_grouped, 7), S("7"));
    EXPECT_STR(steps_text(&a, cli_grouped, 2310), S("2,310"));
    EXPECT_STR(steps_text(&a, cli_grouped, 1002003), S("1,002,003"));
    EXPECT_STR(steps_text(&a, cli_grouped, 1000000), S("1,000,000"));
    // A share is cut short, never past 100, and none of nothing.
    EXPECT_EQ(cli_share(41, 77), 53);
    EXPECT_EQ(cli_share(5, 0), 0);
    EXPECT_EQ(cli_share(UINT64_MAX, UINT64_MAX), 100);
    // The line: the most first, a call or calls, and the script's own code
    // with none.
    StepsTop t = {77349012, {{S("re_find_all"), 41200000, 2310, true}, {S("lines"), 12000000, 1, true}, {S("script"), 10100000, 0, false}}, 3};
    Buf w = buf_make(&a);
    EXPECT(cli_write_steps(&w, &t) == OK);
    EXPECT_STR(buf_view(&w), S("── steps: 77.3M in all; the most 41.2M (53%): re_find_all, 2,310 calls; 12.0M (15%): lines, 1 call; 10.1M (13%): script\n"));
    // A run that spent nothing names nothing.
    StepsTop none;
    memset(&none, 0, sizeof none);
    buf_clear(&w);
    EXPECT(cli_write_steps(&w, &none) == OK);
    EXPECT_STR(buf_view(&w), S("── steps: 0 in all\n"));
    arena_deinit(&a);
}

TEST_MAIN(TEST_CASE(a_budget_past_its_fields_type_saturates_and_never_wraps),
          TEST_CASE(a_size_saturates_only_past_size_max),
          TEST_CASE(a_budget_of_no_settable_kind_ends_the_process),
          TEST_CASE(the_steps_line_says_its_counts_as_the_time_line_says_seconds))
