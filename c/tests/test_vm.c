// test_vm.c: the VM (c/script/vm.c), the compiler (compile.h), the run's
// semantics (run.h) and the values (value.h), each source parsed, compiled
// and run as the C `shale` runs it, with no preludes (ran.c). Every output,
// stop and step count is the one Zig's `shale` gives the same source.

// A part of ran_tests.c (`TEST_PART`, test.h), which defines _GNU_SOURCE and
// includes test.h and ran.c before it.


TEST(arithmetic_and_print_take_their_counted_steps) {
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

TEST(maps_and_list_loops_take_their_counted_steps) {
    static Ran x;
    ran_source(&x, S("local m = {}\nm.k = 1\nprint(get(m, \"k\"), has_key(m, \"z\"), count(m))\nfor x in {5, 6} do print(x) end\n"), ran_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("1\tfalse\t1\n5\n6\n"));
    EXPECT_EQ(x.r.steps, 47);
    ran_free(&x);
}

// With no host, `host` stops with class `host`, naming the call, and
// `attempt` catches it like any other stop.
TEST(with_no_host_host_stops_naming_the_call_and_attempt_catches_it) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    x.no_host = true;
    ran_run(&x, S("local x = host(\"run\", {\"zig\", \"build\"}, {})\n"), ran_budgets());
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_HOST);
    EXPECT_STR(x.r.ctx.stop.message, S("`run`: not on this host"));
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
    static Ran y;
    ran_init(&y, o);
    y.no_host = true;
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

// A table of `kind` holding `v` (under `key` for a map), made in the run's
// values arena while it still gives, for a builtin called directly.
static Value vm_table_of(Ran *x, TableKind kind, Str key, Value v) {
    Table *t = NULL;
    Pos at_ = {1, 1};
    if (run_new_table(&x->r, at_, &t) != OK) abort();
    t->kind = kind;
    if (kind == TABLE_MAP && ValueMap_put(&t->map, key, v) != OK) abort();
    if (kind == TABLE_LIST && ValueVec_push(&t->list, v) != OK) abort();
    Value out;
    value_table(&out, t);
    return out;
}

static Value vm_str(const char *lit) { // cstr: a test's literal
    Value v;
    value_string(&v, ran_cs(lit));
    return v;
}

static Value vm_int(int64_t i) {
    Value v;
    value_int(&v, i);
    return v;
}

