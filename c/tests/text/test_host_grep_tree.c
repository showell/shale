// test_host_grep_tree.c: c/host/host_grep_tree.c, the walk and the search
// behind the Linux prelude's `grep_tree`, called through `host` with the
// options the prelude gives it, on trees planted under the run's root: what
// is searched and what is passed by, a symbolic link, a planted secrets
// mount, the build folders and a hidden one, `ext`, `like`, `most`, either
// case, a pattern and `context`, and the reads and read bytes each costs.

// A part of ran_tests.c (`TEST_PART`, test.h), which defines _GNU_SOURCE and
// includes test.h and ran.c before it.

// `gt(needle, o)` is the host's call with the prelude's defaults, `o` over
// them; `show(g)` prints its hits, what it passed by, `files` and `cut`.
static const char *const GT_LIB =
    "function gt(needle, o)\n"
    "  local a = {place = \"\", prefix = \"\", entries = {}, regex = false, nocase = false, ext = {}, like = \"\", context = 0, most = 10000}\n"
    "  for k in keys(o) do a[k] = o[k] end\n"
    "  return host(\"grep_tree\", needle, a)\n"
    "end\n"
    "function show(g)\n"
    "  for h in g.hits do print(h.path .. \":\" .. h.line .. \": \" .. h.text) end\n"
    "  for p in g.passed do print(\"passed \" .. p.path .. \": \" .. p.why) end\n"
    "  print(g.files, g.cut)\n"
    "end\n";

// A run of GT_LIB and then `src` on the tree the test planted, `mounts` its
// mounts (each `D` the folder).
static void gt_init(Ran *x, const char *mounts) { // cstr: a test's literal
    RanOpts o = {mounts, false, false};
    ran_init(x, o);
}

static void gt_run(Ran *x, const char *src) { // cstr: a test's literal
    Buf b = buf_make(&x->host);
    if (buf_str(&b, ran_cs(GT_LIB)) != OK || buf_str(&b, ran_cs(src)) != OK) abort();
    ran_run(x, buf_view(&b), ran_budgets());
}

// Whether `hay` holds `needle`.
static bool gt_holds(Str hay, Str needle) {
    return mem_find(hay.ptr, hay.len, needle.ptr, needle.len) != SIZE_MAX;
}

TEST(grep_tree_searches_each_file_in_path_order_passing_by_a_binary_a_build_folder_and_a_hidden_one) {
    static Ran x;
    gt_init(&x, "");
    ran_put(&x, "root/a.txt", "one x\ntwo\nx three\n");
    ran_put(&x, "root/bin.dat", "x\0y\n");
    ran_write(&x, "root/bin.dat", (Str){"x\0y\n", 4});
    ran_put(&x, "root/empty.txt", "");
    ran_put(&x, "root/src/b.zig", "fn x() {}\n");
    ran_put(&x, "root/src/sub/c.md", "no\n");
    ran_put(&x, "root/zig-out/d.txt", "x\n");
    ran_put(&x, "root/.hidden/e.txt", "x\n");
    gt_run(&x, "show(gt(\"x\", {}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:1: one x\na.txt:3: x three\nsrc/b.zig:1: fn x() {}\npassed bin.dat: binary\n4\tfalse\n"));
    // The walk: the root's listing, a stat for each of the 9 entries kept
    // and a listing for each of the 3 folders entered (13); then a.txt,
    // src/b.zig and src/sub/c.md each a 4 KiB head and a grep, bin.dat its
    // head alone, and empty.txt a grep alone (8). zig-out/d.txt is stat'ed
    // and never read; .hidden is never listed.
    EXPECT_EQ(x.r.reads, 21);
    EXPECT_EQ(x.r.read_bytes, 2 * 18 + 4 + 2 * 10 + 2 * 3);
    ran_free(&x);
}

