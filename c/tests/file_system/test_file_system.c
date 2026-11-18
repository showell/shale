// The file system's tests: zig-webbox's file system tests that reach the calls
// Shale makes, with the same inputs and answers, then Wyhash's own vectors for
// the sibling name a replace writes, and the dirname and basename cases of
// Zig's standard library, which the file system's must match.
#define _GNU_SOURCE
#include "test.h"

#include "file_system_fixture.c"

static Str rep(char c, size_t n, char *room) { // cstr: a buffer of at least n bytes
    memset(room, c, n);
    return (Str){room, n};
}

TEST(strict_names_held_and_names_refused) {
    char x96[96];
    char x97[97];
    Lits oks = LITS("a", "plan.md", "Plan-2.reactions.jsonl", "0f3a.png", "_session_secret", "a b");
    for (size_t i = 0; i < oks.len; i++) EXPECT(path_strict_name(sx_cs(at(oks, i))));
    EXPECT(path_strict_name(rep('x', 96, x96)));
    Lits bads = LITS("", ".", "..", "what?", "a:b", "a/b", "a\\b", "trailing.", "trailing ", "caf\xc3\xa9", "tab\t");
    for (size_t i = 0; i < bads.len; i++) EXPECT(!path_strict_name(sx_cs(at(bads, i))));
    EXPECT(!path_strict_name(rep('x', 97, x97)));
}

TEST(write_read_append_read_at_stat_list_remove) {
    Sx f;
    sx_init(&f, &f.refused);
    const FileSystem *s = &f.file_system;
    Arena *a = &f.values;
    OsStr topic = sx_at(&f, "data/chat/1_2/sessions/topic.md");
    EXPECT_EQ(file_system_replace(s, a, topic, S("hello")), FILE_SYSTEM_OK);
    uint64_t size = 0;
    EXPECT_EQ(file_system_append(s, topic, S(" world"), &size), FILE_SYSTEM_OK);
    EXPECT_EQ(size, 11);
    char five[5];
    size_t got = 0;
    EXPECT_EQ(file_system_read_at(s, topic, 6, (Bytes){five, 5}, &got), FILE_SYSTEM_OK);
    EXPECT_EQ(got, 5);
    EXPECT_STR(((Str){five, 5}), S("world"));
    FileSystemStat st;
    EXPECT_EQ(file_system_stat(s, topic, &st), FILE_SYSTEM_OK);
    EXPECT_EQ(st.size, 11);
    EXPECT_EQ(file_system_stat(s, sx_at(&f, "data/chat/1_2"), &st), FILE_SYSTEM_OK);
    EXPECT_EQ(st.kind, FILE_SYSTEM_KIND_DIRECTORY);
    // append makes a file that is not there, and its folders.
    EXPECT_EQ(file_system_append(s, sx_at(&f, "data/new/x.log"), S("abc"), &size), FILE_SYSTEM_OK);
    EXPECT_EQ(size, 3);
    FileSystemEntries es;
    EXPECT_EQ(file_system_list(s, a, sx_at(&f, "data/chat/1_2/sessions"), &es), FILE_SYSTEM_OK);
    EXPECT_EQ(es.len, 1);
    EXPECT_STR(at(es, 0).name, S("topic.md"));
    EXPECT_EQ(at(es, 0).kind, FILE_SYSTEM_KIND_FILE);
    EXPECT_EQ(file_system_list(s, a, sx_at(&f, "data/nowhere"), &es), FILE_SYSTEM_OK);
    EXPECT_EQ(es.len, 0);
    EXPECT_EQ(file_system_stat(s, sx_at(&f, "data/nothing.md"), &st), FILE_SYSTEM_FILE_NOT_FOUND);
    EXPECT_EQ(file_system_remove(s, topic), FILE_SYSTEM_OK);
    // Gone already: fine.
    EXPECT_EQ(file_system_remove(s, topic), FILE_SYSTEM_OK);
    bool has = true;
    EXPECT_EQ(file_system_has(s, topic, &has), FILE_SYSTEM_OK);
    EXPECT(!has);
    EXPECT_EQ(file_system_remove_tree(s, a, sx_at(&f, "data/chat")), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_has(s, sx_at(&f, "data/chat"), &has), FILE_SYSTEM_OK);
    EXPECT(!has);
    EXPECT_EQ(file_system_has(s, sx_at(&f, "data/new/x.log"), &has), FILE_SYSTEM_OK);
    EXPECT(has);
    sx_deinit(&f);
}

