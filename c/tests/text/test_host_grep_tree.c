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
static const char *const GREP_TREE_TEST_LIB =
    "function gt(needle, o)\n"
    "  local a = {given = {}, place = \"\", prefix = \"\", entries = {}, regex = false, nocase = false, ext = {}, like = \"\", context = 0, most = 10000}\n"
    "  for k in keys(o) do a[k] = o[k] end\n"
    "  return host(\"grep_tree\", needle, a)\n"
    "end\n"
    "function show(g)\n"
    "  for h in g.hits do print(h.path .. \":\" .. h.line .. \": \" .. h.text) end\n"
    "  for p in g.passed do print(\"passed \" .. p.path .. \": \" .. p.why) end\n"
    "  print(g.files, g.cut)\n"
    "end\n";

// A run of GREP_TREE_TEST_LIB and then `src` on the tree the test planted,
// `mounts` its mounts (each `D` the folder).
static void grep_tree_test_init(Ran *x, const char *mounts) { // cstr: a test's literal
    RanOpts o = {mounts, false, false};
    ran_init(x, o);
}

static void grep_tree_test_run(Ran *x, const char *src) { // cstr: a test's literal
    Buf b = buf_make(&x->host);
    if (buf_str(&b, ran_cs(GREP_TREE_TEST_LIB)) != OK || buf_str(&b, ran_cs(src)) != OK) abort();
    ran_run(x, buf_view(&b), ran_budgets());
}

// Whether `hay` holds `needle`.
static bool grep_tree_test_holds(Str hay, Str needle) {
    return mem_find(hay.ptr, hay.len, needle.ptr, needle.len) != SIZE_MAX;
}

TEST(grep_tree_searches_each_file_in_path_order_passing_by_a_binary_a_build_folder_and_a_hidden_one) {
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/a.txt", "one x\ntwo\nx three\n");
    ran_put(&x, "root/bin.dat", "x\0y\n");
    ran_write(&x, "root/bin.dat", (Str){"x\0y\n", 4});
    ran_put(&x, "root/empty.txt", "");
    ran_put(&x, "root/src/b.zig", "fn x() {}\n");
    ran_put(&x, "root/src/sub/c.md", "no\n");
    ran_put(&x, "root/zig-out/d.txt", "x\n");
    ran_put(&x, "root/.hidden/e.txt", "x\n");
    grep_tree_test_run(&x, "show(gt(\"x\", {}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:1: one x\na.txt:3: x three\nsrc/b.zig:1: fn x() {}\npassed bin.dat: binary\n4\tfalse\n"));
    // One pass: a listing each for the root, src and src/sub (3), and a
    // read each for a.txt, bin.dat, empty.txt, src/b.zig and src/sub/c.md
    // (5), each file's bytes once. zig-out and .hidden are never listed.
    EXPECT_EQ(x.r.reads, 8);
    EXPECT_EQ(x.r.read_bytes, 18 + 4 + 10 + 3);
    ran_free(&x);
}

TEST(grep_tree_leaves_out_a_planted_secrets_mount_s_folder_unlisted_unread_and_unnamed) {
    static Ran x;
    grep_tree_test_init(&x, "k = D/root/keys secrets\n");
    ran_put(&x, "root/a.txt", "x\n");
    ran_put(&x, "root/keys/tok", "x s3cret\n");
    ran_put(&x, "root/keys/sub/two", "x s3cret too\n");
    ran_put(&x, "root/z.txt", "x\n");
    grep_tree_test_run(&x, "local g = gt(\"x\", {})\nshow(g)\nprint(host(\"json_write\", g))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:1: x\nz.txt:1: x\n2\tfalse\n"
                                     "{\"hits\":[{\"path\":\"a.txt\",\"line\":1,\"text\":\"x\"},{\"path\":\"z.txt\",\"line\":1,\"text\":\"x\"}],\"files\":2,\"passed\":[],\"cut\":false}\n"));
    EXPECT(!grep_tree_test_holds(buf_view(&x.r.out), S("keys")));
    EXPECT(!grep_tree_test_holds(buf_view(&x.r.out), S("s3cret")));
    // The root's listing and a read each of a.txt and z.txt: keys is never
    // listed, and not a byte of either secret is read.
    EXPECT_EQ(x.r.reads, 3);
    EXPECT_EQ(x.r.read_bytes, 2 * 2);
    ran_free(&x);
}