TEST(grep_tree_stops_at_a_planted_secrets_mount_s_file_reading_none_of_it_and_naming_no_folder) {
    static Ran x;
    gt_init(&x, "k = D/root/keys secrets\n");
    ran_put(&x, "root/a.txt", "x\n");
    ran_put(&x, "root/keys/tok", "x s3cret\n");
    gt_run(&x, "show(gt(\"x\", {}))\n");
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_REFUSED);
    EXPECT_STR(x.r.ctx.stop.message, S("\"keys/tok\" is in the mount `k`, which holds secrets"));
    EXPECT(!gt_holds(x.r.ctx.stop.message, x.dir));
    EXPECT_EQ(x.r.out.len, 0);
    // a.txt's head and grep, 2 bytes each: not a byte of the secret.
    EXPECT_EQ(x.r.read_bytes, 4);
    EXPECT_EQ(x.r.reads, 7);
    ran_free(&x);
}

TEST(grep_tree_refuses_to_walk_a_secrets_mount_by_its_name_before_any_read) {
    static Ran x;
    gt_init(&x, "k = D/root/keys secrets\n");
    ran_put(&x, "root/keys/tok", "x s3cret\n");
    gt_run(&x, "show(gt(\"x\", {place = \"k:\", prefix = \"k:\"}))\n");
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_REFUSED);
    EXPECT_STR(x.r.ctx.stop.message, S("\"k:\": the mount `k` holds secrets: only `run`'s `secrets` (or `http_get`'s `bearer`) may name its files"));
    EXPECT(!gt_holds(x.r.ctx.stop.message, x.dir));
    EXPECT_EQ(x.r.reads, 0);
    EXPECT_EQ(x.r.read_bytes, 0);
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(grep_tree_passes_by_a_symbolic_link_never_reading_what_it_points_at) {
    static Ran x;
    gt_init(&x, "");
    ran_put(&x, "root/a.txt", "x\n");
    ran_put(&x, "root/far.txt", "x far\n");
    StoreCPath cp;
    const char *link = NULL;
    if (store_cstr(&cp, ran_at(&x, "root/l.txt"), &link) != STORE_OK || symlink("far.txt", link) != 0) abort();
    gt_run(&x, "show(gt(\"x\", {ext = {\"txt\"}}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:1: x\nfar.txt:1: x far\npassed l.txt: a symbolic link, never followed\n2\tfalse\n"));
    // A link is never stat'ed or read: the walk's 3 reads, then 2 a file.
    EXPECT_EQ(x.r.reads, 7);
    EXPECT_EQ(x.r.read_bytes, 2 * 2 + 2 * 6);
    ran_free(&x);
}

TEST(grep_tree_leaves_out_the_build_folders_and_keeps_only_the_extensions_and_names_asked) {
    static Ran x;
    gt_init(&x, "");
    ran_put(&x, "root/node_modules/m.zig", "x\n");
    ran_put(&x, "root/zig-out/o.zig", "x\n");
    ran_put(&x, "root/zig-pkg/p.zig", "x\n");
    ran_put(&x, "root/src/a.md", "x\n");
    ran_put(&x, "root/src/a.zig", "x\n");
    ran_put(&x, "root/src/b_test.zig", "x\n");
    gt_run(&x, "show(gt(\"x\", {ext = {\"zig\"}}))\nshow(gt(\"x\", {like = \"*_test.zig\"}))\nshow(gt(\"x\", {place = \"src\", prefix = \"src/\", ext = {\"md\", \"zig\"}, like = \"a.*\"}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("src/a.zig:1: x\nsrc/b_test.zig:1: x\n2\tfalse\n"
                                     "src/b_test.zig:1: x\n1\tfalse\n"
                                     "src/a.md:1: x\nsrc/a.zig:1: x\n2\tfalse\n"));
    ran_free(&x);
}

