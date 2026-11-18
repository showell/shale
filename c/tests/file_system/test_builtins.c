// test_builtins.c: the file system's builtins (c/script/builtins_files.c,
// builtins_walk.c, builtins_change.c) and the Linux host's `real`,
// `may_change` and `edits_write` (c/host/host_call.c), each source run as
// the C `shale --verbose` runs it (ran.c) over a fresh root. Every output,
// report (its effects, its stop, its steps, reads and read bytes) and file
// left is C Shale's own: the sources and answers began as Zig Shale's runs',
// copied whole, and the goldens hold them now.

// A part of ran_tests.c (`TEST_PART`, test.h), which defines _GNU_SOURCE and
// includes test.h and ran.c before it.

static const char *const SRC_CHANGE_BUDGETS = "-- budget: changes=3 change_bytes=10\nreplace(\"a.txt\", \"12345\")\nreplace(\"b.txt\", \"123456\")\n";
static const char *const OUT_CHANGE_BUDGETS = "";
static const char *const REPORT_CHANGE_BUDGETS = "── effects (1) ──\nreplace a.txt: ok\n── stopped at stdin:3:1: budget change_bytes: the file-system change budget (bytes) ran out (25 steps)\n── used: 25 of 5000000 steps, 0 of 10000 reads, 0 of 67108864 read bytes, 0 of 65536 output bytes\n── steps: 25 in all; the most 17 (68%): replace, 2 calls; 8 (32%): script\n";

static const char *const SRC_CHANGE_COUNT = "-- budget: changes=2\nreplace(\"a.txt\", \"1\")\nreplace(\"b.txt\", \"2\")\nreplace(\"c.txt\", \"3\")\n";
static const char *const OUT_CHANGE_COUNT = "";
static const char *const REPORT_CHANGE_COUNT = "── effects (2) ──\nreplace a.txt: ok\nreplace b.txt: ok\n── stopped at stdin:4:1: budget changes: the file-system change budget (count) ran out (32 steps)\n── used: 32 of 5000000 steps, 0 of 10000 reads, 0 of 67108864 read bytes, 0 of 65536 output bytes\n── steps: 32 in all; the most 20 (62%): replace, 3 calls; 12 (37%): script\n";

static const char *const SRC_CHANGES = "function rm(p) return remove(p) end\nfunction rmt(p) return remove_tree(p) end\nfunction md(p) return mkdir(p) end\nfunction rp(p, s) return replace(p, s) end\nmkdir(\"c/d\")\nreplace(\"c/x.txt\", \"x\")\nappend(\"c/x.txt\", \"y\")\nappend(\"c/new.txt\", \"n\")\nprint(read(\"c/x.txt\"), read(\"c/new.txt\"))\nremove(\"c/new.txt\")\nprint(attempt(rm, \"c/new.txt\").error)\nprint(attempt(md, \"c/x.txt/q\").error)\nremove_tree(\"c/d\")\nprint(attempt(rmt, \"c/d\").error)\nprint(attempt(rp, \"\", \"x\").error)\nprint(exists(\"c/d\"), exists(\"c/x.txt\"))\n";
static const char *const OUT_CHANGES = "xy\tn\n`remove(\"c/new.txt\")`: not found (FileNotFound): `exists(p)` asks first\n`mkdir(\"c/x.txt/q\")`: a part of the path is a file, not a folder (NotDir): `is_dir(p)` asks first\n`remove_tree(\"c/d\")`: not found (FileNotFound): `exists(p)` asks first\n`replace(\"\")` would act on the whole root: name a path of at least one part\nfalse\ttrue\n";
static const char *const REPORT_CHANGES = "── effects (9) ──\nmkdir c/d: ok\nreplace c/x.txt: ok\nappend c/x.txt: ok\nappend c/new.txt: ok\nremove c/new.txt: ok\nremove c/new.txt: FileNotFound\nmkdir c/x.txt/q: NotDir\nremove_tree c/d: ok\nremove_tree c/d: FileNotFound\n── finished (544 steps)\n── used: 544 of 5000000 steps, 4 of 10000 reads, 3 of 67108864 read bytes, 333 of 65536 output bytes\n── steps: 544 in all; the most 428 (78%): script; 22 (4%): read, 2 calls; 22 (4%): append, 2 calls\n";