TEST(grep_tree_leaves_out_two_planted_secrets_mounts_folders_each_unlisted_and_unread) {
    static Ran x;
    grep_tree_test_init(&x, "k = D/root/keys secrets\nv = D/root/deep/vault secrets\n");
    ran_put(&x, "root/a.txt", "x\n");
    ran_put(&x, "root/keys/tok", "x s3cret\n");
    ran_put(&x, "root/deep/b.txt", "x\n");
    ran_put(&x, "root/deep/vault/tok", "x s3cret\n");
    ran_put(&x, "root/deep/vault2/c.txt", "x\n");
    grep_tree_test_run(&x, "show(gt(\"x\", {}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:1: x\ndeep/b.txt:1: x\ndeep/vault2/c.txt:1: x\n3\tfalse\n"));
    EXPECT(!grep_tree_test_holds(buf_view(&x.r.out), S("s3cret")));
    // The root's, deep's and deep/vault2's listings and a read of each of
    // the three files: neither secrets folder is listed or read.
    EXPECT_EQ(x.r.reads, 6);
    ran_free(&x);
}

// The entries `walk` gives, a part at a time, as `walk_all` gathers them:
// each one's path, kind and size, and the `more` paths between the parts.
static const char *const GREP_TREE_TEST_WALK =
    "local after = \"\"\n"
    "while true do\n"
    "  local more = nil\n"
    "  for e in walk(\"\", {after = after, skip = \".*\"}) do\n"
    "    if e.kind == \"more\" then more = e.path else print(e.path, e.kind, e.size) end\n"
    "  end\n"
    "  if more == nil then break end\n"
    "  print(\"more \" .. more)\n"
    "  after = more\n"
    "end\n";

static void grep_tree_test_walk(Ran *x, uint64_t call_entries) {
    Budgets b = ran_budgets();
    b.call_entries = call_entries;
    ran_run(x, ran_cs(GREP_TREE_TEST_WALK), b);
}

TEST(walk_leaves_out_a_planted_secrets_mount_s_folder_whole_and_never_ends_a_part_on_it) {
    static Ran x;
    grep_tree_test_init(&x, "k = D/root/keys secrets\n");
    ran_put(&x, "root/a.txt", "a\n");
    ran_put(&x, "root/keys/tok", "s3cret\n");
    ran_put(&x, "root/keys/sub/two", "s3cret too\n");
    ran_put(&x, "root/z.txt", "zz\n");
    grep_tree_test_walk(&x, 256);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt\tfile\t2\nz.txt\tfile\t3\n"));
    // The root's listing, and a stat each for a.txt and z.txt.
    EXPECT_EQ(x.r.reads, 3);
    EXPECT_EQ(x.r.read_bytes, 0);
    ran_free(&x);
    // A part of one entry: a part that would end on keys ends on a.txt, and
    // the next resumes past keys without listing it.
    grep_tree_test_init(&x, "k = D/root/keys secrets\n");
    ran_put(&x, "root/a.txt", "a\n");
    ran_put(&x, "root/keys/tok", "s3cret\n");
    ran_put(&x, "root/z.txt", "zz\n");
    grep_tree_test_walk(&x, 1);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt\tfile\t2\nmore a.txt\nz.txt\tfile\t3\n"));
    EXPECT(!grep_tree_test_holds(buf_view(&x.r.out), S("keys")));
    // Each of the 2 parts lists the root once, and a.txt and z.txt are each
    // stat'ed once.
    EXPECT_EQ(x.r.reads, 4);
    ran_free(&x);
}

TEST(walk_resumes_past_a_folder_skip_passed_by_and_into_one_it_did_not) {
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/.h/x.txt", "x\n");
    ran_put(&x, "root/a.txt", "a\n");
    grep_tree_test_walk(&x, 1);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    // The first part ends on .h, which skip passed by: the next resumes
    // after it, never inside it.
    EXPECT_STR(buf_view(&x.r.out), S("more .h\na.txt\tfile\t2\n"));
    // Each part lists the root, and a.txt is stat'ed: .h is never listed.
    EXPECT_EQ(x.r.reads, 3);
    ran_free(&x);
    // The near miss: h is not hidden, so the part that ends on it resumes
    // inside it.
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/a.txt", "a\n");
    ran_put(&x, "root/h/x.txt", "x\n");
    grep_tree_test_walk(&x, 1);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt\tfile\t2\nmore a.txt\nh\tdirectory\t0\nmore h\nh/x.txt\tfile\t2\n"));
    // 3 parts' root listings, a stat each for a.txt, h and h/x.txt, and h
    // listed twice: as the second part enters it and as the third resumes.
    EXPECT_EQ(x.r.reads, 8);
    ran_free(&x);
}