TEST(a_name_that_is_not_strict_is_refused_before_anything_is_made) {
    Sx f;
    sx_init(&f, &f.refused);
    const FileSystem *s = &f.file_system;
    Arena *a = &f.values;
    char x97[97];
    uint64_t size = 0;
    EXPECT_EQ(file_system_replace(s, a, sx_at(&f, "data/what?.md"), S("x")), FILE_SYSTEM_BAD_NAME);
    OsStr long_name = sx_at(&f, "data");
    EXPECT(file_system_join(a, long_name, rep('x', 97, x97), &long_name));
    EXPECT_EQ(file_system_append(s, long_name, S("x"), &size), FILE_SYSTEM_BAD_NAME);
    EXPECT_EQ(file_system_make_dir(s, sx_at(&f, "data/trailing./inner")), FILE_SYSTEM_BAD_NAME);
    EXPECT_EQ(file_system_remove_tree(s, a, sx_at(&f, "data/users/   ")), FILE_SYSTEM_BAD_NAME);
    bool has = true;
    EXPECT_EQ(file_system_has(s, sx_at(&f, "data/trailing."), &has), FILE_SYSTEM_OK);
    EXPECT(!has);
    EXPECT(!sx_there(&f, "data"));
    sx_deinit(&f);
}

TEST(replace_makes_the_file_hold_exactly_the_data_keeps_its_name_and_leaves_no_sibling) {
    Sx f;
    sx_init(&f, &f.refused);
    const FileSystem *s = &f.file_system;
    Arena *a = &f.values;
    Str text = S("");
    // New: made, folders and all.
    EXPECT_EQ(file_system_replace(s, a, sx_at(&f, "data/players/next-id.txt"), S("4\n")), FILE_SYSTEM_OK);
    EXPECT(sx_file(&f, "data/players/next-id.txt", &text));
    EXPECT_STR(text, S("4\n"));
    // Shorter than before: nothing of the old tail is left.
    EXPECT_EQ(file_system_replace(s, a, sx_at(&f, "data/players/next-id.txt"), S("5")), FILE_SYSTEM_OK);
    EXPECT(sx_file(&f, "data/players/next-id.txt", &text));
    EXPECT_STR(text, S("5"));
    sx_put(&f, "data/chat/topic.count", "2 20\n");
    FileSystemEntries es;
    // A sibling a stop left behind is written over.
    Str sib = S("");
    EXPECT(path_temp_name(a, S("topic.count"), &sib));
    OsStr left = sx_at(&f, "data/chat");
    EXPECT(file_system_join(a, left, sib, &left));
    EXPECT_EQ(file_system_write_file(left, S("stale"), 0644), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_replace(s, a, sx_at(&f, "data/chat/topic.count"), S("3 30\n")), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_list(s, a, sx_at(&f, "data/chat"), &es), FILE_SYSTEM_OK);
    EXPECT_EQ(es.len, 1);
    EXPECT(sx_file(&f, "data/chat/topic.count", &text));
    EXPECT_STR(text, S("3 30\n"));
    // A name that is not strict is refused before anything is written.
    EXPECT_EQ(file_system_replace(s, a, sx_at(&f, "data/what?"), S("x")), FILE_SYSTEM_BAD_NAME);
    // One sibling name per file, in any case, and short enough for any name.
    Str one = S("");
    Str two = S("");
    EXPECT(path_temp_name(a, S("Topic.count"), &one));
    EXPECT(path_temp_name(a, S("topic.COUNT"), &two));
    EXPECT_STR(one, two);
    EXPECT(path_temp_name(a, S("a.count"), &one));
    EXPECT(path_temp_name(a, S("b.count"), &two));
    EXPECT(!str_eq(one, two));
    char x96[96];
    EXPECT(path_temp_name(a, rep('x', 96, x96), &one));
    EXPECT_EQ(one.len, 13);
    EXPECT(path_strict_name(one));
    sx_deinit(&f);
}

