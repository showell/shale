// test_host_text.c: the host's text calls, c/host/host_json.c (`json_read`,
// `json_write`) and c/host/host_text.c (`re_lines`, `shale_tokens`), each
// source run as the C `shale` runs it (../ran.c) and called through `host`,
// with no preludes. Every output, stop and step count is the one Zig's
// `shale --verbose` gives the same source.

#define _GNU_SOURCE

#include "test.h"

#include "../ran.c"

static void text_expect_stop(Ran *x, StopClass cls, uint32_t line, uint64_t steps, Str message) {
    EXPECT_EQ(x->ran, STOP);
    EXPECT_EQ(x->r.ctx.stop.cls, cls);
    EXPECT_EQ(x->r.ctx.stop.line, line);
    EXPECT_EQ(x->r.steps, steps);
    EXPECT_STR(x->r.ctx.stop.message, message);
}

// A run of `src` with the file `name` written first, holding `text`.
static void text_run_with(Ran *x, const char *src, const char *name, const char *text) { // cstr: test literals
    RanOpts none = {NULL, false, false};
    ran_init(x, none);
    ran_put(x, name, text);
    ran_run(x, ran_cs(src), cli_budgets());
}

TEST(json_read_reads_each_kind_and_json_write_writes_it_back) {
    static Ran x;
    ran_source(&x, S("local v = host(\"json_read\", \"{\\\"a\\\": [1, -2, 3.5e2, true], \\\"b\\\": null, \\\"s\\\": \\\"\\\\u00e9\\\\n\\\"}\")\n"
                     "print(host(\"json_write\", v), v.a[3])\n"),
               cli_budgets());
    EXPECT_EQ(x.ran, OK);
    // A fraction stays its text, a null member is left out, an escape is
    // decoded to UTF-8 and written back as a byte.
    EXPECT_STR(buf_view(&x.r.out), S("{\"a\":[1,-2,\"3.5e2\",true],\"s\":\"\xc3\xa9\\n\"}\t3.5e2\n"));
    EXPECT_EQ(x.r.steps, 99);
    EXPECT(!x.r.ctx.stopped);
    ran_free(&x);
}

TEST(json_read_refuses_a_malformed_text_naming_its_byte_line_and_column) {
    static Ran x;
    ran_source(&x, S("host(\"json_read\", \"[1,\\n 2,\\n x]\")\n"), cli_budgets());
    text_expect_stop(&x, CLASS_ARGUMENT, 1, 18, S("`json_read`: a value was expected, not a byte (0x78) at byte 9 (line 3, column 2)"));
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(json_read_stands_a_value_in_for_null_and_stops_past_its_depth) {
    static Ran x;
    ran_source(&x, S("print(host(\"json_write\", host(\"json_read\", \"[null, [1]]\", {null = 0, depth = 2})))\nhost(\"json_read\", \"[[[1]]]\", {depth = 2})\n"), cli_budgets());
    EXPECT_STR(buf_view(&x.r.out), S("[0,[1]]\n"));
    text_expect_stop(&x, CLASS_ARGUMENT, 2, 80, S("`json_read`: the text nests past its depth budget at byte 2 (line 1, column 3)"));
    ran_free(&x);
}

TEST(json_write_refuses_a_table_that_holds_itself) {
    static Ran x;
    ran_source(&x, S("local t = {}\nt.self = t\nhost(\"json_write\", t)\n"), cli_budgets());
    text_expect_stop(&x, CLASS_ARGUMENT, 3, 537, S("`json_write`: the value nests past 512 deep (or holds itself)"));
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(re_lines_keeps_the_matching_lines_and_charges_each_search) {
    static Ran x;
    ran_source(&x, S("for m in host(\"re_lines\", \"alpha\\nbeta\\n\\ngamma beta\", \"beta\") do print(m.line, m.text) end\nprint(#host(\"re_lines\", \"a\\nb\", \"a\", {invert = true}))\n"), cli_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("2\tbeta\n4\tgamma beta\n1\n"));
    EXPECT_EQ(x.r.steps, 169);
    ran_free(&x);
}

TEST(re_lines_stops_one_past_the_step_budget) {
    static Ran x;
    Budgets b = cli_budgets();
    b.steps = 60;
    ran_source(&x, S("for m in host(\"re_lines\", \"alpha\\nbeta\\n\\ngamma beta\", \"beta\") do print(m.line, m.text) end\n"), b);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_STEPS);
    EXPECT_EQ(x.r.steps, 61);
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(re_lines_refuses_an_unknown_option_before_any_other) {
    static Ran x;
    ran_source(&x, S("host(\"re_lines\", \"a\", \"a\", {invert = true, foo = 1})\n"), cli_budgets());
    text_expect_stop(&x, CLASS_FAIL, 1, 25, S("`re_lines` takes `invert` and `nocase`, not `foo`"));
    ran_free(&x);
}

TEST(shale_tokens_gives_each_token_comments_among_them_with_its_span) {
    static Ran x;
    text_run_with(&x, "for t in host(\"shale_tokens\", \"t.shale\") do print(t.tag, t.text, t.line, t.start, t.stop) end\nprint(#host(\"shale_tokens\", \"t.shale\", {tag = \"comment\"}))\n",
                  "root/t.shale", "local a = 1 -- x\nprint(a)\n");
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("kw_local\tlocal\t1\t0\t5\nname\ta\t1\t6\t7\nassign\t=\t1\t8\t9\nint\t1\t1\t10\t11\ncomment\t-- x\t1\t12\t16\n"
                                     "name\tprint\t2\t17\t22\nlparen\t(\t2\t22\t23\nname\ta\t2\t23\t24\nrparen\t)\t2\t24\t25\n1\n"));
    EXPECT_EQ(x.r.steps, 586);
    EXPECT_EQ(x.r.reads, 2);
    EXPECT_EQ(x.r.read_bytes, 52);
    ran_free(&x);
}

TEST(shale_tokens_stops_on_a_file_that_does_not_lex_naming_the_lexers_line) {
    static Ran x;
    text_run_with(&x, "host(\"shale_tokens\", \"bad.shale\")\n", "root/bad.shale", "x = 1 / 2\n");
    text_expect_stop(&x, CLASS_ARGUMENT, 1, 31, S("`shale_tokens`: bad.shale does not lex as Shale at line 1: `/` divides into a float, and this language has no floats: use `//` (integer division)"));
    EXPECT_EQ(x.r.reads, 1);
    EXPECT_EQ(x.r.read_bytes, 10);
    ran_free(&x);
}

TEST_MAIN(TEST_CASE(json_read_reads_each_kind_and_json_write_writes_it_back), TEST_CASE(json_read_refuses_a_malformed_text_naming_its_byte_line_and_column),
          TEST_CASE(json_read_stands_a_value_in_for_null_and_stops_past_its_depth), TEST_CASE(json_write_refuses_a_table_that_holds_itself),
          TEST_CASE(re_lines_keeps_the_matching_lines_and_charges_each_search), TEST_CASE(re_lines_stops_one_past_the_step_budget),
          TEST_CASE(re_lines_refuses_an_unknown_option_before_any_other), TEST_CASE(shale_tokens_gives_each_token_comments_among_them_with_its_span),
          TEST_CASE(shale_tokens_stops_on_a_file_that_does_not_lex_naming_the_lexers_line))
