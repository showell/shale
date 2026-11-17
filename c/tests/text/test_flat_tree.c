// test_flat_tree.c: the host's reading of roc-ast's and go-ast's `--flat`
// output, c/host/flat_tree.c (`host("roc_tree", out, ending)` and
// `host("go_tree", out, ending)`), each source run as the C `shale` runs it
// (../ran.c) and called through `host`, with no preludes. Every output, stop
// and step count is the one Zig's `shale --verbose` gives the same source.

#define _GNU_SOURCE

#include "test.h"

#include "../ran.c"

// A source run on a fresh root with no mounts.
static void ran_source(Ran *x, Str src, Budgets b) {
    RanOpts none = {NULL, false, false};
    ran_init(x, none);
    ran_run(x, src, b);
}

static Budgets cli_budgets(void) {
    Budgets b = budgets_default();
    b.output_bytes = 64 * 1024;
    return b;
}

TEST(flat_tree_reads_nodes_parents_depths_kids_spans_and_attributes_two_files_and_what_follows) {
    static Ran x;
    ran_source(&x, S("local t = host(\"roc_tree\", \"roc-ast flat 1 v\\n== /x/a.roc\\nn\\t0\\tfile\\t1\\t1\\t5\\t14\\nn\\t1\\tstatements\\t0\\t0\\t0\\t0\\nn\\t2\\te-binop\\t5\\t8\\t5\\t14\\na\\t3\\top\\t==\\nn\\t3\\te-ident\\t+5\\t8\\t5\\t1_0\\na\\t4\\traw\\tx\\n\\n== /x/b.roc\\nn\\t-0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\tline-doc\\tk\\\\tq\\\\\\\\\\n/x/c.roc: refused\\n\", \"whole\")\n"
                     "print(host(\"json_write\", t))\n"),
               cli_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("{\"files\":[{\"path\":\"/x/a.roc\",\"nodes\":[{\"kind\":\"file\",\"depth\":0,\"parent\":0,\"kids\":[2],\"line\":1,\"col\":1,\"end_line\":5,\"end_col\":14},{\"kind\":\"statements\",\"depth\":1,\"parent\":1,\"kids\":[3],\"line\":0,\"col\":0,\"end_line\":0,\"end_col\":0},{\"kind\":\"e-binop\",\"depth\":2,\"parent\":2,\"kids\":[4],\"line\":5,\"col\":8,\"end_line\":5,\"end_col\":14,\"op\":\"==\"},{\"kind\":\"e-ident\",\"depth\":3,\"parent\":3,\"kids\":[],\"line\":5,\"col\":8,\"end_line\":5,\"end_col\":10,\"raw\":\"x\"}]},{\"path\":\"/x/b.roc\",\"nodes\":[{\"kind\":\"file\",\"depth\":0,\"parent\":0,\"kids\":[],\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":2,\"line_doc\":\"k\\tq\\\\\"}]}],\"said\":\"/x/c.roc: refused\"}\n"));
    EXPECT_EQ(x.r.steps, 746);
    ran_free(&x);
}