TEST(a_path_the_bare_metal_host_could_not_hold_is_refused_measured_as_it_would_spell_it) {
    Sx f;
    sx_init(&f, &f.refused);
    Arena *a = &f.values;
    OsStr data = sx_at(&f, "home/someone/a-long-host-path/prod/data");
    FileSystem ls = {true, data.s};
    char x[96];
    char y[96];
    char z[96];
    char q[70];
    // data/ + 245 bytes is 250: held. Spelled here it is far longer.
    OsStr near = data;
    EXPECT(file_system_join(a, near, rep('x', 90, x), &near) && file_system_join(a, near, rep('y', 90, y), &near) && file_system_join(a, near, rep('z', 63, z), &near));
    EXPECT_EQ(file_system_replace(&ls, a, near, S("ok")), FILE_SYSTEM_OK);
    // data/ + 252 bytes is 257: refused, before anything is made.
    OsStr over = data;
    EXPECT(file_system_join(a, over, rep('x', 90, x), &over) && file_system_join(a, over, rep('y', 90, y), &over) && file_system_join(a, over, rep('q', 70, q), &over));
    uint64_t size = 0;
    EXPECT_EQ(file_system_replace(&ls, a, over, S("no")), FILE_SYSTEM_PATH_TOO_LONG);
    EXPECT_EQ(file_system_append(&ls, over, S("no"), &size), FILE_SYSTEM_PATH_TOO_LONG);
    EXPECT_EQ(file_system_make_dir(&ls, over), FILE_SYSTEM_PATH_TOO_LONG);
    // 16 names from the volume's root (data and 15 more): held; 17: refused.
    OsStr deep = data;
    for (int k = 0; k < 15; k++) EXPECT(file_system_join(a, deep, S("d"), &deep));
    EXPECT_EQ(file_system_replace(&ls, a, deep, S("ok")), FILE_SYSTEM_OK);
    OsStr deeper = deep;
    EXPECT(file_system_join(a, deep, S("d"), &deeper));
    EXPECT_EQ(file_system_replace(&ls, a, deeper, S("no")), FILE_SYSTEM_PATH_TOO_DEEP);
    EXPECT_EQ(file_system_make_dir(&ls, deeper), FILE_SYSTEM_PATH_TOO_DEEP);
    bool has = true;
    EXPECT_EQ(file_system_has(&ls, deeper, &has), FILE_SYSTEM_OK);
    EXPECT(!has);
    // Not a path that only begins like the root.
    OsStr free_path = sx_at(&f, "home/someone/a-long-host-path/prod/data2");
    EXPECT(file_system_join(a, free_path, rep('x', 96, x), &free_path) && file_system_join(a, free_path, rep('y', 96, y), &free_path) && file_system_join(a, free_path, rep('z', 96, z), &free_path));
    EXPECT_EQ(file_system_replace(&ls, a, free_path, S("free")), FILE_SYSTEM_OK);
    sx_deinit(&f);
}

TEST(nothing_there_is_null_and_something_there_that_will_not_read_is_an_error) {
    Sx f;
    sx_init(&f, &f.refused);
    const FileSystem *s = &f.file_system;
    sx_put(&f, "a-file", "x");
    sx_mkdir(&f, "a-directory");
    Lits ab = LITS("not-here", "a-file/under-a-file");
    for (size_t i = 0; i < ab.len; i++) {
        bool found = true;
        FileSystemStat st;
        EXPECT_EQ(file_system_stat_or_null(s, sx_at(&f, at(ab, i)), &found, &st), FILE_SYSTEM_OK);
        EXPECT(!found);
    }
    bool found = false;
    FileSystemStat st;
    EXPECT_EQ(file_system_stat_or_null(s, sx_at(&f, "a-file"), &found, &st), FILE_SYSTEM_OK);
    EXPECT(found);
    EXPECT_EQ(st.size, 1);
    // A folder read as a file is an error, not nothing.
    char one[1];
    size_t got = 0;
    EXPECT_EQ(file_system_read_at(s, sx_at(&f, "a-directory"), 0, (Bytes){one, 1}, &got), FILE_SYSTEM_IS_DIR);
    sx_deinit(&f);
}

