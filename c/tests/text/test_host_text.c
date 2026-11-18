// test_host_text.c: the host's text calls, c/host/host_json.c (`json_read`,
// `json_write`), c/host/host_text.c (`re_lines`, `shale_tokens`) and
// c/host/flat_tree.c (`roc_tree` and `go_tree`, the reading of roc-ast's and
// go-ast's `--flat` output), each source run as the C `shale` runs it
// (../ran.c) and called through `host`, with no preludes. Every output, stop
// and step count is C Shale's own, as the goldens hold them.

// A part of ran_tests.c (`TEST_PART`, test.h), which defines _GNU_SOURCE and
// includes test.h and ran.c before it.


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
    ran_run(x, ran_cs(src), ran_budgets());
}

TEST(json_read_reads_each_kind_and_json_write_writes_it_back) {
    static Ran x;
    ran_source(&x, S("local v = host(\"json_read\", \"{\\\"a\\\": [1, -2, 3.5e2, true], \\\"b\\\": null, \\\"s\\\": \\\"\\\\u00e9\\\\n\\\"}\")\n"
                     "print(host(\"json_write\", v), v.a[3])\n"),
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    // A fraction stays its text, a null member is left out, an escape is
    // decoded to UTF-8 and written back as a byte.
    EXPECT_STR(buf_view(&x.r.out), S("{\"a\":[1,-2,\"3.5e2\",true],\"s\":\"\xc3\xa9\\n\"}\t3.5e2\n"));
    EXPECT_EQ(x.r.steps, 99);
    EXPECT(!x.r.ctx.stopped);
    ran_free(&x);
}

TEST(json_read_refuses_a_malformed_text_naming_its_byte_line_and_column) {
    static Ran x;
    ran_source(&x, S("host(\"json_read\", \"[1,\\n 2,\\n x]\")\n"), ran_budgets());
    text_expect_stop(&x, CLASS_ARGUMENT, 1, 18, S("`json_read`: a value was expected, not a byte (0x78) at byte 9 (line 3, column 2)"));
    EXPECT_EQ(x.r.out.len, 0);
    EXPECT_STR(ran_report(&x, &x.host), S("── stopped at stdin:1:1: argument: `json_read`: a value was expected, not a byte (0x78) at byte 9 (line 3, column 2) (18 steps)\n"
                                          "── used: 18 of 5000000 steps, 0 of 10000 reads, 0 of 67108864 read bytes, 0 of 65536 output bytes\n── steps: 18 in all; the most 14 (77%): host, 1 call; 4 (22%): script\n"));
    ran_free(&x);
}

TEST(json_read_stands_a_value_in_for_null_and_stops_past_its_depth) {
    static Ran x;
    ran_source(&x, S("print(host(\"json_write\", host(\"json_read\", \"[null, [1]]\", {null = 0, depth = 2})))\nhost(\"json_read\", \"[[[1]]]\", {depth = 2})\n"), ran_budgets());
    EXPECT_STR(buf_view(&x.r.out), S("[0,[1]]\n"));
    text_expect_stop(&x, CLASS_ARGUMENT, 2, 80, S("`json_read`: the text nests past its depth budget at byte 2 (line 1, column 3)"));
    ran_free(&x);
}

TEST(json_write_refuses_a_table_that_holds_itself) {
    static Ran x;
    ran_source(&x, S("local t = {}\nt.self = t\nhost(\"json_write\", t)\n"), ran_budgets());
    text_expect_stop(&x, CLASS_ARGUMENT, 3, 537, S("`json_write`: the value nests past 512 deep (or holds itself)"));
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(re_lines_keeps_the_matching_lines_and_charges_each_search) {
    static Ran x;
    ran_source(&x, S("for m in host(\"re_lines\", \"alpha\\nbeta\\n\\ngamma beta\", \"beta\") do print(m.line, m.text) end\nprint(#host(\"re_lines\", \"a\\nb\", \"a\", {invert = true}))\n"), ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("2\tbeta\n4\tgamma beta\n1\n"));
    EXPECT_EQ(x.r.steps, 169);
    ran_free(&x);
}

TEST(re_lines_stops_one_past_the_step_budget) {
    static Ran x;
    Budgets b = ran_budgets();
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
    ran_source(&x, S("host(\"re_lines\", \"a\", \"a\", {invert = true, foo = 1})\n"), ran_budgets());
    text_expect_stop(&x, CLASS_FAIL, 1, 25, S("`re_lines` takes `invert` and `nocase`, not `foo`"));
    ran_free(&x);
}

TEST(shale_tokens_gives_each_token_comments_among_them_with_its_span) {
    static Ran x;
    text_run_with(&x, "for t in host(\"shale_tokens\", \"t.shale\") do print(t.tag, t.text, t.line, t.start, t.stop) end\nprint(#host(\"shale_tokens\", \"t.shale\", {tag = \"comment\"}))\n",
                  "root/t.shale", "local a = 1 -- x\nprint(a)\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
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

TEST(flat_tree_reads_nodes_parents_depths_kids_spans_and_attributes_two_files_and_what_follows) {
    static Ran x;
    ran_source(&x, S("local t = host(\"roc_tree\", \"roc-ast flat 1 v\\n== /x/a.roc\\nn\\t0\\tfile\\t1\\t1\\t5\\t14\\nn\\t1\\tstatements\\t0\\t0\\t0\\t0\\nn\\t2\\te-binop\\t5\\t8\\t5\\t14\\na\\t3\\top\\t==\\nn\\t3\\te-ident\\t+5\\t8\\t5\\t1_0\\na\\t4\\traw\\tx\\n\\n== /x/b.roc\\nn\\t-0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\tline-doc\\tk\\\\tq\\\\\\\\\\n/x/c.roc: refused\\n\", \"whole\")\n"
                     "print(host(\"json_write\", t))\n"),
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
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
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
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

TEST(flat_tree_refuses_a_number_past_what_an_integer_holds_and_reads_the_most) {
    // A line or column past INT64_MAX is a line that is not a tree's, a
    // stop: never a negative integer, never the end of the process.
    static Ran x;
    ran_source(&x, S("function rt(s) return host(\"roc_tree\", \"roc-ast flat 1 p\\n\" .. s, \"whole\") end\n"
                     "print(attempt(rt, \"== /x\\nn\\t0\\tfile\\t9223372036854775808\\t1\\t1\\t2\\n\").error)\n"
                     "print(attempt(rt, \"== /x\\nn\\t0\\tfile\\t1\\t18446744073709551615\\t1\\t2\\n\").error)\n"
                     "print(rt(\"== /x\\nn\\t0\\tfile\\t9223372036854775807\\t1\\t1\\t2\\n\").files[1].nodes[1].line)\n"),
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`roc_tree`: roc-ast's output line 3 is not a tree's: a span's number\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: a span's number\n"
                     "9223372036854775807\n"));
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
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`roc_tree`: roc-ast's output line 1 is not a tree's: no `roc-ast flat 1` header: an older roc-ast, or not its --flat; `shale install.shale` in a checkout of https://github.com/showell/roc-ast installs the one this Shale reads\n"
                     "`roc_tree`: roc-ast's output line 1 is not a tree's: roc-ast's --flat is another format than 1, the one this Shale reads: `shale install.shale` in a checkout of https://github.com/showell/roc-ast installs the matching roc-ast\n"
                     "`roc_tree`: roc-ast's output line 1 is not a tree's: roc-ast's --flat is another format than 1, the one this Shale reads: `shale install.shale` in a checkout of https://github.com/showell/roc-ast installs the matching roc-ast\n"
                     "`roc_tree`: roc-ast's output line 3 is not a tree's: roc-ast's --flat is another format than 1, the one this Shale reads: `shale install.shale` in a checkout of https://github.com/showell/roc-ast installs the matching roc-ast\n"
                     "`go_tree`: go-ast's output line 1 is not a tree's: no `go-ast flat 1` header: an older go-ast, or not its --flat; Shale's `shale tools/go_ast/install.shale` installs the one this Shale reads\n"
                     "`go_tree`: go-ast's output line 1 is not a tree's: go-ast's --flat is another format than 1, the one this Shale reads: Shale's `shale tools/go_ast/install.shale` installs the matching go-ast\n"
                     "{\"files\":[{\"path\":\"/x/a.go\",\"nodes\":[{\"kind\":\"File\",\"depth\":0,\"parent\":0,\"kids\":[2],\"line\":1,\"col\":1,\"end_line\":3,\"end_col\":2},{\"kind\":\"Ident\",\"depth\":1,\"parent\":1,\"kids\":[],\"line\":1,\"col\":9,\"end_line\":1,\"end_col\":10,\"role\":\"Name\",\"name\":\"a\"}]}]}\n"
                     "{\"files\":[]}\t{\"files\":[]}\n"));
    EXPECT_EQ(x.r.steps, 1906);
    ran_free(&x);
}

TEST(flat_tree_ends_the_trees_at_a_line_that_does_not_read_in_a_cut_output) {
    static Ran x;
    ran_source(&x, S("local text = \"roc-ast flat 1 p\\n== /x/a.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\n== /x/b.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\trawerror: IsDir\\n\"\n"
                     "function rt(e) return host(\"roc_tree\", \"roc-ast flat 1 p\\n== /x/a.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\n== /x/b.roc\\nn\\t0\\tfile\\t1\\t1\\t1\\t2\\na\\t1\\trawerror: IsDir\\n\", e) end\n"
                     "print(attempt(rt, \"whole\").error)\n"
                     "print(host(\"json_write\", host(\"roc_tree\", text, \"cut\")))\n"),
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("`roc_tree`: roc-ast's output line 6 is not a tree's: an attribute line without its three fields\n"
                     "{\"files\":[{\"path\":\"/x/a.roc\",\"nodes\":[{\"kind\":\"file\",\"depth\":0,\"parent\":0,\"kids\":[],\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":2}]},{\"path\":\"/x/b.roc\",\"nodes\":[{\"kind\":\"file\",\"depth\":0,\"parent\":0,\"kids\":[],\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":2}]}],\"said\":\"a\\t1\\trawerror: IsDir\"}\n"));
    EXPECT_EQ(x.r.steps, 489);
    ran_free(&x);
}