TEST(flat_tree_refuses_a_line_that_does_not_read_naming_its_line_and_why) {
    static Ran x;
    ran_source(&x, S("function rt(s) return host(\"roc_tree\", \"roc-ast flat 1 p\\n\" .. s, \"whole\") end\n"
                     "for s in {\"n\\t0\\tfile\\t1\\t1\\t1\\t2\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\t9\\n\", \"== /x\\nn\\t1\\tfile\\t1\\t1\\t1\\t2\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t2\\traw\\tx\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t0\\traw\\tx\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\traw\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\traw\\tx\\\\q\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\traw\\tx\\\\\\n\", \"== /x\\nn\\t0\\t\\t1\\t1\\t1\\t2\\n\", \"== /x\\nn\\t0\\tfile\\tx\\t1\\t1\\t2\\n\", \"== /x\\nn\\t-1\\tfile\\t1\\t1\\t1\\t2\\n\", \"== /x\\nn\\t0\\tfile\\t_1\\t1\\t1\\t2\\n\", \"== /x\\nn\\t0\\tfile\\t18446744073709551616\\t1\\t1\\t2\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\traw\\tx\\na\\t1\\traw\\ty\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\tend-col\\tx\\n\", \"== /x\\nn\\tq\\tfile\\t1\\t1\\t1\\t2\\n\", \"== /x\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\tz\\traw\\tx\\n\", \"== /x\\nn\\t0\\tfi\\\\le\\t1\\t1\\t1\\t2\\n\"} do\n"
                     "  print(attempt(rt, s).error)\n"
                     "end\n"),
               cli_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("`roc_tree`: roc-ast's output line 2 is not a tree's: a node before any `== path`\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a node line without its six fields\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a line with too many fields\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a node's parent not yet read\n"
                     "`roc_tree`: roc-ast's output line 4 is not a tree's: an attribute's node not yet read\n"
                     "`roc_tree`: roc-ast's output line 4 is not a tree's: an attribute's node not yet read\n"
                     "`roc_tree`: roc-ast's output line 4 is not a tree's: an attribute line without its three fields\n"
                     "`roc_tree`: roc-ast's output line 4 is not a tree's: a bad escape\n"
                     "`roc_tree`: roc-ast's output line 4 is not a tree's: a bad escape\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a node with no kind\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a span's number\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a node's parent\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a span's number\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a span's number\n"
                     "`roc_tree`: roc-ast's output line 5 is not a tree's: an attribute twice in one node\n"
                     "`roc_tree`: roc-ast's output line 4 is not a tree's: an attribute takes a node field's name\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a node's parent\n"
                     "`roc_tree`: roc-ast's output line 4 is not a tree's: an attribute's node\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a bad escape\n"));
    EXPECT_EQ(x.r.steps, 2951);
    ran_free(&x);
}

TEST(flat_tree_reads_each_tools_header_as_its_own_and_refuses_another) {
    static Ran x;
    ran_source(&x, S("function rt(t, s) return host(t, s, \"whole\") end\n"
                     "for c in {{\"roc_tree\", \"== /x\\n\"}, {\"roc_tree\", \"roc-ast flat 2 p\\n\"}, {\"roc_tree\", \"roc-ast flat 10 p\\n\"}, {\"roc_tree\", \"\\n\\nroc-ast flat \\n\"}, {\"go_tree\", \"roc-ast flat 1 p\\n\"}, {\"go_tree\", \"go-ast flat 2 go1\\n\"}} do\n"
                     "  print(attempt(rt, c[1], c[2]).error)\n"
                     "end\n"
                     "print(host(\"json_write\", host(\"go_tree\", \"go-ast flat 1 go1.27.4\\n== /x/a.go\\nn\\t0\\tFile\\t1\\t1\\t3\\t2\\nn\\t1\\tIdent\\t1\\t9\\t1\\t10\\na\\t2\\trole\\tName\\na\\t2\\tname\\ta\\n\", \"whole\")))\n"
                     "print(host(\"json_write\", host(\"roc_tree\", \"roc-ast flat 1\", \"whole\")), host(\"json_write\", host(\"roc_tree\", \"\", \"cut\")))\n"),
               cli_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("`roc_tree`: roc-ast's output line 1 is not a tree's: no `roc-ast flat 1` header: an older roc-ast, or not its --flat; Shale's `shale tools/roc_ast/install.shale` installs the one this Shale reads\n"
                     "`roc_tree`: roc-ast's output line 1 is not a tree's: roc-ast's --flat is another format than 1, the one this Shale reads: Shale's `shale tools/roc_ast/install.shale` installs the matching roc-ast\n"
                     "`roc_tree`: roc-ast's output line 1 is not a tree's: roc-ast's --flat is another format than 1, the one this Shale reads: Shale's `shale tools/roc_ast/install.shale` installs the matching roc-ast\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: roc-ast's --flat is another format than 1, the one this Shale reads: Shale's `shale tools/roc_ast/install.shale` installs the matching roc-ast\n"
                     "`go_tree`: go-ast's output line 1 is not a tree's: no `go-ast flat 1` header: an older go-ast, or not its --flat; Shale's `shale tools/go_ast/install.shale` installs the one this Shale reads\n"
                     "`go_tree`: go-ast's output line 1 is not a tree's: go-ast's --flat is another format than 1, the one this Shale reads: Shale's `shale tools/go_ast/install.shale` installs the matching go-ast\n"
                     "{\"files\":[{\"path\":\"/x/a.go\",\"nodes\":[{\"kind\":\"File\",\"depth\":0,\"parent\":0,\"kids\":[2],\"line\":1,\"col\":1,\"end_line\":3,\"end_col\":2},{\"kind\":\"Ident\",\"depth\":1,\"parent\":1,\"kids\":[],\"line\":1,\"col\":9,\"end_line\":1,\"end_col\":10,\"role\":\"Name\",\"name\":\"a\"}]}]}\n"
                     "{\"files\":[]}\t{\"files\":[]}\n"));
    EXPECT_EQ(x.r.steps, 1786);
    ran_free(&x);
}