TEST(a_name_in_another_case_is_another_name_and_a_miss_is_not_found) {
    Sx f;
    sx_init(&f, &f.refused);
    const FileSystem *s = &f.file_system;
    Arena *a = &f.values;
    Str text = S("");
    EXPECT_EQ(file_system_replace(s, a, sx_at(&f, "data/chat/channels/Dev/sessions/plan.md"), S("one")), FILE_SYSTEM_OK);
    char three[3];
    size_t got = 0;
    // A read, a stat and a list in another case find nothing.
    EXPECT_EQ(file_system_read_at(s, sx_at(&f, "data/chat/channels/Dev/sessions/PLAN.md"), 0, (Bytes){three, 3}, &got), FILE_SYSTEM_FILE_NOT_FOUND);
    EXPECT_EQ(got, 0);
    bool has = true;
    EXPECT_EQ(file_system_has(s, sx_at(&f, "data/chat/CHANNELS/Dev/sessions/plan.md"), &has), FILE_SYSTEM_OK);
    EXPECT(!has);
    FileSystemStat st;
    EXPECT_EQ(file_system_stat(s, sx_at(&f, "data/chat/channels/dev"), &st), FILE_SYSTEM_FILE_NOT_FOUND);
    FileSystemEntries es;
    EXPECT_EQ(file_system_list(s, a, sx_at(&f, "data/chat/channels/dev/sessions"), &es), FILE_SYSTEM_OK);
    EXPECT_EQ(es.len, 0);
    // A write in another case makes another file, and the first is as it was.
    EXPECT_EQ(file_system_replace(s, a, sx_at(&f, "data/chat/channels/Dev/sessions/Plan.md"), S("two")), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_list(s, a, sx_at(&f, "data/chat/channels/Dev/sessions"), &es), FILE_SYSTEM_OK);
    EXPECT_EQ(es.len, 2);
    EXPECT(sx_file(&f, "data/chat/channels/Dev/sessions/plan.md", &text));
    EXPECT_STR(text, S("one"));
    EXPECT(sx_file(&f, "data/chat/channels/Dev/sessions/Plan.md", &text));
    EXPECT_STR(text, S("two"));
    // A removal in another case removes nothing.
    EXPECT_EQ(file_system_remove(s, sx_at(&f, "data/chat/channels/Dev/sessions/PLAN.md")), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_remove_tree(s, a, sx_at(&f, "data/chat/channels/DEV")), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_has(s, sx_at(&f, "data/chat/channels/Dev/sessions/plan.md"), &has), FILE_SYSTEM_OK);
    EXPECT(has);
    sx_deinit(&f);
}

TEST(a_path_with_an_empty_part_is_refused_under_a_root_as_metal_refuses_it) {
    FileSystem ls = {true, S("/srv/gopher/data/")};
    EXPECT_EQ(file_system_within_limits(&ls, S("/srv/gopher/data/chat/1_2/sessions/x.md")), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_within_limits(&ls, S("/srv/gopher/data/chat//1_2/x.md")), FILE_SYSTEM_BAD_NAME);
    EXPECT_EQ(file_system_within_limits(&ls, S("/srv/gopher/data/chat/")), FILE_SYSTEM_BAD_NAME);
    EXPECT_EQ(file_system_within_limits(&ls, S("/srv/gopher/data//x")), FILE_SYSTEM_BAD_NAME);
    // Outside the roots nothing is measured.
    EXPECT_EQ(file_system_within_limits(&ls, S("/tmp//x")), FILE_SYSTEM_OK);
}

