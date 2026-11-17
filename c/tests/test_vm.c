// test_vm.c: the VM (c/script/vm.c), the compiler (compile.h), the run's
// semantics (run.h) and the values (value.h), each source parsed, compiled
// and run as the C `shale` runs it, with no preludes (ran.c). Every output, stop and
// step count is the one Zig's `shale` gives the same source
// (c/out/dev/zig_counts.shale printed them).

// A part of ran_tests.c (`TEST_PART`, test.h), which defines _GNU_SOURCE and
// includes test.h and ran.c before it.


TEST(arithmetic_and_print_take_zigs_steps) {
    static Ran x;
    ran_source(&x, S("local x = 1 + 2 * 3\nprint(x, \"a\" .. x)\n"), ran_budgets());
    EXPECT(x.parsed);
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("7\ta7\n"));
    EXPECT_EQ(x.r.steps, 19);
    EXPECT(!x.r.ctx.stopped);
    ran_free(&x);
}

TEST(the_step_budget_stops_one_past_it) {
    static Ran x;
    Budgets b = ran_budgets();
    b.steps = 100;
    ran_source(&x, S("while true do end\n"), b);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.steps, 101);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_BUDGET);
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_STEPS);
    EXPECT_EQ(x.r.ctx.stop.line, 1);
    EXPECT_EQ(x.r.ctx.stop.column, 7);
    EXPECT_STR(x.r.ctx.stop.message, S("the step budget (100 steps) ran out"));
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(the_call_depth_budget_names_every_call) {
    static Ran x;
    ran_source(&x, S("function f(n) return f(n + 1) end\nf(1)\n"), ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.steps, 321);
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_CALL_DEPTH);
    EXPECT_STR(x.r.ctx.stop.message, S("calls nest deeper than the call-depth budget (64)"));
    EXPECT_STR(x.r.ctx.stop.function, S("f"));
    EXPECT_EQ(x.r.ctx.stop.trail.len, 64);
    EXPECT_EQ(at(x.r.ctx.stop.trail, 0).line, 2);
    EXPECT_STR(at(x.r.ctx.stop.trail, 0).function, S(""));
    EXPECT_STR(at(x.r.ctx.stop.trail, 63).function, S("f"));
    ran_free(&x);
}

TEST(attempt_makes_a_stop_a_value_and_the_run_goes_on) {
    static Ran x;
    ran_source(&x, S("function g() fail(\"no\") end\nlocal r = attempt(g)\nprint(r.ok, r.error, r.class, r.line, #r.trail)\n"), ran_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("false\tno\tfail\t1\t1\n"));
    // The attempt's frame is gone: no call is left in progress.
    EXPECT_EQ(x.r.ctx.calls.len, 0);
    EXPECT_EQ(x.r.depth, 0);
    ran_free(&x);
}

TEST(a_budget_is_never_caught) {
    static Ran x;
    Budgets b = ran_budgets();
    b.steps = 50;
    ran_source(&x, S("function spin() while true do end end\nlocal r = attempt(spin)\nprint(\"after\")\n"), b);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_STEPS);
    EXPECT_STR(x.r.ctx.stop.function, S("spin"));
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(a_chain_grows_its_string_in_place) {
    static Ran x;
    ran_source(&x, S("local s = \"\"\nfor i = 1, 100 do s = s .. \"ab\" end\nprint(#s)\n"), ran_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("200\n"));
    EXPECT_EQ(x.r.steps, 911);
    // The last chain's buffer holds the whole string, with room to spare.
    EXPECT_EQ(x.r.tail_used, 200);
    EXPECT(x.r.tail_cap >= 200);
    EXPECT(x.r.tail_cap < 1024);
    ran_free(&x);
}

TEST(yield_keeps_what_is_reached_and_charges_the_copy) {
    static Ran x;
    ran_source(&x, S("local keep = {1, {a = \"xy\"}}\nlocal drop = {}\nfor i = 1, 10 do push(drop, i) end\nyield()\nprint(keep[2].a, #drop)\n"), ran_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("xy\t10\n"));
    EXPECT_EQ(x.r.steps, 99);
    EXPECT_EQ(x.r.collections, 1);
    // The chain's tail was the old arena's, so it is let go.
    EXPECT(x.r.tail_ptr == NULL);
    ran_free(&x);
}

TEST(the_output_budget_writes_what_fits) {
    static Ran x;
    Budgets b = ran_budgets();
    b.output_bytes = 5;
    ran_source(&x, S("print(\"abcdefgh\")\n"), b);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_STR(buf_view(&x.r.out), S("abcde"));
    EXPECT_EQ(x.r.steps, 11);
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_OUTPUT);
    ran_free(&x);
}

TEST(division_floors_and_an_overflow_stops) {
    static Ran x;
    static Arena a;
    a = arena_make(1 << 20);
    ran_source(&x, S("print(7 // -2, -7 % 3, 7 % -3)\nprint(9223372036854775807 + 1)\n"), ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_STR(buf_view(&x.r.out), S("-4\t2\t-2\n"));
    EXPECT_STR(ran_report(&x, &a), S("── stopped at stdin:2:27: arithmetic: 9223372036854775807 + 1 overflows 64 bits (24 steps)\n── used: 24 of 5000000 steps, 0 of 10000 reads, 0 of 67108864 read bytes, 8 of 65536 output bytes\n"));
    arena_deinit(&a);
    ran_free(&x);
}

TEST(an_index_past_the_append_names_the_one_that_appends) {
    static Ran x;
    ran_source(&x, S("local l = {1}\nl[3] = 2\n"), ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.steps, 7);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_INDEX);
    EXPECT_EQ(x.r.ctx.stop.line, 2);
    EXPECT_EQ(x.r.ctx.stop.column, 2);
    EXPECT_STR(x.r.ctx.stop.message, S("index 3 is outside the list (length 1): only index 2 appends, and `push(l, x)` says so"));
    ran_free(&x);
}