TEST(flat_tree_refuses_its_arguments_first_to_last) {
    static Ran x;
    ran_source(&x, S("function rt0() return host(\"roc_tree\", \"\") end\n"
                     "function rt(a, b) return host(\"go_tree\", a, b) end\n"
                     "print(attempt(rt0).error)\n"
                     "print(attempt(rt, \"\", \"half\").error)\n"
                     "print(attempt(rt, \"\", 1).error)\n"
                     "print(attempt(rt, {}, \"whole\").error)\n"
                     "print(attempt(rt, 5, \"nope\").error)\n"),
               ran_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
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
    Budgets b = ran_budgets();
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
    EXPECT_STR(ran_report_of(&x, &x.host, false), S("── stopped at stdin:1:192: budget steps: the step budget (33 steps) ran out (34 steps)\n"
                                                    "── used: 34 of 33 steps, 0 of 10000 reads, 0 of 67108864 read bytes, 0 of 65536 output bytes\n── steps: 34 in all; the most 21 (61%): host, 1 call; 13 (38%): script\n"));
    ran_free(&x);
}

TEST_PART(host_text, TEST_CASE(json_read_reads_each_kind_and_json_write_writes_it_back), TEST_CASE(json_read_refuses_a_malformed_text_naming_its_byte_line_and_column),
          TEST_CASE(json_read_stands_a_value_in_for_null_and_stops_past_its_depth), TEST_CASE(json_write_refuses_a_table_that_holds_itself),
          TEST_CASE(re_lines_keeps_the_matching_lines_and_charges_each_search), TEST_CASE(re_lines_stops_one_past_the_step_budget),
          TEST_CASE(re_lines_refuses_an_unknown_option_before_any_other), TEST_CASE(shale_tokens_gives_each_token_comments_among_them_with_its_span),
          TEST_CASE(shale_tokens_stops_on_a_file_that_does_not_lex_naming_the_lexers_line),
          TEST_CASE(flat_tree_reads_nodes_parents_depths_kids_spans_and_attributes_two_files_and_what_follows),
          TEST_CASE(flat_tree_refuses_a_line_that_does_not_read_naming_its_line_and_why),
          TEST_CASE(flat_tree_refuses_a_number_past_what_an_integer_holds_and_reads_the_most),
          TEST_CASE(flat_tree_reads_each_tools_header_as_its_own_and_refuses_another),
          TEST_CASE(flat_tree_ends_the_trees_at_a_line_that_does_not_read_in_a_cut_output),
          TEST_CASE(flat_tree_refuses_its_arguments_first_to_last),
          TEST_CASE(flat_tree_stops_one_past_the_step_budget_at_a_files_nodes));