TEST(a_replace_keeps_the_file_s_mode_on_linux_an_executable_script_stays_one) {
    Sx f;
    sx_init(&f, &f.refused);
    sx_put(&f, "data/run.sh", "old");
    EXPECT_EQ(sx_chmod(&f, "data/run.sh", 0755), 0);
    EXPECT_EQ(file_system_replace(&f.file_system, &f.values, sx_at(&f, "data/run.sh"), S("new")), FILE_SYSTEM_OK);
    struct stat st;
    EXPECT_EQ(file_system_stat_exact(sx_at(&f, "data/run.sh"), true, &st), FILE_SYSTEM_OK);
    EXPECT_EQ(st.st_mode & 0777, 0755);
    Str text = S("");
    EXPECT(sx_file(&f, "data/run.sh", &text));
    EXPECT_STR(text, S("new"));
    sx_deinit(&f);
}

TEST(a_replace_onto_a_folder_fails_and_leaves_no_temporary_file_beside_it) {
    Sx f;
    sx_init(&f, &f.refused);
    sx_mkdir(&f, "data/x");
    EXPECT(file_system_replace(&f.file_system, &f.values, sx_at(&f, "data/x"), S("d")) != FILE_SYSTEM_OK);
    FileSystemEntries es;
    EXPECT_EQ(file_system_list(&f.file_system, &f.values, sx_at(&f, "data"), &es), FILE_SYSTEM_OK);
    EXPECT_EQ(es.len, 1);
    for (size_t i = 0; i < es.len; i++) EXPECT(!str_starts_with(at(es, i).name, S("~")));
    sx_deinit(&f);
}