static const char *const SRC_EDIT = "function ed(p, a, b, c) return edit(p, a, b, c) end\nfunction el(p, l) return edit(p, l) end\nreplace(\"e.txt\", \"one two\\ntwo_x two\\nthree\\nfour\\nfive\\n\")\nprint(edit(\"e.txt\", \"two\", \"2\", {all = true, word = true}))\nprint(edit(\"e.txt\", {{old = \"three\\n\", new = \"\"}, {old = \"five\", new = \"5\\n6\"}}))\nprint(read(\"e.txt\"))\nprint(attempt(ed, \"e.txt\", \"fiv\", \"x\", {word = true}).error)\nprint(attempt(ed, \"e.txt\", \"2\", \"x\", {}).error)\nprint(attempt(ed, \"e.txt\", \"four\\nsix\", \"x\", {}).error)\nprint(attempt(ed, \"e.txt\", \"fourteen\", \"x\", {}).error)\nprint(attempt(el, \"e.txt\", {{old = \"a\"}}).error)\nprint(attempt(el, \"e.txt\", {}).error)\nprint(attempt(ed, \"e.txt\", \"a\", \"b\", {every = true}).error)\n";
static const char *const OUT_EDIT = "--- a/e.txt\n+++ b/e.txt\n@@ -1,1 +1,1 @@\n-one two\n+one 2\n@@ -2,1 +2,1 @@\n-two_x two\n+two_x 2\n2\n--- a/e.txt\n+++ b/e.txt\n@@ -3,2 +3,1 @@\n-three\n-four\n+four\n@@ -5,1 +4,2 @@\n-five\n+5\n+6\n2\none 2\ntwo_x 2\nfour\n5\n6\n\n`edit`: \"fiv\" is not in e.txt as a whole word: its line \"fiv\" is not in it\n`edit`: \"2\" is in e.txt 2 times: a longer `old` picks one, or `{all = true}` changes every one\n`edit`: \"four\nsix\" is not in e.txt: its line \"six\" is not in it\n`edit`: \"fourteen\" is not in e.txt: its line \"fourteen\" is not in it\n`edit`: edit 1 has no `new`\n`edit` of no edits: the list is empty\n`edit` takes `all` and `word`, not `every`\n";
static const char *const REPORT_EDIT = "── effects (3) ──\nreplace e.txt: ok\nedit e.txt: ok\nedit e.txt: ok\n── finished (1057 steps)\n── used: 1057 of 5000000 steps, 7 of 10000 reads, 179 of 67108864 read bytes, 619 of 65536 output bytes\n── steps: 1.0K in all; the most 637 (60%): script; 372 (35%): edit, 9 calls; 40 (3%): replace, 1 call\n";

static const char *const SRC_EDITS_WRITE = "function ew(l) return host(\"edits_write\", l) end\nreplace(\"a.txt\", \"a1 a2\\n\")\nreplace(\"b.txt\", \"b1\\n\")\nprint(host(\"edits_write\", {{\"a.txt\", {{old = \"a1\", new = \"A\"}}}, {\"b.txt\", {{old = \"b1\", new = \"B\"}}}}))\nprint(read(\"a.txt\"), read(\"b.txt\"))\nprint(attempt(ew, {{\"a.txt\", {{old = \"A\", new = \"q\"}}}, {\"b.txt\", {{old = \"zz\", new = \"y\"}}}}).error)\nprint(read(\"a.txt\"))\nprint(attempt(ew, {{\"a.txt\", {{old = \"A\", new = \"q\"}}}, {\"a.txt\", {{old = \"a2\", new = \"y\"}}}}).error)\nprint(host(\"may_change\", \"a.txt\"), sub(host(\"real\", \"a.txt\"), -10))\n";
static const char *const OUT_EDITS_WRITE = "--- a/a.txt\n+++ b/a.txt\n@@ -1,1 +1,1 @@\n-a1 a2\n+A a2\n--- a/b.txt\n+++ b/b.txt\n@@ -1,1 +1,1 @@\n-b1\n+B\n2\nA a2\n\tB\n\n`edit`: \"zz\" is not in b.txt: its line \"zz\" is not in it\nA a2\n\n`edits_write`: a.txt is named twice: one list of its edits, which apply in sequence\ntrue\troot/a.txt\n";
static const char *const REPORT_EDITS_WRITE = "── effects (4) ──\nreplace a.txt: ok\nreplace b.txt: ok\nedit a.txt: ok\nedit b.txt: ok\n── finished (590 steps)\n── used: 590 of 5000000 steps, 8 of 10000 reads, 33 of 67108864 read bytes, 274 of 65536 output bytes\n── steps: 590 in all; the most 310 (52%): script; 228 (38%): host, 5 calls; 21 (3%): read, 3 calls\n";

