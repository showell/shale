// err.h: the class and budget names, every budget stop's message and its
// fallback, and a stop's report with its call chain, each against Zig
// Shale's text.

#include "test.h"

#include "err.h"

TEST(every_class_has_its_name_and_the_count_traps) {
    EXPECT_STR(stop_class_name(CLASS_SYNTAX), S("syntax"));
    EXPECT_STR(stop_class_name(CLASS_REFUSED), S("refused"));
    EXPECT_STR(stop_class_name(CLASS_NAME), S("name"));
    EXPECT_STR(stop_class_name(CLASS_TYPE), S("type"));
    EXPECT_STR(stop_class_name(CLASS_INDEX), S("index"));
    EXPECT_STR(stop_class_name(CLASS_ARITHMETIC), S("arithmetic"));
    EXPECT_STR(stop_class_name(CLASS_ARGUMENT), S("argument"));
    EXPECT_STR(stop_class_name(CLASS_BUDGET), S("budget"));
    EXPECT_STR(stop_class_name(CLASS_STORE), S("store"));
    EXPECT_STR(stop_class_name(CLASS_FAIL), S("fail"));
    EXPECT_STR(stop_class_name(CLASS_HOST), S("host"));
    EXPECT_TRAPS((void)stop_class_name(CLASS_COUNT));
}

TEST(every_budget_has_its_name_and_the_count_traps) {
    EXPECT_STR(budget_name(BUDGET_NONE), S(""));
    EXPECT_STR(budget_name(BUDGET_STEPS), S("steps"));
    EXPECT_STR(budget_name(BUDGET_ARENA), S("arena"));
    EXPECT_STR(budget_name(BUDGET_CALL_DEPTH), S("call_depth"));
    EXPECT_STR(budget_name(BUDGET_OUTPUT), S("output"));
    EXPECT_STR(budget_name(BUDGET_READS), S("reads"));
    EXPECT_STR(budget_name(BUDGET_READ_BYTES), S("read_bytes"));
    EXPECT_STR(budget_name(BUDGET_CHANGES), S("changes"));
    EXPECT_STR(budget_name(BUDGET_CHANGE_BYTES), S("change_bytes"));
    EXPECT_STR(budget_name(BUDGET_SOURCE), S("source"));
    EXPECT_STR(budget_name(BUDGET_NESTING), S("nesting"));
    EXPECT_STR(budget_name(BUDGET_DEPTH), S("depth"));
    EXPECT_STR(budget_name(BUDGET_KEPT), S("kept"));
    EXPECT_STR(budget_name(BUDGET_MACHINE), S("machine"));
    EXPECT_STR(budget_name(BUDGET_PROGRAMS), S("programs"));
    EXPECT_STR(budget_name(BUDGET_CPU), S("cpu"));
    EXPECT_TRAPS((void)budget_name(BUDGET_COUNT));
}

TEST(every_budget_has_its_default) {
    Budgets b = budgets_default();
    EXPECT_EQ(b.steps, 5000000);
    EXPECT_EQ(b.arena_bytes, 16777216);
    EXPECT_EQ(b.call_depth, 64);
    EXPECT_EQ(b.programs, 1000);
    EXPECT_EQ(b.output_bytes, 262144);
    EXPECT_EQ(b.reads, 10000);
    EXPECT_EQ(b.read_bytes, 67108864);
    EXPECT_EQ(b.changes, 1000);
    EXPECT_EQ(b.change_bytes, 16777216);
    EXPECT_EQ(b.source_bytes, 65536);
    EXPECT_EQ(b.nesting, 200);
    EXPECT_EQ(b.depth, 2000);
    EXPECT_EQ(b.kept_bytes, 1048576);
    EXPECT_EQ(b.machine_bytes, 16777216);
    EXPECT_EQ(b.call_bytes, 1048576);
    EXPECT_EQ(b.call_entries, 256);
}