TEST(flat_tree_ends_the_trees_at_a_line_that_does_not_read_in_a_cut_output) {
    static Ran x;
    ran_source(&x, S("local text = \"roc-ast flat 1 p\\n== /x/a.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\n== /x/b.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\trawerror: IsDir\\n\"\n"
                     "function rt(e) return host(\"roc_tree\", \"roc-ast flat 1 p\\n== /x/a.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\n== /x/b.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\trawerror: IsDir\\n\", e) end\n"
                     "print(attempt(rt, \"whole\").error)\n"
                     "print(host(\"json_write\", host(\"roc_tree\", text, \"cut\")))\n"),
               cli_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("`roc_tree`: roc-ast's output line 6 is not a tree's: an attribute line without its three fields\n"
                     "{\"files\":[{\"path\":\"/x/a.roc\",\"nodes\":[{\"kind\":\"file\",\"depth\":0,\"parent\":0,\"kids\":[],\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":2}]},{\"path\":\"/x/b.roc\",\"nodes\":[{\"kind\":\"file\",\"depth\":0,\"parent\":0,\"kids\":[],\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":2}]}],\"said\":\"a\\t1\\trawerror: IsDir\"}\n"));
    EXPECT_EQ(x.r.steps, 489);
    ran_free(&x);
}

TEST(flat_tree_refuses_its_arguments_in_zigs_order) {
    static Ran x;
    ran_source(&x, S("function rt0() return host(\"roc_tree\", \"\") end\n"
                     "function rt(a, b) return host(\"go_tree\", a, b) end\n"
                     "print(attempt(rt0).error)\n"
                     "print(attempt(rt, \"\", \"half\").error)\n"
                     "print(attempt(rt, \"\", 1).error)\n"
                     "print(attempt(rt, {}, \"whole\").error)\n"
                     "print(attempt(rt, 5, \"nope\").error)\n"),
               cli_budgets());
    EXPECT_EQ(x.ran, OK);
    EXPECT_STR(buf_view(&x.r.out), S("`roc_tree`'s reading takes roc-ast's output and how its run ended, given 1 arguments\n"
                     "`go_tree`'s reading takes how the run ended, \"whole\" or \"cut\", not \"half\"\n"
                     "`go_tree`'s reading takes how the run ended, \"whole\" or \"cut\", not an integer\n"
                     "`go_tree`'s reading takes a text, not an empty table\n"
                     "`go_tree`'s reading takes how the run ended, \"whole\" or \"cut\", not \"nope\"\n"));
    EXPECT_EQ(x.r.steps, 484);
    ran_free(&x);
}

TEST(flat_tree_stops_one_past_the_step_budget_at_a_files_nodes) {
    static Ran x;
    Budgets b = cli_budgets();
    b.steps = 33;
    // 24 steps before the call, 8 for the text's 117 bytes, then the first
    // file's 2 nodes pass the budget.
    ran_source(&x, S("print(#host(\"roc_tree\", \"roc-ast flat 1 p\\n== /x/a.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\nn\\t1\\tk\\t1\\t1\\t1\\t2\\n== /x/b.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\nn\\t1\\tk\\t1\\t1\\t1\\t2\\nn\\t1\\tk\\t1\\t1\\t1\\t2\\n\", \"whole\").files)\n"), b);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_BUDGET);
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_STEPS);
    EXPECT_EQ(x.r.ctx.stop.column, 192);
    EXPECT_EQ(x.r.steps, 34);
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST_MAIN(TEST_CASE(flat_tree_reads_nodes_parents_depths_kids_spans_and_attributes_two_files_and_what_follows),
          TEST_CASE(flat_tree_refuses_a_line_that_does_not_read_naming_its_line_and_why),
          TEST_CASE(flat_tree_reads_each_tools_header_as_its_own_and_refuses_another),
          TEST_CASE(flat_tree_ends_the_trees_at_a_line_that_does_not_read_in_a_cut_output),
          TEST_CASE(flat_tree_refuses_its_arguments_in_zigs_order),
          TEST_CASE(flat_tree_stops_one_past_the_step_budget_at_a_files_nodes))