TEST(grep_tree_stops_on_an_option_it_does_not_take_naming_those_it_does_before_any_read) {
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/a.txt", "x\n");
    grep_tree_test_run(&x, "show(gt(\"x\", {given = {most = 5, max = 60}}))\n");
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_FAIL);
    EXPECT_STR(x.r.ctx.stop.message, S("`grep_tree` takes `regex`, `nocase`, `ext`, `like`, `context`, `most`, `skip`, not `max`"));
    EXPECT_EQ(x.r.reads, 0);
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(grep_tree_refuses_to_walk_a_secrets_mount_by_its_name_before_any_read) {
    static Ran x;
    grep_tree_test_init(&x, "k = D/root/keys secrets\n");
    ran_put(&x, "root/keys/tok", "x s3cret\n");
    grep_tree_test_run(&x, "show(gt(\"x\", {place = \"k:\", prefix = \"k:\"}))\n");
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, CLASS_REFUSED);
    EXPECT_STR(x.r.ctx.stop.message, S("\"k:\": the mount `k` holds secrets: only `run`'s `secrets` and `secret_args` (or `http_get`'s `bearer`) may name its files"));
    EXPECT(!grep_tree_test_holds(x.r.ctx.stop.message, x.dir));
    EXPECT_EQ(x.r.reads, 0);
    EXPECT_EQ(x.r.read_bytes, 0);
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(grep_tree_passes_by_a_symbolic_link_never_reading_what_it_points_at) {
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/a.txt", "x\n");
    ran_put(&x, "root/far.txt", "x far\n");
    FileSystemCPath cp;
    const char *link = NULL;
    if (file_system_cstr(&cp, os_str_own(ran_at(&x, "root/l.txt")), &link) != FILE_SYSTEM_OK || symlink("far.txt", link) != 0) abort();
    grep_tree_test_run(&x, "show(gt(\"x\", {ext = {\"txt\"}}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:1: x\nfar.txt:1: x far\npassed l.txt: a symbolic link, never followed\n2\tfalse\n"));
    // A link is never read: the root's listing, then a read a file.
    EXPECT_EQ(x.r.reads, 3);
    EXPECT_EQ(x.r.read_bytes, 2 + 6);
    ran_free(&x);
}

TEST(grep_tree_leaves_out_the_build_folders_and_keeps_only_the_extensions_and_names_asked) {
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/node_modules/m.zig", "x\n");
    ran_put(&x, "root/zig-out/o.zig", "x\n");
    ran_put(&x, "root/zig-pkg/p.zig", "x\n");
    ran_put(&x, "root/src/a.md", "x\n");
    ran_put(&x, "root/src/a.zig", "x\n");
    ran_put(&x, "root/src/b_test.zig", "x\n");
    grep_tree_test_run(&x, "show(gt(\"x\", {ext = {\"zig\"}}))\nshow(gt(\"x\", {like = \"*_test.zig\"}))\nshow(gt(\"x\", {place = \"src\", prefix = \"src/\", ext = {\"md\", \"zig\"}, like = \"a.*\"}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("src/a.zig:1: x\nsrc/b_test.zig:1: x\n2\tfalse\n"
                                     "src/b_test.zig:1: x\n1\tfalse\n"
                                     "src/a.md:1: x\nsrc/a.zig:1: x\n2\tfalse\n"));
    ran_free(&x);
}

TEST(grep_tree_cuts_the_hits_at_most_and_searches_no_file_after) {
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/a.txt", "x1\nx2\nx3\nx4\nx5\n");
    ran_put(&x, "root/b.txt", "x6\n");
    grep_tree_test_run(&x, "show(gt(\"x\", {most = 3}))\nshow(gt(\"x\", {most = 0}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:1: x1\na.txt:2: x2\na.txt:3: x3\n1\ttrue\n1\ttrue\n"));
    // Each call lists the root and reads a.txt, where the cut ends the
    // walk: b.txt is never read.
    EXPECT_EQ(x.r.reads, 4);
    EXPECT_EQ(x.r.read_bytes, 2 * 15);
    ran_free(&x);
}