static const char *const SRC_LOOKS = "mkdir(\"d/e\")\nreplace(\"d/b.txt\", \"bb\")\nreplace(\"d/a.txt\", \"a\")\nprint(join(ls(\"d\"), \",\"), exists(\"d/a.txt\"), exists(\"d/zz\"), is_dir(\"d/e\"), is_dir(\"d/a.txt\"), is_dir(\"nope\"))\nlocal s = stat(\"d/b.txt\")\nprint(s.size, s.kind, stat(\"d\").kind)\nfunction l(p) return ls(p) end\nfunction st(p) return stat(p) end\nprint(attempt(l, \"d/a.txt\").error)\nprint(attempt(l, \"nope\").error)\nprint(attempt(st, \"nope\").error)\n";
static const char *const OUT_LOOKS = "a.txt,b.txt,e\ttrue\tfalse\ttrue\tfalse\tfalse\n2\tfile\tdirectory\n`ls(\"d/a.txt\")`: a part of the path is a file, not a folder (NotDir): `is_dir(p)` asks first\n`ls(\"nope\")`: not found (FileNotFound): `exists(p)` asks first\n`stat(\"nope\")`: not found (FileNotFound): `exists(p)` asks first\n";
static const char *const REPORT_LOOKS = "── effects (3) ──\nmkdir d/e: ok\nreplace d/b.txt: ok\nreplace d/a.txt: ok\n── finished (520 steps)\n── used: 520 of 5000000 steps, 11 of 10000 reads, 0 of 67108864 read bytes, 280 of 65536 output bytes\n── steps: 520 in all; the most 378 (72%): script; 53 (10%): ls, 3 calls; 21 (4%): replace, 2 calls\n";

static const char *const SRC_READ_BUDGETS = "-- budget: reads=6 read_bytes=40\nreplace(\"f.txt\", \"0123456789abcdef0123456789\")\nprint(read(\"f.txt\", 0, 10))\nprint(count_in(\"f.txt\", \"f\"))\nprint(read(\"f.txt\"))\n";
static const char *const OUT_READ_BUDGETS = "0123456789\n1\n";
static const char *const REPORT_READ_BUDGETS = "── effects (1) ──\nreplace f.txt: ok\n── stopped at stdin:5:7: budget read_bytes: the file-system read budget (bytes) ran out (86 steps)\n── used: 86 of 5000000 steps, 3 of 6 reads, 36 of 40 read bytes, 13 of 65536 output bytes\n── steps: 86 in all; the most 32 (37%): script; 32 (37%): replace, 1 call; 13 (15%): read, 2 calls\n";

static const char *const SRC_READS = "function rd(p, o) return read(p, o) end\nfunction gr(p, s) return grep(p, s) end\nfunction ro(p, d) return read_or(p, d) end\nreplace(\"f.txt\", \"alpha\\nbeta gamma\\n\\ndelta beta\")\nprint(read(\"f.txt\", 6, 4), \"[\" .. read(\"f.txt\", 100) .. \"]\", read_or(\"none\", 7))\nlocal p = read_lines(\"f.txt\", 0, 3)\nprint(#p.lines, p.lines[1], p.next, p.done)\np = read_lines(\"f.txt\", p.next)\nprint(#p.lines, p.lines[3], p.next, p.done)\nfor g in grep(\"f.txt\", \"beta\") do print(g.line, g.text) end\nprint(line_at(\"f.txt\", 8), line_at(\"f.txt\", 17) == \"\", line_at(\"f.txt\", 99))\nprint(count_lines(\"f.txt\"), count_lines(\"f.txt\", 0, 6), count_lines(\"f.txt\", 6))\nprint(count_in(\"f.txt\", \"a\"), find_in(\"f.txt\", \"beta\", 7), find_in(\"f.txt\", \"zz\"))\nlocal big = rep(\"x\", 65530) .. \"needle\" .. rep(\"y\", 100) .. \"needle\"\nreplace(\"big.txt\", big)\nprint(count_in(\"big.txt\", \"needle\"), find_in(\"big.txt\", \"needle\"), count_in(\"big.txt\", \"needle\", 65531), count_lines(\"big.txt\"))\nprint(attempt(rd, \"f.txt\", -1).error)\nprint(attempt(gr, \"f.txt\", \"\").error)\nprint(attempt(ro, \".\", \"x\").error)\n";
static const char *const OUT_READS = "beta\t[]\t7\n1\talpha\t6\tfalse\n3\tdelta beta\t28\ttrue\n2\tbeta gamma\n4\tdelta beta\nbeta gamma\ttrue\tnil\n4\t1\t3\n7\t24\tnil\n2\t65530\t1\t1\n`read`: offset -1 is before the file's start\n`grep` of \"\" is an error: there is nothing to look for\n`read_or(\"\")` would act on the whole root: name a path of at least one part\n";
static const char *const REPORT_READS = "── effects (2) ──\nreplace f.txt: ok\nreplace big.txt: ok\n── finished (210059 steps)\n── used: 210059 of 5000000 steps, 19 of 10000 reads, 197217 of 67108864 read bytes, 296 of 65536 output bytes\n── steps: 210.0K in all; the most 66.1K (31%): script; 65.6K (31%): replace, 2 calls; 65.6K (31%): rep, 2 calls\n";