static Str message_of(BudgetKind k, Arena *values) {
    Arena machine = arena_make(1 << 20);
    RunCtx r = run_ctx_make(values, &machine, budgets_default());
    Pos at = {3, 9};
    EXPECT(run_budget_stop(&r, at, k) == STOP);
    EXPECT(r.stopped);
    EXPECT_EQ(r.stop.cls, CLASS_BUDGET);
    EXPECT_EQ(r.stop.budget, k);
    EXPECT_EQ(r.stop.line, 3);
    EXPECT_EQ(r.stop.column, 9);
    arena_deinit(&machine);
    return r.stop.message;
}

TEST(every_budget_stop_says_its_message) {
    Arena v = arena_make(1 << 20);
    EXPECT_STR(message_of(BUDGET_STEPS, &v), S("the step budget (5000000 steps) ran out"));
    EXPECT_STR(message_of(BUDGET_ARENA, &v), S("the arena budget ran out"));
    EXPECT_STR(message_of(BUDGET_CALL_DEPTH, &v), S("calls nest deeper than the call-depth budget (64)"));
    EXPECT_STR(message_of(BUDGET_OUTPUT, &v), S("the output budget ran out"));
    EXPECT_STR(message_of(BUDGET_READS, &v), S("the store-read budget (count) ran out"));
    EXPECT_STR(message_of(BUDGET_READ_BYTES, &v), S("the store-read budget (bytes) ran out"));
    EXPECT_STR(message_of(BUDGET_CHANGES, &v), S("the store-change budget (count) ran out"));
    EXPECT_STR(message_of(BUDGET_CHANGE_BYTES, &v), S("the store-change budget (bytes) ran out"));
    EXPECT_STR(message_of(BUDGET_SOURCE, &v), S("the source is larger than the source budget"));
    EXPECT_STR(message_of(BUDGET_NESTING, &v), S("the source nests deeper than the nesting budget"));
    EXPECT_STR(message_of(BUDGET_DEPTH, &v), S("calls and the expressions inside them nest deeper than the depth budget"));
    EXPECT_STR(message_of(BUDGET_MACHINE, &v), S("the machine's memory (the code, the frames, the operands) is past its budget (16777216 bytes): raise it with `--budget machine_bytes=N` or the script's `-- budget: machine_bytes=N` line, up to 1073741824"));
    EXPECT_STR(message_of(BUDGET_KEPT, &v), S("what the run keeps at a `yield` is past the kept budget (1048576 bytes): raise it with `--budget kept_bytes=N` or the script's `-- budget: kept_bytes=N` line, up to 268435456"));
    EXPECT_STR(message_of(BUDGET_PROGRAMS, &v), S("1000 programs a run, the `programs` budget: raise it with `--budget programs=N` or the script's `-- budget: programs=N` line"));
    EXPECT_STR(message_of(BUDGET_CPU, &v), S("a program's CPU time is past its budget"));
    arena_deinit(&v);
}

TEST(a_message_the_arena_refuses_falls_back_to_a_plain_one) {
    Arena full = arena_make(0);
    EXPECT_STR(message_of(BUDGET_STEPS, &full), S("the step budget ran out"));
    EXPECT_STR(message_of(BUDGET_CALL_DEPTH, &full), S("calls nest deeper than the call-depth budget"));
    EXPECT_STR(message_of(BUDGET_MACHINE, &full), S("the machine's memory is past its budget"));
    EXPECT_STR(message_of(BUDGET_KEPT, &full), S("what the run keeps at a `yield` is past the kept budget"));
    EXPECT_STR(message_of(BUDGET_PROGRAMS, &full), S("the run's programs are past their budget"));
    EXPECT_STR(message_of(BUDGET_ARENA, &full), S("the arena budget ran out"));
    arena_deinit(&full);
}