TEST(maps_and_list_loops_take_zigs_steps) {
    static Ran x;
    ran_source(&x, S("local m = {}\nm.k = 1\nprint(get(m, \"k\"), has_key(m, \"z\"), count(m))\nfor x in {5, 6} do print(x) end\n"), ran_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("1\tfalse\t1\n5\n6\n"));
    EXPECT_EQ(x.r.steps, 47);
    ran_free(&x);
}

// Zig's host_test.zig, "seam: with no host": `host` stops with class
// `host`, naming the call, and `attempt` catches it like any other stop.
TEST(with_no_host_host_stops_naming_the_call_and_attempt_catches_it) {
    static Ran x;
    RanOpts metal = {NULL, false, false, true};
    ran_init(&x, metal);
    ran_run(&x, S("local x = host(\"run\", {\"zig\", \"build\"}, {})\n"), ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_HOST);
    EXPECT_STR(x.r.ctx.stop.message, S("`run`: not on this host"));
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
    static Ran y;
    ran_init(&y, metal);
    ran_run(&y, S("function f() return host(\"real\", \"runs:x\") end local r = attempt(f) print(r.ok, r.class)\n"), ran_budgets());
    EXPECT_EQ(y.ran, OK);
    EXPECT_STR(buf_view(&y.r.out), S("false\thost\n"));
    ran_free(&y);
}

TEST(values_name_their_types_and_truth) {
    Value v;
    value_nil(&v);
    EXPECT_STR(value_type_name(&v), S("nil"));
    EXPECT(!value_truthy(&v));
    value_bool(&v, false);
    EXPECT_STR(value_type_name(&v), S("a boolean"));
    EXPECT(!value_truthy(&v));
    value_int(&v, 0);
    EXPECT_STR(value_type_name(&v), S("an integer"));
    EXPECT(value_truthy(&v));
    value_string(&v, S(""));
    EXPECT_STR(value_type_name(&v), S("a string"));
    EXPECT(value_truthy(&v));
    Table t;
    memset(&t, 0, sizeof t);
    value_table(&v, &t);
    EXPECT_STR(value_type_name(&v), S("an empty table"));
    t.kind = TABLE_LIST;
    EXPECT_STR(value_type_name(&v), S("a list"));
    t.kind = TABLE_MAP;
    EXPECT_STR(value_type_name(&v), S("a map"));
}

TEST(integer_text_and_checked_arithmetic) {
    IntText t;
    EXPECT_STR(int_text(&t, 0), S("0"));
    EXPECT_STR(int_text(&t, -42), S("-42"));
    EXPECT_STR(int_text(&t, INT64_MIN), S("-9223372036854775808"));
    EXPECT_STR(int_text(&t, INT64_MAX), S("9223372036854775807"));
    int64_t got = 7;
    EXPECT(!int_add(INT64_MAX, 1, &got));
    EXPECT(!int_sub(INT64_MIN, 1, &got));
    EXPECT(!int_mul(INT64_MIN, -1, &got));
    EXPECT(!int_mul(3037000500, 3037000500, &got));
    EXPECT_EQ(got, 7);
    EXPECT(int_mul(-3037000499, 3037000499, &got));
    EXPECT_EQ((uint64_t)got, (uint64_t)(-3037000499LL * 3037000499LL));
    EXPECT(int_add(INT64_MIN, INT64_MAX, &got));
    EXPECT_EQ((uint64_t)got, (uint64_t)-1);
}

TEST(the_depth_budget_stops_a_deep_expression) {
    static Ran x;
    Budgets b = ran_budgets();
    b.depth = 10;
    ran_source(&x, S("print(abs(abs(abs(abs(abs(abs(abs(abs(abs(abs(abs(abs(1)))))))))))))\n"), b);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_DEPTH);
    EXPECT_STR(x.r.ctx.stop.message, S("calls and the expressions inside them nest deeper than the depth budget"));
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST_PART(vm, TEST_CASE(arithmetic_and_print_take_zigs_steps), TEST_CASE(the_step_budget_stops_one_past_it),
          TEST_CASE(the_call_depth_budget_names_every_call), TEST_CASE(attempt_makes_a_stop_a_value_and_the_run_goes_on),
          TEST_CASE(a_budget_is_never_caught), TEST_CASE(a_chain_grows_its_string_in_place),
          TEST_CASE(yield_keeps_what_is_reached_and_charges_the_copy), TEST_CASE(the_output_budget_writes_what_fits),
          TEST_CASE(division_floors_and_an_overflow_stops), TEST_CASE(an_index_past_the_append_names_the_one_that_appends),
          TEST_CASE(maps_and_list_loops_take_zigs_steps), TEST_CASE(with_no_host_host_stops_naming_the_call_and_attempt_catches_it),
          TEST_CASE(values_name_their_types_and_truth), TEST_CASE(integer_text_and_checked_arithmetic),
          TEST_CASE(the_depth_budget_stops_a_deep_expression));