static const char *const SRC_RO_MOUNT = "print(read(\"ro:a.txt\"))\nreplace(\"mine.txt\", \"m\")\nreplace(\"ro:a.txt\", \"changed\")\n";
static const char *const OUT_RO_MOUNT = "a\n";
static const char *const REPORT_RO_MOUNT = "── effects (1) ──\nreplace mine.txt: ok\n── stopped at stdin:3:1: refused: ro:a.txt: mount `ro` is read-only (43 steps)\n── used: 43 of 5000000 steps, 1 of 10000 reads, 1 of 67108864 read bytes, 2 of 65536 output bytes\n── steps: 43 in all; the most 19 (44%): replace, 2 calls; 14 (32%): script; 10 (23%): read, 1 call\n";

static const char *const SRC_UNDO = "replace(\"keep.txt\", \"new\")\nreplace(\"made.txt\", \"m\")\nfail(\"stop\")\n";
static const char *const OUT_UNDO = "";
static const char *const REPORT_UNDO = "── effects (2) ──\nreplace keep.txt: ok\nreplace made.txt: ok\n── stopped at stdin:3:1: fail: stop (37 steps)\n── used: 37 of 5000000 steps, 0 of 10000 reads, 0 of 67108864 read bytes, 0 of 65536 output bytes\n── steps: 37 in all; the most 22 (59%): replace, 2 calls; 11 (29%): script; 4 (10%): fail, 1 call\n";
static const char *const UNDONE_UNDO = "── undone: 2 path(s) the run changed put back\n";

static const char *const SRC_WALK = "function wk(p, o) return walk(p, o) end\nmkdir(\"w/sub/deep\")\nmkdir(\"w/.hid\")\nreplace(\"w/b.zig\", \"b\")\nreplace(\"w/a.txt\", \"aa\")\nreplace(\"w/sub/c.zig\", \"ccc\")\nreplace(\"w/sub/deep/d.zig\", \"dddd\")\nfor e in walk(\"w\") do print(e.path, e.kind, e.size) end\nprint(\"--\")\nfor e in walk(\"w\", {depth = 1, kind = \"file\"}) do print(e.path) end\nprint(\"--\")\nfor e in walk(\"w\", {like = \"*.zig\", skip = \".*\"}) do print(e.path) end\nprint(\"--\")\nfor e in walk(\"w\", {after = \"w/sub/c.zig\"}) do print(e.path) end\nprint(\"--\")\nprint(#walk(\".\", {kind = \"directory\"}), attempt(wk, \"w/a.txt\", {}).error)\nprint(attempt(wk, \"w\", {depth = 0}).error)\nprint(attempt(wk, \"w\", {kind = \"fifo\"}).error)\nprint(attempt(wk, \"w\", {after = \"x/y\"}).error)\n";
static const char *const OUT_WALK = "w/.hid\tdirectory\t0\nw/a.txt\tfile\t2\nw/b.zig\tfile\t1\nw/sub\tdirectory\t0\nw/sub/c.zig\tfile\t3\nw/sub/deep\tdirectory\t0\nw/sub/deep/d.zig\tfile\t4\n--\nw/a.txt\nw/b.zig\n--\nw/b.zig\nw/sub/c.zig\nw/sub/deep/d.zig\n--\nw/sub/deep\nw/sub/deep/d.zig\n--\n4\t`walk(\"w/a.txt\")`: a part of the path is a file, not a folder (NotDir): `is_dir(p)` asks first\n`walk`: a depth of 0: 1 is the folder's own entries\n`walk`: a kind of \"fifo\": \"file\", \"directory\" or \"other\"\n`walk`: `after` is \"x/y\", which is not under \"w\": it is a path an earlier walk of it returned\n";
static const char *const REPORT_WALK = "── effects (6) ──\nmkdir w/sub/deep: ok\nmkdir w/.hid: ok\nreplace w/b.zig: ok\nreplace w/a.txt: ok\nreplace w/sub/c.zig: ok\nreplace w/sub/deep/d.zig: ok\n── finished (1760 steps)\n── used: 1760 of 5000000 steps, 36 of 10000 reads, 0 of 67108864 read bytes, 526 of 65536 output bytes\n── steps: 1.7K in all; the most 945 (53%): script; 734 (41%): walk, 9 calls; 62 (3%): replace, 4 calls\n";