TEST(a_message_quotes_the_budget_set_not_the_default) {
    Arena v = arena_make(1 << 20);
    Arena machine = arena_make(1 << 20);
    Budgets b = budgets_default();
    b.steps = 900;
    RunCtx r = run_ctx_make(&v, &machine, b);
    Pos at = {1, 1};
    EXPECT(run_budget_stop(&r, at, BUDGET_STEPS) == STOP);
    EXPECT_STR(r.stop.message, S("the step budget (900 steps) ran out"));
    arena_deinit(&v);
    arena_deinit(&machine);
}

TEST(a_stop_on_no_budget_ends_the_process) {
    Arena v = arena_make(1 << 20);
    Arena machine = arena_make(1 << 20);
    RunCtx r = run_ctx_make(&v, &machine, budgets_default());
    Pos at = {1, 1};
    EXPECT_DIES(EXPECT(run_budget_stop(&r, at, BUDGET_NONE) == STOP));
    arena_deinit(&v);
    arena_deinit(&machine);
}

// An arena's refusal, as a container's STOP leaves one: what run_oom is
// held to there being.
static void refuse_once(void) {
    Arena none = arena_make(0);
    EXPECT(arena_alloc(&none, 1, 1) == NULL);
    EXPECT(none.hit);
    EXPECT(*arena_refused_slot());
}

TEST(run_oom_is_the_arena_budget_with_its_function_and_trail) {
    Arena v = arena_make(1 << 20);
    Arena machine = arena_make(1 << 20);
    RunCtx r = run_ctx_make(&v, &machine, budgets_default());
    Call outer = {2, 1, S("")};
    Call inner = {1, 21, S("f")};
    EXPECT(CallVec_push(&r.calls, outer) == OK);
    EXPECT(CallVec_push(&r.calls, inner) == OK);
    r.function = S("g");
    Pos at = {7, 4};
    refuse_once();
    EXPECT(run_oom(&r, at) == STOP);
    EXPECT(!*arena_refused_slot());
    EXPECT_EQ(r.stop.cls, CLASS_BUDGET);
    EXPECT_EQ(r.stop.budget, BUDGET_ARENA);
    EXPECT_STR(r.stop.message, S("the arena budget ran out"));
    EXPECT_STR(r.stop.function, S("g"));
    EXPECT_EQ(r.stop.trail.len, 2);
    EXPECT_STR(at(r.stop.trail, 1).function, S("f"));
    arena_deinit(&v);
    arena_deinit(&machine);
}

TEST(run_fail_keeps_its_class_and_has_no_budget) {
    Arena v = arena_make(1 << 20);
    Arena machine = arena_make(1 << 20);
    RunCtx r = run_ctx_make(&v, &machine, budgets_default());
    Pos at = {5, 6};
    EXPECT(run_fail(&r, at, CLASS_INDEX, S("no key `x` in the map")) == STOP);
    EXPECT_EQ(r.stop.cls, CLASS_INDEX);
    EXPECT_EQ(r.stop.budget, BUDGET_NONE);
    EXPECT_STR(r.stop.function, S(""));
    EXPECT_EQ(r.stop.trail.len, 0);
    Buf b = buf_make(&v);
    Status built = buf_lit(&b, "built");
    EXPECT(run_fail_built(&r, at, CLASS_TYPE, &b, built, S("fallback")) == STOP);
    EXPECT_STR(r.stop.message, S("built"));
    EXPECT_EQ(r.stop.cls, CLASS_TYPE);
    EXPECT(run_fail_built(&r, at, CLASS_TYPE, &b, STOP, S("fallback")) == STOP);
    EXPECT_STR(r.stop.message, S("fallback"));
    arena_deinit(&v);
    arena_deinit(&machine);
}

static Str report_of(Arena *a, Str name, const Stop *s, uint64_t steps) {
    Buf w = buf_make(a);
    EXPECT(stop_report(&w, name, s, steps) == OK);
    return buf_view(&w);
}