// Each stop whose Zig format escapes a brace (`{{`), every one in src/, with
// the values' arena refusing everything when the stop's message is made: the
// message is Zig's format text, raw, as `Run.fail`'s `allocPrint(...) catch
// fmt` gives it. The C's cooked text has one brace there, so a stop that
// gave its finished literal would differ from Zig here alone. The builtins
// are called directly, their arguments made first, since a script under a
// tiny arena stops on its arguments before it reaches the stop. The host's
// path_fail sites (`run`, `job_start`, `job_wait`) take Zig's format itself
// and fall back to it, so they cannot drift and are not repeated here.
TEST(a_stop_whose_format_escapes_a_brace_falls_back_to_that_format_raw) {
    static Ran x;
    ran_source(&x, S(""), ran_budgets());
    EXPECT_EQ(x.ran, OK);
    Value edit_map[2] = {vm_str("p"), vm_table_of(&x, TABLE_MAP, S("a"), vm_int(1))};
    Value edit_str[2] = {vm_str("p"), vm_str("x")};
    Value edit_one[1] = {vm_str("p")};
    Value sort_int = vm_table_of(&x, TABLE_LIST, S(""), vm_int(1));
    Value sort_list = vm_table_of(&x, TABLE_LIST, S(""), vm_table_of(&x, TABLE_LIST, S(""), vm_int(1)));
    Value sort_nokey = vm_table_of(&x, TABLE_LIST, S(""), vm_table_of(&x, TABLE_MAP, S("desc"), vm_int(1)));
    Value subst[3] = {vm_str("abc"), vm_str("x"), vm_str("y")};
    Value lines_int = vm_table_of(&x, TABLE_MAP, S("lines"), vm_int(5));
    Value lines_one = vm_table_of(&x, TABLE_MAP, S("lines"), vm_table_of(&x, TABLE_LIST, S(""), vm_int(1)));
    Arena refuses = arena_make(0);
    x.r.ctx.values = &refuses;
    Run *r = &x.r;
    Pos pos = {1, 1};
    EditList edits = {NULL, 0};
    SortKey keys[BUILTIN_SORT_KEYS_MOST];
    size_t n_keys = 0;
    Value out;
    value_nil(&out);
    ConstValues a_map = {edit_map, 2};
    EXPECT_EQ(change_edits_arg(r, pos, a_map, &edits), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`edit`'s second argument is a list of edits, `{{{{old = ..., new = ...}}, ...}}`, not a map"));
    ConstValues a_str = {edit_str, 2};
    EXPECT_EQ(change_edits_arg(r, pos, a_str, &edits), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`edit` of two arguments takes a list of edits, `{{{{old = ..., new = ...}}, ...}}`, not {s}: or `edit(p, old, new)`"));
    ConstValues a_one = {edit_one, 1};
    EXPECT_EQ(change_edits_arg(r, pos, a_one, &edits), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`edit` takes `(p, old, new[, opts])` or `(p, {{edits}})`"));
    EXPECT_EQ(builtin_sort_keys(r, pos, &sort_int, false, keys, &n_keys), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`sort`'s key {d} is {s}: a key is a name or {{key = name, desc = true}}"));
    EXPECT_EQ(builtin_sort_keys(r, pos, &sort_list, false, keys, &n_keys), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`sort`'s key {d} is a list: a key is a name or {{key = name, desc = true}}"));
    EXPECT_EQ(builtin_sort_keys(r, pos, &sort_nokey, false, keys, &n_keys), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`sort`'s key {d} has no `key`: {{key = name, desc = true}}"));
    ConstValues a_subst = {subst, 3};
    EXPECT_EQ(bi_substitute(r, pos, a_subst, &out), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`substitute`: \"{s}\" is not in the text, so nothing was replaced; `{{missing_ok = true}}` if a miss is expected"));
    HostZigRenameOpts o;
    memset(&o, 0, sizeof o);
    EXPECT_EQ(host_zig_lines_opt(r, pos, lines_int.as.table, &o), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`zig_rename`'s `lines` is `{{first, last}}`"));
    EXPECT_EQ(host_zig_lines_opt(r, pos, lines_one.as.table, &o), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`zig_rename`'s `lines` is `{{first, last}}`"));
    EXPECT_EQ(host_zig_names_type(r, pos), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`zig_rename`'s names are `{{from, to}}`"));
    EXPECT_EQ(host_zig_names_list_type(r, pos), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`zig_move`'s names are a list of names, or `{{kind = ..., name = ...}}`"));
    EXPECT_EQ(host_zig_name_type(r, pos), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`zig_move`: a name is a string, or `{{kind = ..., name = ...}}`"));
    EXPECT_EQ(host_zig_renames_list(r, pos), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`zig_renames` takes a map of names, `{{a = \"x.a\"}}`, not a list"));
    EXPECT_EQ(host_zig_renames_none(r, pos), STOP);
    EXPECT_STR(r->ctx.stop.message, S("`zig_renames` of no names: give `{{a = \"x.a\", ...}}`"));
    EXPECT(refuses.hit);
    EXPECT_EQ(refuses.held, 0);
    x.r.ctx.values = &x.values;
    ran_free(&x);
}

TEST(the_compilers_own_checks_end_the_process) {
    Arena a = arena_make(1 << 20);
    Compiler c;
    memset(&c, 0, sizeof c);
    c.arena = &a;
    c.code.ops = VmOpVec_make(&a);
    c.code.enters = VmEnterVec_make(&a);
    Pos p = {1, 1};
    EXPECT(compile_emit(&c, OP_NOP, 0, 0, p) == OK);
    EXPECT(compile_emit(&c, OP_JUMP, 0, 0, p) == OK);
    // A jump takes its target; an op that does not jump is a bug to patch.
    compile_patch(&c, 1, 9);
    EXPECT_EQ(at(c.code.ops, 1).a, 9);
    EXPECT_DIES(compile_patch(&c, 0, 9));
    EXPECT_EQ(at(c.code.ops, 0).a, 0);
    // A function's end with an operand left, and a pop past what was pushed.
    compile_operands_settled(&c);
    compile_push(&c, 1);
    EXPECT_DIES(compile_operands_settled(&c));
    compile_pop(&c, 1);
    compile_operands_settled(&c);
    EXPECT_DIES(compile_pop(&c, 1));
    arena_deinit(&a);
}

TEST(a_store_builtin_on_a_run_with_no_store_ends_the_process) {
    static Run r;
    memset(&r, 0, sizeof r);
    EXPECT(r.store == NULL);
    EXPECT_DIES((void)builtin_store_of(&r));
}

TEST(a_value_of_the_cs_own_enums_past_their_cases_ends_the_process) {
    // Each answer for a value no case names would be a plausible lie: an
    // operator's text "", a table's kind "an empty table", a count's sign.
    EXPECT_STR(run_op_text(BINOP_CONCAT), S(".."));
    EXPECT_DIES((void)run_op_text((BinOp)99));
    int64_t got = 0;
    EXPECT(run_int_arith(BINOP_MOD, -7, 3, &got));
    EXPECT_EQ_I(got, 2);
    EXPECT_DIES((void)run_int_arith(BINOP_CONCAT, 1, 2, &got));
    Value v;
    value_int(&v, 1);
    v.tag = (ValueTag)9;
    EXPECT_DIES((void)value_type_name(&v));
    EXPECT_DIES((void)flat_int((size_t)INT64_MAX + 1));
    EXPECT_EQ_I(flat_int((size_t)INT64_MAX), INT64_MAX);
}

TEST_PART(vm, TEST_CASE(a_value_of_the_cs_own_enums_past_their_cases_ends_the_process), TEST_CASE(the_compilers_own_checks_end_the_process), TEST_CASE(a_store_builtin_on_a_run_with_no_store_ends_the_process), TEST_CASE(arithmetic_and_print_take_their_counted_steps), TEST_CASE(the_step_budget_stops_one_past_it),
          TEST_CASE(the_call_depth_budget_names_every_call), TEST_CASE(attempt_makes_a_stop_a_value_and_the_run_goes_on),
          TEST_CASE(a_budget_is_never_caught), TEST_CASE(a_chain_grows_its_string_in_place),
          TEST_CASE(yield_keeps_what_is_reached_and_charges_the_copy), TEST_CASE(the_output_budget_writes_what_fits),
          TEST_CASE(division_floors_and_an_overflow_stops), TEST_CASE(an_index_past_the_append_names_the_one_that_appends),
          TEST_CASE(maps_and_list_loops_take_their_counted_steps), TEST_CASE(with_no_host_host_stops_naming_the_call_and_attempt_catches_it),
          TEST_CASE(values_name_their_types_and_truth), TEST_CASE(integer_text_and_checked_arithmetic),
          TEST_CASE(the_depth_budget_stops_a_deep_expression), TEST_CASE(a_stop_whose_format_escapes_a_brace_falls_back_to_that_format_raw));