static const char *const SRC_WALK_PARTS = "-- budget: call_entries=3\nmkdir(\"p\")\nfor n in {\"a\", \"b\", \"c\", \"d\", \"e\"} do replace(\"p/\" .. n, n) end\nlocal part = walk(\"p\")\nprint(#part, part[4].kind, part[4].path)\npart = walk(\"p\", {after = part[4].path})\nprint(#part, part[1].path, part[2].path)\nlocal k = walk(\"p\", {keep_reads = 9997})\nprint(#k, k[1].kind)\n";
static const char *const OUT_WALK_PARTS = "4\tmore\tp/c\n2\tp/d\tp/e\n1\tmore\n";
static const char *const REPORT_WALK_PARTS = "── effects (6) ──\nmkdir p: ok\nreplace p/a: ok\nreplace p/b: ok\nreplace p/c: ok\nreplace p/d: ok\nreplace p/e: ok\n── finished (342 steps)\n── used: 342 of 5000000 steps, 7 of 10000 reads, 0 of 67108864 read bytes, 28 of 65536 output bytes\n── steps: 342 in all; the most 186 (54%): script; 124 (36%): walk, 3 calls; 30 (8%): replace, 5 calls\n";

static const char *const SRC_YIELD_KEEPS = "function w(i)\n  replace(\"f\" .. i .. \".txt\", \"x\")\n  yield()\n  return nil\nend\nfor i in {1, 2, 3} do w(i) end\nremove(\"f2.txt\")\n";
static const char *const OUT_YIELD_KEEPS = "";
static const char *const REPORT_YIELD_KEEPS = "── effects (4) ──\nreplace f1.txt: ok\nreplace f2.txt: ok\nreplace f3.txt: ok\nremove f2.txt: ok\n── finished (134 steps)\n── used: 134 of 5000000 steps, 0 of 10000 reads, 0 of 67108864 read bytes, 0 of 65536 output bytes\n── steps: 134 in all; the most 103 (76%): script; 24 (17%): replace, 3 calls; 7 (5%): remove, 1 call\n";

static Budgets builtins_test_budgets(void) {
    Budgets b;
    cli_defaults(&b);
    return b;
}

static RanOpts builtins_test_verbose(void) {
    RanOpts o = {NULL, true, false};
    return o;
}

// Runs `src` on `x`, made with `ran_init` (and any files a test puts),
// and holds its output and its report to Zig's.
static bool builtins_test_ran(Ran *x, const char *src, Budgets b, const char *out, const char *report) { // cstr: the test's literals
    ran_run(x, ran_cs(src), b);
    bool same_out = str_eq(buf_view(&x->r.out), ran_cs(out));
    Str said = ran_report_of(x, &x->host, true);
    bool same_report = str_eq(said, ran_cs(report));
    if (!same_out) fprintf(stderr, "  printed:\n%.*s\n  wanted:\n%s\n", (int)x->r.out.len, x->r.out.ptr, out);
    if (!same_report) fprintf(stderr, "  reported:\n%.*s\n  wanted:\n%s\n", (int)said.len, said.ptr, report);
    return x->parsed && same_out && same_report;
}

// Whether the root's `rel` holds exactly `text`.
static bool builtins_test_holds(Ran *x, const char *rel, const char *text) { // cstr: test literals
    Str got = S("");
    char path[PATH_MAX];
    int n = snprintf(path, sizeof path, "root/%s", rel);
    if (n <= 0 || !ran_file(x, path, &got)) return false;
    return str_eq(got, ran_cs(text));
}

static bool builtins_test_absent(Ran *x, const char *rel) { // cstr: a test's literal
    Str got = S("");
    char path[PATH_MAX];
    int n = snprintf(path, sizeof path, "root/%s", rel);
    return n > 0 && !ran_file(x, path, &got);
}