TEST(a_report_names_the_place_the_class_the_budget_and_the_steps) {
    Arena a = arena_make(1 << 20);
    Calls none = {NULL, 0};
    Stop top = {2, 12, CLASS_BUDGET, BUDGET_ARENA, S("the arena budget ran out"), S(""), none};
    EXPECT_STR(report_of(&a, S("stdin"), &top, 1088860),
               S("── stopped at stdin:2:12: budget arena: the arena budget ran out (1088860 steps)\n"));
    Stop typed = {1, 5, CLASS_TYPE, BUDGET_NONE, S("`+` takes integers"), S(""), none};
    EXPECT_STR(report_of(&a, S("a.shale"), &typed, 3),
               S("── stopped at a.shale:1:5: type: `+` takes integers (3 steps)\n"));
    arena_deinit(&a);
}

TEST(a_report_gives_the_calls_innermost_first) {
    // A nested call: g stopped, called from f, called from the top.
    Arena a = arena_make(1 << 20);
    Call calls[2] = {{2, 1, S("")}, {1, 21, S("f")}};
    Calls trail = {calls, 2};
    Stop s = {1, 30, CLASS_FAIL, BUDGET_NONE, S("no"), S("g"), trail};
    EXPECT_STR(report_of(&a, S("seq 7"), &s, 12),
               S("── stopped at seq 7:1:30 (in `g`): fail: no (12 steps)\n"
                 "   called from seq 7:1:21 (in `f`)\n"
                 "   called from seq 7:2:1\n"));
    arena_deinit(&a);
}

TEST(a_report_names_a_line_in_the_prelude_as_the_preludes) {
    Arena a = arena_make(1 << 20);
    Call calls[1] = {{4, 2, S("")}};
    Calls trail = {calls, 1};
    Stop s = {11, 10, CLASS_HOST, BUDGET_NONE, S("not found"), S("the prelude's run"), trail};
    EXPECT_STR(report_of(&a, S("stdin"), &s, 418),
               S("── stopped at the prelude:11:10 (in `the prelude's run`): host: not found (418 steps)\n"
                 "   called from stdin:4:2\n"));
    arena_deinit(&a);
}

TEST(a_report_the_arena_refuses_says_so) {
    Arena full = arena_make(0);
    Calls none = {NULL, 0};
    Stop s = {1, 1, CLASS_FAIL, BUDGET_NONE, S("x"), S(""), none};
    Buf w = buf_make(&full);
    EXPECT(stop_report(&w, S("stdin"), &s, 0) == STOP);
    EXPECT(full.hit);
    arena_deinit(&full);
}

TEST(a_stop_with_no_stop_recorded_ends_the_process) {
    // A container's STOP passed up without run_oom would end the run with a
    // zeroed Stop that names no budget.
    Arena values = arena_make(1 << 20);
    Arena machine = arena_make(1 << 20);
    RunCtx r = run_ctx_make(&values, &machine, budgets_default());
    EXPECT(run_ended(&r, OK) == OK);
    EXPECT_DIES(EXPECT(run_ended(&r, STOP) == STOP));
    EXPECT(!r.stopped);
    Pos at_one = {1, 2};
    refuse_once();
    EXPECT(run_oom(&r, at_one) == STOP);
    EXPECT(run_ended(&r, STOP) == STOP);
    EXPECT_EQ(r.stop.budget, BUDGET_ARENA);
    arena_deinit(&values);
    arena_deinit(&machine);
}