TEST(wyhash_its_test_vectors) {
    typedef struct {
        uint64_t seed;
        uint64_t expected;
        const char *input;
    } Vector;
    static const Vector vs[] = {
        {0, 0x409638ee2bde459ull, ""},
        {1, 0xa8412d091b5fe0a9ull, "a"},
        {2, 0x32dd92e4b2915153ull, "abc"},
        {3, 0x8619124089a3a16bull, "message digest"},
        {4, 0x7a43afb61d7f5f40ull, "abcdefghijklmnopqrstuvwxyz"},
        {5, 0xff42329b90e50d58ull, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"},
        {6, 0xc39cab13b115aad3ull, "12345678901234567890123456789012345678901234567890123456789012345678901234567890"},
    };
    SLICE_TYPE(Vectors, const Vector);
    Vectors all = {vs, sizeof vs / sizeof *vs};
    for (size_t i = 0; i < all.len; i++) EXPECT_EQ(wyhash(at(all, i).seed, sx_cs(at(all, i).input)), at(all, i).expected);
}

TEST(dirname_and_basename_take_trailing_slashes_and_the_root) {
    Str d = S("");
    EXPECT(path_dirname(S("/a/b/c"), &d));
    EXPECT_STR(d, S("/a/b"));
    EXPECT(path_dirname(S("/a/b/c///"), &d));
    EXPECT_STR(d, S("/a/b"));
    EXPECT(path_dirname(S("/a"), &d));
    EXPECT_STR(d, S("/"));
    EXPECT(!path_dirname(S("/"), &d));
    EXPECT(!path_dirname(S("//"), &d));
    EXPECT(!path_dirname(S(""), &d));
    EXPECT(!path_dirname(S("a"), &d));
    EXPECT(!path_dirname(S("a//"), &d));
    EXPECT_STR(path_basename(S("")), S(""));
    EXPECT_STR(path_basename(S("/")), S(""));
    EXPECT_STR(path_basename(S("/dir/basename.ext")), S("basename.ext"));
    EXPECT_STR(path_basename(S("/basename.ext")), S("basename.ext"));
    EXPECT_STR(path_basename(S("a/b/")), S("b"));
}

TEST(an_os_string_holds_no_nul_and_its_c_string_refuses_only_a_path_past_path_max) {
    OsStr o = os_str_own(S("x"));
    EXPECT(os_str_of(S("a/b"), &o));
    EXPECT_STR(o.s, S("a/b"));
    // A NUL anywhere is refused, and `*out` is left as it was.
    EXPECT(!os_str_of(((Str){"a\0/x", 4}), &o));
    EXPECT(!os_str_of(((Str){"\0", 1}), &o));
    EXPECT_STR(o.s, S("a/b"));
    // A NUL in Shale's own string is a bug in the C.
    EXPECT_DIES((void)os_str_own(((Str){"keys\0x", 6})));
    FileSystemCPath cp;
    const char *c = NULL;
    EXPECT_EQ(file_system_cstr(&cp, os_str_own(S("/a/b")), &c), FILE_SYSTEM_OK);
    EXPECT_STR(((Str){c, strlen(c)}), S("/a/b"));
    char big[PATH_MAX];
    EXPECT_EQ(file_system_cstr(&cp, os_str_own(rep('x', PATH_MAX, big)), &c), FILE_SYSTEM_PATH_TOO_LONG);
    // A path past PATH_MAX is too long, never "not there": it may name a
    // file that is. A part past the kernel's most is no file, so not there.
    FileSystem none = {false, S("")};
    bool found = true;
    FileSystemStat st;
    EXPECT_EQ(file_system_stat_or_null(&none, os_str_own(rep('x', PATH_MAX, big)), &found, &st), FILE_SYSTEM_PATH_TOO_LONG);
    EXPECT(!found);
    EXPECT_EQ(file_system_stat_or_null(&none, os_str_own(rep('x', 300, big)), &found, &st), FILE_SYSTEM_OK);
    EXPECT(!found);
}

TEST(an_errno_is_named_by_its_error_name) {
    EXPECT_STR(file_system_err_name(file_system_err_of(ENOENT)), S("FileNotFound"));
    EXPECT_STR(file_system_err_name(file_system_err_of(ENOTDIR)), S("NotDir"));
    EXPECT_STR(file_system_err_name(file_system_err_of(EISDIR)), S("IsDir"));
    EXPECT_STR(file_system_err_name(file_system_err_of(EACCES)), S("AccessDenied"));
    EXPECT_STR(file_system_err_name(file_system_err_of(EPERM)), S("PermissionDenied"));
    EXPECT_STR(file_system_err_name(file_system_err_of(EEXIST)), S("PathAlreadyExists"));
    EXPECT_STR(file_system_err_name(file_system_err_of(ENOTEMPTY)), S("DirNotEmpty"));
    EXPECT_STR(file_system_err_name(file_system_err_of(ELOOP)), S("SymLinkLoop"));
    EXPECT_STR(file_system_err_name(file_system_err_of(EOVERFLOW)), S("Unexpected"));
    EXPECT_STR(file_system_err_name(FILE_SYSTEM_OUT_OF_MEMORY), S("OutOfMemory"));
}

TEST_MAIN(
    TEST_CASE(strict_names_held_and_names_refused),
    TEST_CASE(write_read_append_read_at_stat_list_remove),
    TEST_CASE(a_name_that_is_not_strict_is_refused_before_anything_is_made),
    TEST_CASE(replace_makes_the_file_hold_exactly_the_data_keeps_its_name_and_leaves_no_sibling),
    TEST_CASE(a_path_the_bare_metal_host_could_not_hold_is_refused_measured_as_it_would_spell_it),
    TEST_CASE(nothing_there_is_null_and_something_there_that_will_not_read_is_an_error),
    TEST_CASE(a_name_in_another_case_is_another_name_and_a_miss_is_not_found),
    TEST_CASE(a_path_with_an_empty_part_is_refused_under_a_root_as_metal_refuses_it),
    TEST_CASE(a_replace_keeps_the_file_s_mode_on_linux_an_executable_script_stays_one),
    TEST_CASE(a_replace_onto_a_folder_fails_and_leaves_no_temporary_file_beside_it),
    TEST_CASE(wyhash_its_test_vectors),
    TEST_CASE(dirname_and_basename_take_trailing_slashes_and_the_root),
    TEST_CASE(an_os_string_holds_no_nul_and_its_c_string_refuses_only_a_path_past_path_max),
    TEST_CASE(an_errno_is_named_by_its_error_name)
)