TEST(ls_stat_exists_and_is_dir_look_without_changing_and_name_a_missing_path) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    EXPECT(builtins_test_ran(&x, SRC_LOOKS, builtins_test_budgets(), OUT_LOOKS, REPORT_LOOKS));
    EXPECT_EQ(x.r.reads, 11);
    EXPECT(builtins_test_holds(&x, "d/a.txt", "a"));
    ran_free(&x);
}

TEST(the_reads_whole_in_parts_by_lines_and_scans_across_a_chunks_edge) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    EXPECT(builtins_test_ran(&x, SRC_READS, builtins_test_budgets(), OUT_READS, REPORT_READS));
    EXPECT_EQ(x.r.steps, 210059);
    EXPECT_EQ(x.r.read_bytes, 197217);
    ran_free(&x);
}

TEST(the_read_budgets_stop_a_read_before_its_bytes_are_taken) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    Budgets b = builtins_test_budgets();
    b.reads = 6;
    b.read_bytes = 40;
    EXPECT(builtins_test_ran(&x, SRC_READ_BUDGETS, b, OUT_READ_BUDGETS, REPORT_READ_BUDGETS));
    EXPECT_EQ(x.r.ctx.stop.budget, BUDGET_READ_BYTES);
    EXPECT_EQ(x.r.read_bytes, 36);
    ran_free(&x);
}

TEST(walk_sorts_filters_skips_resumes_and_names_its_mistakes) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    EXPECT(builtins_test_ran(&x, SRC_WALK, builtins_test_budgets(), OUT_WALK, REPORT_WALK));
    EXPECT_EQ(x.r.reads, 36);
    ran_free(&x);
}

TEST(walk_in_parts_ends_each_with_more_and_keep_reads_reads_nothing) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    Budgets b = builtins_test_budgets();
    b.call_entries = 3;
    EXPECT(builtins_test_ran(&x, SRC_WALK_PARTS, b, OUT_WALK_PARTS, REPORT_WALK_PARTS));
    EXPECT_EQ(x.r.reads, 7);
    ran_free(&x);
}

TEST(every_change_is_logged_with_its_outcome_a_failed_one_too) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    EXPECT(builtins_test_ran(&x, SRC_CHANGES, builtins_test_budgets(), OUT_CHANGES, REPORT_CHANGES));
    EXPECT_EQ(x.r.changes, 9);
    EXPECT(builtins_test_holds(&x, "c/x.txt", "xy"));
    EXPECT(builtins_test_absent(&x, "c/new.txt"));
    ran_free(&x);
}

TEST(the_change_bytes_budget_stops_a_change_before_it_is_written) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    Budgets b = builtins_test_budgets();
    b.changes = 3;
    b.change_bytes = 10;
    EXPECT(builtins_test_ran(&x, SRC_CHANGE_BUDGETS, b, OUT_CHANGE_BUDGETS, REPORT_CHANGE_BUDGETS));
    EXPECT(builtins_test_holds(&x, "a.txt", "12345"));
    EXPECT(builtins_test_absent(&x, "b.txt"));
    EXPECT_EQ(x.r.change_bytes, 5);
    ran_free(&x);
}

TEST(the_changes_budget_stops_the_change_past_it) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    Budgets b = builtins_test_budgets();
    b.changes = 2;
    EXPECT(builtins_test_ran(&x, SRC_CHANGE_COUNT, b, OUT_CHANGE_COUNT, REPORT_CHANGE_COUNT));
    EXPECT(builtins_test_holds(&x, "b.txt", "2"));
    EXPECT(builtins_test_absent(&x, "c.txt"));
    EXPECT_EQ(x.r.changes, 2);
    ran_free(&x);
}

TEST(edit_prints_its_diff_holds_word_and_all_and_says_where_old_parts) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    EXPECT(builtins_test_ran(&x, SRC_EDIT, builtins_test_budgets(), OUT_EDIT, REPORT_EDIT));
    EXPECT(builtins_test_holds(&x, "e.txt", "one 2\ntwo_x 2\nfour\n5\n6\n"));
    ran_free(&x);
}

TEST(edit_says_a_diff_as_one_line_without_verbose) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    ran_run(&x, S("replace(\"e.txt\", \"a\\nb\\n\")\nprint(edit(\"e.txt\", \"b\", \"c\\nd\"))\nprint(5000000 - budget().steps)\n"), builtins_test_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("edit e.txt: 1 hunk, -1 +2 lines\n1\n80\n"));
    EXPECT(builtins_test_holds(&x, "e.txt", "a\nc\nd\n"));
    ran_free(&x);
}