TEST(a_stop_read_as_a_refusal_no_arena_made_ends_the_process) {
    // A STOP that no arena refused, read as a budget, would say the budget
    // ran out when a bug in the C stopped the run.
    Arena values = arena_make(1 << 20);
    Arena machine = arena_make(1 << 20);
    RunCtx r = run_ctx_make(&values, &machine, budgets_default());
    Pos at = {1, 2};
    *arena_refused_slot() = false;
    EXPECT_DIES(EXPECT(run_oom(&r, at) == STOP));
    EXPECT_DIES(EXPECT(run_refused_stop(&r, at, BUDGET_MACHINE) == STOP));
    EXPECT(!r.stopped);
    // One refusal is read once: a second STOP after it has none behind it.
    refuse_once();
    EXPECT(run_oom(&r, at) == STOP);
    r.stopped = false;
    EXPECT_DIES(EXPECT(run_oom(&r, at) == STOP));
    arena_deinit(&values);
    arena_deinit(&machine);
}

TEST(a_refusal_read_over_a_recorded_stop_ends_the_process) {
    // A refusal read as the arena budget over a stop already recorded would
    // write the budget over the stop the run had.
    Arena values = arena_make(1 << 20);
    Arena machine = arena_make(1 << 20);
    RunCtx r = run_ctx_make(&values, &machine, budgets_default());
    Pos at = {3, 4};
    EXPECT(run_fail(&r, at, CLASS_INDEX, S("no key")) == STOP);
    refuse_once();
    EXPECT_DIES(EXPECT(run_oom(&r, at) == STOP));
    EXPECT_EQ(r.stop.cls, CLASS_INDEX);
    *arena_refused_slot() = false;
    arena_deinit(&values);
    arena_deinit(&machine);
}

TEST(a_stop_whose_class_and_budget_disagree_ends_the_process) {
    Arena values = arena_make(1 << 20);
    Arena machine = arena_make(1 << 20);
    RunCtx r = run_ctx_make(&values, &machine, budgets_default());
    Pos at = {2, 3};
    EXPECT(run_budget_stop(&r, at, BUDGET_STEPS) == STOP);
    EXPECT(run_ended(&r, STOP) == STOP);
    // A budget's class with no budget, and a budget on another class.
    r.stop.budget = BUDGET_NONE;
    EXPECT_DIES(EXPECT(run_ended(&r, STOP) == STOP));
    EXPECT(run_fail(&r, at, CLASS_TYPE, S("not an integer")) == STOP);
    EXPECT(run_ended(&r, STOP) == STOP);
    r.stop.budget = BUDGET_READS;
    EXPECT_DIES(EXPECT(run_ended(&r, STOP) == STOP));
    EXPECT_DIES(stop_check_budget(&r.stop));
    r.stop.budget = BUDGET_NONE;
    stop_check_budget(&r.stop);
    arena_deinit(&values);
    arena_deinit(&machine);
}

TEST_MAIN(
    TEST_CASE(every_class_has_its_name_and_the_count_traps),
    TEST_CASE(every_budget_has_its_name_and_the_count_traps),
    TEST_CASE(every_budget_has_its_default),
    TEST_CASE(every_budget_stop_says_its_message),
    TEST_CASE(a_message_the_arena_refuses_falls_back_to_a_plain_one),
    TEST_CASE(a_message_quotes_the_budget_set_not_the_default),
    TEST_CASE(a_stop_on_no_budget_ends_the_process),
    TEST_CASE(run_oom_is_the_arena_budget_with_its_function_and_trail),
    TEST_CASE(run_fail_keeps_its_class_and_has_no_budget),
    TEST_CASE(a_report_names_the_place_the_class_the_budget_and_the_steps),
    TEST_CASE(a_report_gives_the_calls_innermost_first),
    TEST_CASE(a_report_names_a_line_in_the_prelude_as_the_preludes),
    TEST_CASE(a_report_the_arena_refuses_says_so),
    TEST_CASE(a_stop_with_no_stop_recorded_ends_the_process),
    TEST_CASE(a_stop_read_as_a_refusal_no_arena_made_ends_the_process),
    TEST_CASE(a_refusal_read_over_a_recorded_stop_ends_the_process),
    TEST_CASE(a_stop_whose_class_and_budget_disagree_ends_the_process)
)