TEST(grep_tree_cuts_the_hits_at_most_and_searches_no_file_after) {
    static Ran x;
    gt_init(&x, "");
    ran_put(&x, "root/a.txt", "x1\nx2\nx3\nx4\nx5\n");
    ran_put(&x, "root/b.txt", "x6\n");
    gt_run(&x, "show(gt(\"x\", {most = 3}))\nshow(gt(\"x\", {most = 0}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:1: x1\na.txt:2: x2\na.txt:3: x3\n1\ttrue\n1\ttrue\n"));
    // Each call walks the whole tree first (3 reads), as walk_all did, then
    // reads a.txt alone (2): b.txt is never read.
    EXPECT_EQ(x.r.reads, 10);
    EXPECT_EQ(x.r.read_bytes, 4 * 15);
    ran_free(&x);
}

TEST(grep_tree_finds_either_case_a_pattern_and_the_lines_around_a_hit) {
    static Ran x;
    gt_init(&x, "");
    ran_put(&x, "root/a.txt", "Alpha\nbeta X\ngamma\nx delta\n");
    gt_run(&x, "show(gt(\"X\", {nocase = true}))\nshow(gt(\"^x|^a\", {regex = true}))\nshow(gt(\"^A\", {regex = true, nocase = true}))\n"
               "for h in gt(\"gamma\", {context = 1}).hits do print(h.line, h.before[1].line, h.before[1].text, h.after[1].line, h.after[1].text) end\n"
               "print(#gt(\"Alpha\", {context = 2}).hits[1].before)\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:2: beta X\na.txt:4: x delta\n1\tfalse\n"
                                     "a.txt:4: x delta\n1\tfalse\n"
                                     "a.txt:1: Alpha\n1\tfalse\n"
                                     "3\t2\tbeta X\t4\tx delta\n"
                                     "0\n"));
    ran_free(&x);
}

TEST(grep_tree_charges_what_its_reads_bring_not_a_script_s_steps) {
    static Ran x;
    gt_init(&x, "");
    // 200 files of 1000 lines, 26 KB each, and no hit: the steps are the
    // bytes the reads bring, a step per 16, and a little more an entry.
    Buf body = buf_make(&x.host);
    for (int i = 0; i < 1000; i++) {
        if (buf_str(&body, S("a line of text, no needle\n")) != OK) abort();
    }
    char name[64];
    for (int f = 0; f < 200; f++) {
        int n = snprintf(name, sizeof name, "root/d/f%03d.txt", f);
        if (n <= 0) abort();
        ran_write(&x, name, buf_view(&body));
    }
    gt_run(&x, "show(gt(\"needle!\", {}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("200\tfalse\n"));
    EXPECT_EQ(x.r.reads, 3 + 200 + 200 * 2);
    EXPECT_EQ(x.r.read_bytes, 200 * (4096 + 26000));
    // 376,200 of them the read bytes' (a step per 16), the rest the walk's
    // listings and stats, each path's confinement and each entry's step.
    EXPECT_EQ(x.r.steps, 397733);
    ran_free(&x);
}

TEST_PART(host_grep_tree, TEST_CASE(grep_tree_searches_each_file_in_path_order_passing_by_a_binary_a_build_folder_and_a_hidden_one),
          TEST_CASE(grep_tree_stops_at_a_planted_secrets_mount_s_file_reading_none_of_it_and_naming_no_folder),
          TEST_CASE(grep_tree_refuses_to_walk_a_secrets_mount_by_its_name_before_any_read),
          TEST_CASE(grep_tree_passes_by_a_symbolic_link_never_reading_what_it_points_at),
          TEST_CASE(grep_tree_leaves_out_the_build_folders_and_keeps_only_the_extensions_and_names_asked),
          TEST_CASE(grep_tree_cuts_the_hits_at_most_and_searches_no_file_after),
          TEST_CASE(grep_tree_finds_either_case_a_pattern_and_the_lines_around_a_hit),
          TEST_CASE(grep_tree_charges_what_its_reads_bring_not_a_script_s_steps));