// Word mode holds an edge only where `old`'s own edge is a word byte: `f(`
// is whole before `x`, which it was not before (the call stopped: "not in
// w.txt as a whole word"), and still not after the `a` of `af(`. A miss that
// is there inside a word says so (it said "each of its lines is in it, but
// not together").
TEST(edit_word_holds_only_the_edges_where_old_ends_in_a_word_byte) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    ran_run(&x,
            S("function ed(p, a, b, o) return edit(p, a, b, o) end\nreplace(\"w.txt\", \"f(x) af(y) f(z)\\n(x)\\n\")\nprint(edit(\"w.txt\", \"f(\", \"g(\", {all = true, word = true}))\nprint(edit(\"w.txt\", \"(x)\\n\", \"[x]\\n\", {word = "
              "true}))\nprint(attempt(ed, \"w.txt\", \"a\", \"b\", {word = true}).error)\n"),
            builtins_test_budgets());
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("edit w.txt: 1 hunk, -1 +1 lines\n2\nedit w.txt: 1 hunk, -1 +1 lines\n1\n`edit`: \"a\" is not in w.txt as a whole word: it is there, but each time inside a longer word\n"));
    EXPECT(builtins_test_holds(&x, "w.txt", "g(x) af(y) g(z)\n[x]\n"));
    ran_free(&x);
}

// A cap of 64 KiB or less is `read_lines`' part itself, so a part that ends
// mid-line can't go on to the line's end: it ends at its last whole line
// (before, the first such part stopped, "the line at offset 0 runs past 11
// bytes"). A part that ends at a newline on the cap is whole, and only a line
// past the cap from the part's start still stops.
TEST(read_lines_at_the_call_bytes_cap_ends_a_part_at_its_last_whole_line) {
    static Ran x;
    RanOpts o = {NULL, false, false};
    ran_init(&x, o);
    Budgets b = builtins_test_budgets();
    b.call_bytes = 11;
    ran_run(&x,
            S("function rl(p, o) return read_lines(p, o) end\nreplace(\"f.txt\", \"aaaa\\nbbbb\\ncccc\\n\")\nlocal p = read_lines(\"f.txt\")\nprint(#p.lines, p.lines[2], p.next, p.done)\np = read_lines(\"f.txt\", p.next)\nprint(#p.lines, "
              "p.lines[1], p.next, p.done)\nreplace(\"g.txt\", \"aaaaaaaaaa\\nb\\n\")\np = read_lines(\"g.txt\")\nprint(#p.lines, p.next, p.done)\nreplace(\"h.txt\", \"a\\naaaaaaaaaaa\\n\")\np = read_lines(\"h.txt\")\nprint(#p.lines, p.next, "
              "p.done)\nprint(attempt(rl, \"h.txt\", p.next).error)\n"),
            b);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("2\tbbbb\t10\tfalse\n1\tcccc\t15\ttrue\n1\t11\tfalse\n1\t2\tfalse\n`read_lines`: the line at offset 2 runs past 11 bytes, the most one call brings\n"));
    ran_free(&x);
}

TEST(edits_write_writes_every_file_or_none_and_refuses_one_named_twice) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    EXPECT(builtins_test_ran(&x, SRC_EDITS_WRITE, builtins_test_budgets(), OUT_EDITS_WRITE, REPORT_EDITS_WRITE));
    EXPECT(builtins_test_holds(&x, "a.txt", "A a2\n"));
    EXPECT(builtins_test_holds(&x, "b.txt", "B\n"));
    ran_free(&x);
}

TEST(a_yield_keeps_the_effects_log) {
    static Ran x;
    ran_init(&x, builtins_test_verbose());
    EXPECT(builtins_test_ran(&x, SRC_YIELD_KEEPS, builtins_test_budgets(), OUT_YIELD_KEEPS, REPORT_YIELD_KEEPS));
    EXPECT_EQ(x.r.collections, 3);
    EXPECT(builtins_test_absent(&x, "f2.txt"));
    ran_free(&x);
}

TEST(attack_a_change_through_a_read_only_mount_is_refused_before_it_is_written) {
    static Ran x;
    RanOpts o = {"ro = D/ro ro\n", true, false};
    ran_init(&x, o);
    ran_put(&x, "ro/a.txt", "a");
    EXPECT(builtins_test_ran(&x, SRC_RO_MOUNT, builtins_test_budgets(), OUT_RO_MOUNT, REPORT_RO_MOUNT));
    Str got = S("");
    EXPECT(ran_file(&x, "ro/a.txt", &got));
    EXPECT_STR(got, S("a"));
    EXPECT_EQ(x.r.changes, 1);
    ran_free(&x);
}