TEST(grep_tree_finds_either_case_a_pattern_and_the_lines_around_a_hit) {
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/a.txt", "Alpha\nbeta X\ngamma\nx delta\n");
    grep_tree_test_run(&x, "show(gt(\"X\", {nocase = true}))\nshow(gt(\"^x|^a\", {regex = true}))\nshow(gt(\"^A\", {regex = true, nocase = true}))\n"
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
    grep_tree_test_init(&x, "");
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
    grep_tree_test_run(&x, "show(gt(\"needle!\", {}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("200\tfalse\n"));
    // The root's and d's listings, and a read a file, its bytes once.
    EXPECT_EQ(x.r.reads, 2 + 200);
    EXPECT_EQ(x.r.read_bytes, 200 * 26000);
    // 325,000 of them the read bytes' (a step per 16), the rest the two
    // listings, each file's step and its path's confinement.
    EXPECT_EQ(x.r.steps, 343935);
    ran_free(&x);
}

TEST(grep_tree_leaves_out_each_folder_skip_names_and_passes_one_not_there) {
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/c/a.txt", "x a\n");
    ran_put(&x, "root/c/out/o.txt", "x o\n");
    ran_put(&x, "root/tests/golden/g.txt", "x g\n");
    ran_put(&x, "root/tests/golden2/h.txt", "x h\n");
    ran_put(&x, "root/tests/other.txt", "x t\n");
    grep_tree_test_run(&x, "show(gt(\"x\", {given = {skip = {\"tests/golden\", \"c/out\", \"c/gone\"}}}))\n"
               "show(gt(\"x\", {place = \"tests\", prefix = \"tests/\", given = {skip = {\"golden\"}}}))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    // tests/golden2 only begins with a skip's name: it is searched.
    EXPECT_STR(buf_view(&x.r.out), S("c/a.txt:1: x a\ntests/golden2/h.txt:1: x h\ntests/other.txt:1: x t\n3\tfalse\n"
                                     "tests/golden2/h.txt:1: x h\ntests/other.txt:1: x t\n2\tfalse\n"));
    // The first call: a stat for each skip (3, c/gone's finding nothing to
    // skip), listings of the root, c, tests and tests/golden2 (4) and the 3
    // files (3). The second: a stat for its skip, listings of tests and
    // tests/golden2, and 2 files.
    EXPECT_EQ(x.r.reads, 10 + 5);
    EXPECT_EQ(x.r.read_bytes, 3 * 4 + 2 * 4);
    ran_free(&x);
}

// The stop `skip` gives, run alone on a fresh tree with `c/a.txt`.
static void grep_tree_test_skip_stop(const char *opts, StopClass cls, Str message) { // cstr: a test's literal
    static Ran x;
    grep_tree_test_init(&x, "k = D/root/keys secrets\n");
    ran_put(&x, "root/c/a.txt", "x\n");
    ran_put(&x, "root/keys/tok", "s3cret\n");
    Buf b = buf_make(&x.host);
    if (buf_str(&b, S("show(gt(\"x\", ")) != OK || buf_str(&b, ran_cs(opts)) != OK || buf_str(&b, S("))\n")) != OK) abort();
    Buf src = buf_make(&x.host);
    if (buf_str(&src, buf_view(&b)) != OK || buf_byte(&src, 0) != OK) abort();
    grep_tree_test_run(&x, src.ptr);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, cls);
    EXPECT_STR(x.r.ctx.stop.message, message);
    EXPECT_EQ(x.r.out.len, 0);
    EXPECT_EQ(x.r.read_bytes, 0);
    ran_free(&x);
}

TEST(grep_tree_refuses_a_skip_that_is_not_a_folder_below_the_place_searched) {
    grep_tree_test_skip_stop("{given = {skip = {\"c/a.txt\"}}}", CLASS_ARGUMENT, S("`grep_tree`'s `skip` \"c/a.txt\": c/a.txt is not a folder"));
    grep_tree_test_skip_stop("{given = {skip = {\"/tmp\"}}}", CLASS_ARGUMENT, S("`grep_tree`'s `skip` \"/tmp\" is absolute: a skip is a folder's path below the place searched"));
    grep_tree_test_skip_stop("{given = {skip = {\"c/../c\"}}}", CLASS_ARGUMENT, S("`grep_tree`'s `skip` \"c/../c\" is not a path of names: a skip is a folder's path below the place searched, with no mount, empty part, `.` or `..`"));
    grep_tree_test_skip_stop("{given = {skip = {\"c//out\"}}}", CLASS_ARGUMENT, S("`grep_tree`'s `skip` \"c//out\" is not a path of names: a skip is a folder's path below the place searched, with no mount, empty part, `.` or `..`"));
    grep_tree_test_skip_stop("{given = {skip = {\"\"}}}", CLASS_ARGUMENT, S("`grep_tree`'s `skip` \"\" is not a path of names: a skip is a folder's path below the place searched, with no mount, empty part, `.` or `..`"));
    grep_tree_test_skip_stop("{given = {skip = {\"k:\"}}}", CLASS_ARGUMENT, S("`grep_tree`'s `skip` \"k:\" is not a path of names: a skip is a folder's path below the place searched, with no mount, empty part, `.` or `..`"));
    grep_tree_test_skip_stop("{given = {skip = \"c\"}}", CLASS_TYPE, S("`grep_tree`'s `skip` is a list of folders' paths below the place searched, not a string"));
    grep_tree_test_skip_stop("{given = {skip = {3}}}", CLASS_TYPE, S("`grep_tree`'s `skip` holds an integer: a folder's path is a string"));
    grep_tree_test_skip_stop("{given = {skip = {\"keys\"}}}", CLASS_REFUSED, S("\"keys\" is in the mount `k`, which holds secrets"));
    grep_tree_test_skip_stop("{place = \"c/a.txt\", entries = {{path = \"c/a.txt\", kind = \"file\", size = 2}}, given = {skip = {\"c\"}}}", CLASS_ARGUMENT,
                 S("`grep_tree`'s `skip` \"c\": the place searched, \"c/a.txt\", is a file, with no folder below it"));
}

TEST(grep_tree_walks_once_a_listing_a_folder_and_a_read_a_file_at_a_step_an_entry) {
    static Ran x;
    grep_tree_test_init(&x, "");
    // 3 levels of 4 folders and 5 files each (84 folders, 420 files), a hit
    // in each of the 320 deepest files (24 bytes) and none in the rest (25).
    char name[64];
    for (int a = 0; a < 4; a++) {
        for (int b = 0; b < 4; b++) {
            for (int c = 0; c < 4; c++) {
                for (int f = 0; f < 5; f++) {
                    int n = snprintf(name, sizeof name, "root/a%d/b%d/c%d/f%d.txt", a, b, c, f);
                    if (n <= 0) abort();
                    ran_put(&x, name, "a line of text, needle!\n");
                }
            }
            for (int f = 0; f < 5; f++) {
                int n = snprintf(name, sizeof name, "root/a%d/b%d/f%d.txt", a, b, f);
                if (n <= 0) abort();
                ran_put(&x, name, "a line of text, no hit..\n");
            }
        }
        for (int f = 0; f < 5; f++) {
            int n = snprintf(name, sizeof name, "root/a%d/f%d.txt", a, f);
            if (n <= 0) abort();
            ran_put(&x, name, "a line of text, no hit..\n");
        }
    }
    grep_tree_test_run(&x, "local g = gt(\"needle!\", {})\nprint(g.files, #g.hits, g.hits[1].path, g.hits[#g.hits].path)\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    EXPECT_STR(buf_view(&x.r.out), S("420\t320\ta0/b0/c0/f0.txt\ta3/b3/c3/f4.txt\n"));
    // The root and 84 folders listed once each, and 420 files read once.
    EXPECT_EQ(x.r.reads, 1 + 84 + 420);
    EXPECT_EQ(x.r.read_bytes, 320 * 24 + 100 * 25);
    // A step an entry (504), each listing's entries and sort bound, a step
    // per 16 bytes read, each file's path confined and each hit's text.
    EXPECT_EQ(x.r.steps, 29082);
    ran_free(&x);
}

TEST(grep_tree_finds_a_list_of_needles_in_one_walk_each_hit_naming_its_needle) {
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/a.txt", "one alpha\nbeta and alpha\nneither\n");
    ran_put(&x, "root/b/c.txt", "BETA\n");
    grep_tree_test_run(&x, "local g = gt({\"alpha\", \"beta\"}, {})\n"
               "for h in g.hits do print(h.path .. \":\" .. h.line .. \": \" .. h.needle .. \": \" .. h.text) end\n"
               "print(g.files, g.cut)\n"
               "for h in gt({\"beta\"}, {nocase = true}).hits do print(h.path, h.line, h.needle) end\n"
               "for h in gt({\"^one\", \"^B\"}, {regex = true}).hits do print(h.path, h.line, h.needle) end\n"
               "print(#gt({\"alpha\", \"beta\"}, {most = 2}).hits)\n"
               "print(has_key(gt(\"alpha\", {}).hits[1], \"needle\"))\n");
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    // A line holding two needles is a hit for each, in the needles' order.
    EXPECT_STR(buf_view(&x.r.out), S("a.txt:1: alpha: one alpha\na.txt:2: alpha: beta and alpha\na.txt:2: beta: beta and alpha\n2\tfalse\n"
                                     "a.txt\t2\tbeta\nb/c.txt\t1\tbeta\n"
                                     "a.txt\t1\t^one\nb/c.txt\t1\t^B\n"
                                     "2\n"
                                     "false\n"));
    // Each call lists the root and b once and reads each file once,
    // however many needles it has: 4 reads each, but the call that `most`
    // cuts in a.txt, which lists the root and reads a.txt alone.
    EXPECT_EQ(x.r.reads, 4 * 4 + 2);
    ran_free(&x);
}

// A file of 140 KB past a call-bytes cap of 100 KB, searched a part at a
// time (64 KiB, as `read_lines` brings them) for `needles`; gives the run's
// reads, and its output in `out`.
static uint64_t grep_tree_test_parts(const char *needles, Buf *out) { // cstr: a test's literal
    static Ran x;
    grep_tree_test_init(&x, "");
    Buf body = buf_make(&x.host);
    char line[32];
    for (int k = 1; k <= 20000; k++) {
        int n = snprintf(line, sizeof line, "l%05d%s%s\n", k, k % 2500 == 0 ? " alpha" : "", k % 4000 == 0 ? " beta" : "");
        if (n <= 0 || buf_str(&body, (Str){line, (size_t)n}) != OK) abort();
    }
    ran_put(&x, "root/big.txt", "");
    ran_write(&x, "root/big.txt", buf_view(&body));
    Buf src = buf_make(&x.host);
    if (buf_str(&src, ran_cs(GREP_TREE_TEST_LIB)) != OK || buf_lit(&src, "for h in gt(") != OK || buf_str(&src, ran_cs(needles)) != OK ||
        buf_lit(&src, ", {}).hits do print(h.line, h.needle) end\n") != OK) {
        abort();
    }
    Budgets b = ran_budgets();
    b.call_bytes = 100000;
    ran_run(&x, buf_view(&src), b);
    EXPECT_OK_IN(&x.r.ctx, x.ran);
    if (buf_str(out, buf_view(&x.r.out)) != OK) abort();
    uint64_t reads = x.r.reads;
    ran_free(&x);
    return reads;
}

TEST(grep_tree_searches_a_file_past_the_cap_for_each_needle_a_part_at_a_time_in_line_order) {
    bool refused = false;
    Arena a;
    arena_init(&a, 1 << 20, &refused);
    Buf two = buf_make(&a);
    Buf one = buf_make(&a);
    uint64_t reads_two = grep_tree_test_parts("{\"alpha\", \"beta\"}", &two);
    uint64_t reads_one = grep_tree_test_parts("{\"alpha\"}", &one);
    EXPECT_STR(buf_view(&two), S("2500\talpha\n4000\tbeta\n5000\talpha\n7500\talpha\n8000\tbeta\n10000\talpha\n12000\tbeta\n"
                                 "12500\talpha\n15000\talpha\n16000\tbeta\n17500\talpha\n20000\talpha\n20000\tbeta\n"));
    EXPECT_STR(buf_view(&one), S("2500\talpha\n5000\talpha\n7500\talpha\n10000\talpha\n12500\talpha\n15000\talpha\n17500\talpha\n20000\talpha\n"));
    // Each part is read once for both needles: a second costs no read.
    EXPECT_EQ(reads_two, reads_one);
    EXPECT(reads_one > 2);
    arena_deinit(&a);
}

// A run of `src`, which must stop with `cls` and `message` before any read.
static void grep_tree_test_needle_stop(const char *src, StopClass cls, Str message) { // cstr: a test's literal
    static Ran x;
    grep_tree_test_init(&x, "");
    ran_put(&x, "root/a.txt", "x\n");
    grep_tree_test_run(&x, src);
    EXPECT_EQ(x.ran, STOP);
    EXPECT_EQ(x.r.ctx.stop.cls, cls);
    EXPECT_STR(x.r.ctx.stop.message, message);
    EXPECT_EQ(x.r.reads, 0);
    EXPECT_EQ(x.r.out.len, 0);
    ran_free(&x);
}

TEST(grep_tree_refuses_a_needle_of_nothing_and_a_list_that_is_not_one_of_strings) {
    grep_tree_test_needle_stop("show(gt(\"\", {}))\n", CLASS_FAIL, S("`grep_tree` of \"\": every line holds it"));
    grep_tree_test_needle_stop("show(gt({\"x\", \"\"}, {}))\n", CLASS_FAIL, S("`grep_tree` of needle 2, \"\": every line holds it"));
    grep_tree_test_needle_stop("show(gt({}, {}))\n", CLASS_FAIL, S("`grep_tree` of an empty list: there is nothing to look for"));
    grep_tree_test_needle_stop("show(gt({x = \"y\"}, {}))\n", CLASS_TYPE, S("`grep_tree` looks for a string or a list of strings, not a map"));
    grep_tree_test_needle_stop("show(gt({\"x\", 3}, {}))\n", CLASS_TYPE, S("`grep_tree`'s needle 2 is an integer: a needle is a string"));
    grep_tree_test_needle_stop("show(gt(3, {}))\n", CLASS_TYPE, S("`grep_tree` looks for a string or a list of strings, not an integer"));
}

TEST_PART(host_grep_tree, TEST_CASE(grep_tree_searches_each_file_in_path_order_passing_by_a_binary_a_build_folder_and_a_hidden_one),
          TEST_CASE(grep_tree_leaves_out_a_planted_secrets_mount_s_folder_unlisted_unread_and_unnamed),
          TEST_CASE(grep_tree_leaves_out_two_planted_secrets_mounts_folders_each_unlisted_and_unread),
          TEST_CASE(walk_leaves_out_a_planted_secrets_mount_s_folder_whole_and_never_ends_a_part_on_it),
          TEST_CASE(walk_resumes_past_a_folder_skip_passed_by_and_into_one_it_did_not),
          TEST_CASE(grep_tree_stops_on_an_option_it_does_not_take_naming_those_it_does_before_any_read),
          TEST_CASE(grep_tree_refuses_to_walk_a_secrets_mount_by_its_name_before_any_read),
          TEST_CASE(grep_tree_passes_by_a_symbolic_link_never_reading_what_it_points_at),
          TEST_CASE(grep_tree_leaves_out_the_build_folders_and_keeps_only_the_extensions_and_names_asked),
          TEST_CASE(grep_tree_cuts_the_hits_at_most_and_searches_no_file_after),
          TEST_CASE(grep_tree_finds_either_case_a_pattern_and_the_lines_around_a_hit),
          TEST_CASE(grep_tree_charges_what_its_reads_bring_not_a_script_s_steps),
          TEST_CASE(grep_tree_leaves_out_each_folder_skip_names_and_passes_one_not_there),
          TEST_CASE(grep_tree_refuses_a_skip_that_is_not_a_folder_below_the_place_searched),
          TEST_CASE(grep_tree_walks_once_a_listing_a_folder_and_a_read_a_file_at_a_step_an_entry),
          TEST_CASE(grep_tree_finds_a_list_of_needles_in_one_walk_each_hit_naming_its_needle),
          TEST_CASE(grep_tree_searches_a_file_past_the_cap_for_each_needle_a_part_at_a_time_in_line_order),
          TEST_CASE(grep_tree_refuses_a_needle_of_nothing_and_a_list_that_is_not_one_of_strings));