TEST(undo_on_stop_puts_back_a_file_changed_and_deletes_one_made) {
    static Ran x;
    RanOpts o = {NULL, true, true};
    ran_init(&x, o);
    ran_put(&x, "root/keep.txt", "old");
    EXPECT(builtins_test_ran(&x, SRC_UNDO, builtins_test_budgets(), OUT_UNDO, REPORT_UNDO));
    EXPECT_STR(x.undone, ran_cs(UNDONE_UNDO));
    EXPECT(builtins_test_holds(&x, "keep.txt", "old"));
    EXPECT(builtins_test_absent(&x, "made.txt"));
    ran_free(&x);
}

// The bytes `a` has handed out and keeps: every chunk, but the newest's room
// not yet bumped into.
static size_t builtins_test_arena_taken(const Arena *a) {
    if (a->chunk == NULL) return a->held;
    return a->held - (arena_chunk_len(a->chunk) - a->end);
}

// What a run of `n` creates through a mount (`exists`, then `replace`, as
// the Linux prelude's `create` makes one) leaves in the values' arena.
static size_t builtins_test_creates_hold(size_t n) {
    static Ran x;
    RanOpts o = {"w = D/w\n", false, false};
    ran_init(&x, o);
    ran_put(&x, "w/d/kept", "k");
    Buf src = buf_make(&x.host);
    if (buf_lit(&src, "local i = 0\nwhile i < ") != OK || buf_u64(&src, n) != OK ||
        buf_lit(&src, " do\n  i = i + 1\n  local p = \"w:d/f\" .. i\n  if exists(p) then fail(p) end\n  replace(p, \"x\")\nend\nprint(i)\n") != OK) {
        abort();
    }
    Budgets b = builtins_test_budgets();
    b.changes = 100000;
    ran_run(&x, buf_view(&src), b);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_EQ(x.r.changes, n);
    size_t taken = builtins_test_arena_taken(&x.values);
    ran_free(&x);
    return taken;
}

// A run of many changes holds about what Zig's does a change: a path made
// for the kernel lives on the stack, as Zig's posix layer has it, and a
// path built and kept (a join, a mount's path) holds its length. Here a
// create holds about 785 bytes (2026-11-18; about 930 before the case retry
// went, whose joins and listings a missed name paid for); with each join
// keeping its growth's slack it holds about 1,070, so the bound sits between,
// 115 above the one and 170 below the other. With each kernel path copied
// into the arena as well, a run of 20,000 creates once stopped on the 16 MiB
// arena at about 6,000, which Zig finishes.
TEST(many_creates_hold_under_900_bytes_of_the_arena_each) {
    size_t few = builtins_test_creates_hold(50);
    size_t many = builtins_test_creates_hold(550);
    size_t each = (many - few) / 500;
    EXPECT(each < 900);
}

TEST_PART(builtins, TEST_CASE(ls_stat_exists_and_is_dir_look_without_changing_and_name_a_missing_path), TEST_CASE(the_reads_whole_in_parts_by_lines_and_scans_across_a_chunks_edge),
          TEST_CASE(the_read_budgets_stop_a_read_before_its_bytes_are_taken), TEST_CASE(walk_sorts_filters_skips_resumes_and_names_its_mistakes),
          TEST_CASE(walk_in_parts_ends_each_with_more_and_keep_reads_reads_nothing), TEST_CASE(every_change_is_logged_with_its_outcome_a_failed_one_too),
          TEST_CASE(the_change_bytes_budget_stops_a_change_before_it_is_written), TEST_CASE(the_changes_budget_stops_the_change_past_it),
          TEST_CASE(edit_prints_its_diff_holds_word_and_all_and_says_where_old_parts), TEST_CASE(edit_says_a_diff_as_one_line_without_verbose),
          TEST_CASE(edit_word_holds_only_the_edges_where_old_ends_in_a_word_byte), TEST_CASE(read_lines_at_the_call_bytes_cap_ends_a_part_at_its_last_whole_line),
          TEST_CASE(edits_write_writes_every_file_or_none_and_refuses_one_named_twice), TEST_CASE(a_yield_keeps_the_effects_log),
          TEST_CASE(attack_a_change_through_a_read_only_mount_is_refused_before_it_is_written), TEST_CASE(undo_on_stop_puts_back_a_file_changed_and_deletes_one_made),
          TEST_CASE(many_creates_hold_under_900_bytes_of_the_arena_each));
