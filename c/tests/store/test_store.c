// The store's tests: zig-webbox's store tests that reach the calls Shale
// makes, with the same inputs and answers, then Wyhash's own vectors for the
// sibling name a replace writes, and the dirname and basename cases of Zig's
// standard library, which the store's must match.
#define _GNU_SOURCE
#include "test.h"

#include "store_fixture.c"

static Str rep(char c, size_t n, char *room) { // cstr: a buffer of at least n bytes
    memset(room, c, n);
    return (Str){room, n};
}

TEST(names_fat_holds_and_names_it_refuses) {
    char x96[96];
    char x97[97];
    const char *ok[] = {"a", "plan.md", "Plan-2.reactions.jsonl", "0f3a.png", "_session_secret", "a b"};
    SLICE_TYPE(Lits, const char *const);
    Lits oks = {ok, 6};
    for (size_t i = 0; i < oks.len; i++) EXPECT(store_fat_name(sx_cs(at(oks, i))));
    EXPECT(store_fat_name(rep('x', 96, x96)));
    const char *bad[] = {"", ".", "..", "what?", "a:b", "a/b", "a\\b", "trailing.", "trailing ", "caf\xc3\xa9", "tab\t"};
    Lits bads = {bad, 11};
    for (size_t i = 0; i < bads.len; i++) EXPECT(!store_fat_name(sx_cs(at(bads, i))));
    EXPECT(!store_fat_name(rep('x', 97, x97)));
}

TEST(write_read_append_read_at_stat_list_remove) {
    Sx f;
    sx_init(&f);
    Store *s = &f.store;
    Arena *a = &f.values;
    Str topic = sx_at(&f, "data/chat/1_2/sessions/topic.md");
    EXPECT_EQ(s->replace(s->ctx, a, topic, S("hello")), STORE_OK);
    uint64_t size = 0;
    EXPECT_EQ(s->append(s->ctx, a, topic, S(" world"), &size), STORE_OK);
    EXPECT_EQ(size, 11);
    char five[5];
    size_t got = 0;
    EXPECT_EQ(s->read_at(s->ctx, a, topic, 6, (Bytes){five, 5}, &got), STORE_OK);
    EXPECT_EQ(got, 5);
    EXPECT_STR(((Str){five, 5}), S("world"));
    StoreStat st;
    EXPECT_EQ(s->stat(s->ctx, a, topic, &st), STORE_OK);
    EXPECT_EQ(st.size, 11);
    EXPECT_EQ(s->stat(s->ctx, a, sx_at(&f, "data/chat/1_2"), &st), STORE_OK);
    EXPECT_EQ(st.kind, STORE_KIND_DIRECTORY);
    // append makes a file that is not there, and its folders.
    EXPECT_EQ(s->append(s->ctx, a, sx_at(&f, "data/new/x.log"), S("abc"), &size), STORE_OK);
    EXPECT_EQ(size, 3);
    StoreEntries es;
    EXPECT_EQ(s->list(s->ctx, a, sx_at(&f, "data/chat/1_2/sessions"), &es), STORE_OK);
    EXPECT_EQ(es.len, 1);
    EXPECT_STR(at(es, 0).name, S("topic.md"));
    EXPECT_EQ(at(es, 0).kind, STORE_KIND_FILE);
    EXPECT_EQ(s->list(s->ctx, a, sx_at(&f, "data/nowhere"), &es), STORE_OK);
    EXPECT_EQ(es.len, 0);
    EXPECT_EQ(s->stat(s->ctx, a, sx_at(&f, "data/nothing.md"), &st), STORE_FILE_NOT_FOUND);
    EXPECT_EQ(s->remove(s->ctx, a, topic), STORE_OK);
    // Gone already: fine.
    EXPECT_EQ(s->remove(s->ctx, a, topic), STORE_OK);
    bool has = true;
    EXPECT_EQ(s->has(s->ctx, a, topic, &has), STORE_OK);
    EXPECT(!has);
    EXPECT_EQ(s->remove_tree(s->ctx, a, sx_at(&f, "data/chat")), STORE_OK);
    EXPECT_EQ(s->has(s->ctx, a, sx_at(&f, "data/chat"), &has), STORE_OK);
    EXPECT(!has);
    EXPECT_EQ(s->has(s->ctx, a, sx_at(&f, "data/new/x.log"), &has), STORE_OK);
    EXPECT(has);
    sx_deinit(&f);
}

TEST(a_name_fat_refuses_is_refused_here_too_before_anything_is_made) {
    Sx f;
    sx_init(&f);
    Store *s = &f.store;
    Arena *a = &f.values;
    char x97[97];
    uint64_t size = 0;
    EXPECT_EQ(s->replace(s->ctx, a, sx_at(&f, "data/what?.md"), S("x")), STORE_BAD_NAME);
    Str long_name = S("");
    EXPECT(store_join(a, sx_at(&f, "data"), rep('x', 97, x97), &long_name));
    EXPECT_EQ(s->append(s->ctx, a, long_name, S("x"), &size), STORE_BAD_NAME);
    EXPECT_EQ(s->make_dir(s->ctx, a, sx_at(&f, "data/trailing./inner")), STORE_BAD_NAME);
    EXPECT_EQ(s->remove_tree(s->ctx, a, sx_at(&f, "data/users/   ")), STORE_BAD_NAME);
    bool has = true;
    EXPECT_EQ(s->has(s->ctx, a, sx_at(&f, "data/trailing."), &has), STORE_OK);
    EXPECT(!has);
    EXPECT(!sx_there(&f, "data"));
    sx_deinit(&f);
}

TEST(replace_makes_the_file_hold_exactly_the_data_keeps_its_name_and_leaves_no_sibling) {
    Sx f;
    sx_init(&f);
    Store *s = &f.store;
    Arena *a = &f.values;
    Str text = S("");
    // New: made, folders and all.
    EXPECT_EQ(s->replace(s->ctx, a, sx_at(&f, "data/players/next-id.txt"), S("4\n")), STORE_OK);
    EXPECT(sx_file(&f, "data/players/next-id.txt", &text));
    EXPECT_STR(text, S("4\n"));
    // Shorter than before: nothing of the old tail is left.
    EXPECT_EQ(s->replace(s->ctx, a, sx_at(&f, "data/players/next-id.txt"), S("5")), STORE_OK);
    EXPECT(sx_file(&f, "data/players/next-id.txt", &text));
    EXPECT_STR(text, S("5"));
    // In another case: the same file, in its first case.
    sx_put(&f, "data/chat/topic.count", "1 10\n");
    EXPECT_EQ(s->replace(s->ctx, a, sx_at(&f, "data/chat/TOPIC.count"), S("2 20\n")), STORE_OK);
    StoreEntries es;
    EXPECT_EQ(s->list(s->ctx, a, sx_at(&f, "data/chat"), &es), STORE_OK);
    EXPECT_EQ(es.len, 1);
    EXPECT_STR(at(es, 0).name, S("topic.count"));
    EXPECT(sx_file(&f, "data/chat/topic.count", &text));
    EXPECT_STR(text, S("2 20\n"));
    // A sibling a stop left behind is written over.
    Str sib = S("");
    EXPECT(store_sibling_name(a, S("topic.count"), &sib));
    Str left = S("");
    EXPECT(store_join(a, sx_at(&f, "data/chat"), sib, &left));
    EXPECT_EQ(store_os_write_file(left, S("stale"), 0644), STORE_OK);
    EXPECT_EQ(s->replace(s->ctx, a, sx_at(&f, "data/chat/topic.count"), S("3 30\n")), STORE_OK);
    EXPECT_EQ(s->list(s->ctx, a, sx_at(&f, "data/chat"), &es), STORE_OK);
    EXPECT_EQ(es.len, 1);
    EXPECT(sx_file(&f, "data/chat/topic.count", &text));
    EXPECT_STR(text, S("3 30\n"));
    // A name FAT cannot hold is refused before anything is written.
    EXPECT_EQ(s->replace(s->ctx, a, sx_at(&f, "data/what?"), S("x")), STORE_BAD_NAME);
    // One sibling name per file, in any case, and short enough for any name.
    Str one = S("");
    Str two = S("");
    EXPECT(store_sibling_name(a, S("Topic.count"), &one));
    EXPECT(store_sibling_name(a, S("topic.COUNT"), &two));
    EXPECT_STR(one, two);
    EXPECT(store_sibling_name(a, S("a.count"), &one));
    EXPECT(store_sibling_name(a, S("b.count"), &two));
    EXPECT(!str_eq(one, two));
    char x96[96];
    EXPECT(store_sibling_name(a, rep('x', 96, x96), &one));
    EXPECT_EQ(one.len, 13);
    EXPECT(store_fat_name(one));
    sx_deinit(&f);
}

TEST(a_path_the_bare_metal_host_could_not_hold_is_refused_measured_as_it_would_spell_it) {
    Sx f;
    sx_init(&f);
    Arena *a = &f.values;
    Str data = sx_at(&f, "home/someone/a-long-host-path/prod/data");
    Str auth = sx_at(&f, "home/someone/a-long-host-path/prod/auth");
    LinuxStore ls = {true, data, true, auth};
    Store s;
    store_linux(&ls, &s);
    char x[96];
    char y[96];
    char z[96];
    char q[70];
    // data/ + 245 bytes is 250: held. Spelled here it is far longer.
    Str near = data;
    EXPECT(store_join(a, near, rep('x', 90, x), &near) && store_join(a, near, rep('y', 90, y), &near) && store_join(a, near, rep('z', 63, z), &near));
    EXPECT_EQ(s.replace(s.ctx, a, near, S("ok")), STORE_OK);
    // data/ + 252 bytes is 257: refused, before anything is made.
    Str over = data;
    EXPECT(store_join(a, over, rep('x', 90, x), &over) && store_join(a, over, rep('y', 90, y), &over) && store_join(a, over, rep('q', 70, q), &over));
    uint64_t size = 0;
    EXPECT_EQ(s.replace(s.ctx, a, over, S("no")), STORE_PATH_TOO_LONG);
    EXPECT_EQ(s.append(s.ctx, a, over, S("no"), &size), STORE_PATH_TOO_LONG);
    EXPECT_EQ(s.make_dir(s.ctx, a, over), STORE_PATH_TOO_LONG);
    // 16 names from the volume's root (data and 15 more): held; 17: refused.
    Str deep = data;
    for (int k = 0; k < 15; k++) EXPECT(store_join(a, deep, S("d"), &deep));
    EXPECT_EQ(s.replace(s.ctx, a, deep, S("ok")), STORE_OK);
    Str deeper = S("");
    EXPECT(store_join(a, deep, S("d"), &deeper));
    EXPECT_EQ(s.replace(s.ctx, a, deeper, S("no")), STORE_PATH_TOO_DEEP);
    EXPECT_EQ(s.make_dir(s.ctx, a, deeper), STORE_PATH_TOO_DEEP);
    bool has = true;
    EXPECT_EQ(s.has(s.ctx, a, deeper, &has), STORE_OK);
    EXPECT(!has);
    // Under auth/ too; and not a path that only begins like a root.
    Str under_auth = auth;
    EXPECT(store_join(a, under_auth, rep('x', 96, x), &under_auth) && store_join(a, under_auth, rep('y', 96, y), &under_auth) && store_join(a, under_auth, rep('z', 96, z), &under_auth));
    EXPECT_EQ(s.replace(s.ctx, a, under_auth, S("no")), STORE_PATH_TOO_LONG);
    Str free_path = sx_at(&f, "home/someone/a-long-host-path/prod/data2");
    EXPECT(store_join(a, free_path, rep('x', 96, x), &free_path) && store_join(a, free_path, rep('y', 96, y), &free_path) && store_join(a, free_path, rep('z', 96, z), &free_path));
    EXPECT_EQ(s.replace(s.ctx, a, free_path, S("free")), STORE_OK);
    sx_deinit(&f);
}

TEST(nothing_there_is_null_and_something_there_that_will_not_read_is_an_error) {
    Sx f;
    sx_init(&f);
    Store *s = &f.store;
    Arena *a = &f.values;
    sx_put(&f, "a-file", "x");
    sx_mkdir(&f, "a-directory");
    const char *absent[] = {"not-here", "a-file/under-a-file"};
    SLICE_TYPE(Lits, const char *const);
    Lits ab = {absent, 2};
    for (size_t i = 0; i < ab.len; i++) {
        bool found = true;
        StoreStat st;
        EXPECT_EQ(s->stat_or_null(s->ctx, a, sx_at(&f, at(ab, i)), &found, &st), STORE_OK);
        EXPECT(!found);
    }
    bool found = false;
    StoreStat st;
    EXPECT_EQ(s->stat_or_null(s->ctx, a, sx_at(&f, "a-file"), &found, &st), STORE_OK);
    EXPECT(found);
    EXPECT_EQ(st.size, 1);
    // A folder read as a file is an error, not nothing.
    char one[1];
    size_t got = 0;
    EXPECT_EQ(s->read_at(s->ctx, a, sx_at(&f, "a-directory"), 0, (Bytes){one, 1}, &got), STORE_IS_DIR);
    sx_deinit(&f);
}

TEST(case_does_not_tell_two_names_apart_and_the_first_case_is_kept) {
    Sx f;
    sx_init(&f);
    Store *s = &f.store;
    Arena *a = &f.values;
    Str text = S("");
    EXPECT_EQ(s->replace(s->ctx, a, sx_at(&f, "data/chat/channels/Dev/sessions/plan.md"), S("one")), STORE_OK);
    char three[3];
    size_t got = 0;
    // Read in another case: the same file, as FAT finds it.
    EXPECT_EQ(s->read_at(s->ctx, a, sx_at(&f, "data/chat/channels/dev/sessions/PLAN.md"), 0, (Bytes){three, 3}, &got), STORE_OK);
    EXPECT_STR(((Str){three, got}), S("one"));
    bool has = false;
    EXPECT_EQ(s->has(s->ctx, a, sx_at(&f, "data/chat/CHANNELS/dev/Sessions/Plan.md"), &has), STORE_OK);
    EXPECT(has);
    // Write in another case: the same file again, kept in its first case.
    EXPECT_EQ(s->replace(s->ctx, a, sx_at(&f, "data/chat/channels/DEV/sessions/Plan.md"), S("two")), STORE_OK);
    uint64_t size = 0;
    EXPECT_EQ(s->append(s->ctx, a, sx_at(&f, "data/chat/channels/dev/sessions/PLAN.md"), S("!"), &size), STORE_OK);
    StoreEntries es;
    EXPECT_EQ(s->list(s->ctx, a, sx_at(&f, "data/chat/channels/dev/sessions"), &es), STORE_OK);
    EXPECT_EQ(es.len, 1);
    EXPECT_STR(at(es, 0).name, S("plan.md"));
    EXPECT(sx_file(&f, "data/chat/channels/Dev/sessions/plan.md", &text));
    EXPECT_STR(text, S("two!"));
    EXPECT_EQ(s->list(s->ctx, a, sx_at(&f, "data/chat/channels"), &es), STORE_OK);
    EXPECT_EQ(es.len, 1);
    EXPECT_STR(at(es, 0).name, S("Dev"));
    // A folder made in another case is the one already there.
    EXPECT_EQ(s->make_dir(s->ctx, a, sx_at(&f, "data/chat/channels/dEv/sessions/plan.uploads")), STORE_OK);
    EXPECT_EQ(s->list(s->ctx, a, sx_at(&f, "data/chat/channels"), &es), STORE_OK);
    EXPECT_EQ(es.len, 1);
    // Removal in another case removes it.
    EXPECT_EQ(s->remove(s->ctx, a, sx_at(&f, "data/chat/channels/dev/sessions/PLAN.md")), STORE_OK);
    EXPECT_EQ(s->has(s->ctx, a, sx_at(&f, "data/chat/channels/Dev/sessions/plan.md"), &has), STORE_OK);
    EXPECT(!has);
    EXPECT_EQ(s->remove_tree(s->ctx, a, sx_at(&f, "data/chat/channels/DEV")), STORE_OK);
    EXPECT_EQ(s->list(s->ctx, a, sx_at(&f, "data/chat/channels"), &es), STORE_OK);
    EXPECT_EQ(es.len, 0);
    sx_deinit(&f);
}

TEST(a_path_with_an_empty_part_is_refused_under_a_root_as_metal_refuses_it) {
    LinuxStore ls = {true, S("/srv/gopher/data/"), false, S("")};
    EXPECT_EQ(store_within_limits(&ls, S("/srv/gopher/data/chat/1_2/sessions/x.md")), STORE_OK);
    EXPECT_EQ(store_within_limits(&ls, S("/srv/gopher/data/chat//1_2/x.md")), STORE_BAD_NAME);
    EXPECT_EQ(store_within_limits(&ls, S("/srv/gopher/data/chat/")), STORE_BAD_NAME);
    EXPECT_EQ(store_within_limits(&ls, S("/srv/gopher/data//x")), STORE_BAD_NAME);
    // Outside the roots nothing is measured.
    EXPECT_EQ(store_within_limits(&ls, S("/tmp//x")), STORE_OK);
}

TEST(a_replace_keeps_the_file_s_mode_on_linux_an_executable_script_stays_one) {
    Sx f;
    sx_init(&f);
    sx_put(&f, "data/run.sh", "old");
    EXPECT_EQ(sx_chmod(&f, "data/run.sh", 0755), 0);
    EXPECT_EQ(f.store.replace(f.store.ctx, &f.values, sx_at(&f, "data/run.sh"), S("new")), STORE_OK);
    struct stat st;
    EXPECT_EQ(store_os_stat(sx_at(&f, "data/run.sh"), true, &st), STORE_OK);
    EXPECT_EQ(st.st_mode & 0777, 0755);
    Str text = S("");
    EXPECT(sx_file(&f, "data/run.sh", &text));
    EXPECT_STR(text, S("new"));
    sx_deinit(&f);
}

TEST(a_replace_onto_a_folder_fails_and_leaves_no_temporary_file_beside_it) {
    Sx f;
    sx_init(&f);
    sx_mkdir(&f, "data/x");
    EXPECT(f.store.replace(f.store.ctx, &f.values, sx_at(&f, "data/x"), S("d")) != STORE_OK);
    StoreEntries es;
    EXPECT_EQ(f.store.list(f.store.ctx, &f.values, sx_at(&f, "data"), &es), STORE_OK);
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
    Vectors all = {vs, 7};
    for (size_t i = 0; i < all.len; i++) EXPECT_EQ(store_wyhash(at(all, i).seed, sx_cs(at(all, i).input)), at(all, i).expected);
}

TEST(dirname_and_basename_take_trailing_slashes_and_the_root) {
    Str d = S("");
    EXPECT(store_dirname(S("/a/b/c"), &d));
    EXPECT_STR(d, S("/a/b"));
    EXPECT(store_dirname(S("/a/b/c///"), &d));
    EXPECT_STR(d, S("/a/b"));
    EXPECT(store_dirname(S("/a"), &d));
    EXPECT_STR(d, S("/"));
    EXPECT(!store_dirname(S("/"), &d));
    EXPECT(!store_dirname(S("//"), &d));
    EXPECT(!store_dirname(S(""), &d));
    EXPECT(!store_dirname(S("a"), &d));
    EXPECT(!store_dirname(S("a//"), &d));
    EXPECT_STR(store_basename(S("")), S(""));
    EXPECT_STR(store_basename(S("/")), S(""));
    EXPECT_STR(store_basename(S("/dir/basename.ext")), S("basename.ext"));
    EXPECT_STR(store_basename(S("/basename.ext")), S("basename.ext"));
    EXPECT_STR(store_basename(S("a/b/")), S("b"));
}

TEST(a_path_with_a_nul_inside_is_refused_before_any_call) {
    Sx f;
    sx_init(&f);
    sx_put(&f, "a", "x");
    Str p = sx_at(&f, "a");
    Buf b = buf_make(&f.values);
    EXPECT(buf_str(&b, p) == OK && buf_lit(&b, "\0/x") == OK);
    StoreStat st;
    EXPECT_EQ(f.store.stat(f.store.ctx, &f.values, buf_view(&b), &st), STORE_BAD_PATH_NAME);
    EXPECT_EQ(f.store.replace(f.store.ctx, &f.values, buf_view(&b), S("y")), STORE_BAD_PATH_NAME);
    sx_deinit(&f);
}

TEST(an_errno_is_named_by_its_error_name) {
    EXPECT_STR(store_err_name(store_err_of(ENOENT)), S("FileNotFound"));
    EXPECT_STR(store_err_name(store_err_of(ENOTDIR)), S("NotDir"));
    EXPECT_STR(store_err_name(store_err_of(EISDIR)), S("IsDir"));
    EXPECT_STR(store_err_name(store_err_of(EACCES)), S("AccessDenied"));
    EXPECT_STR(store_err_name(store_err_of(EPERM)), S("PermissionDenied"));
    EXPECT_STR(store_err_name(store_err_of(EEXIST)), S("PathAlreadyExists"));
    EXPECT_STR(store_err_name(store_err_of(ENOTEMPTY)), S("DirNotEmpty"));
    EXPECT_STR(store_err_name(store_err_of(ELOOP)), S("SymLinkLoop"));
    EXPECT_STR(store_err_name(store_err_of(EOVERFLOW)), S("Unexpected"));
    EXPECT_STR(store_err_name(STORE_OUT_OF_MEMORY), S("OutOfMemory"));
}

TEST_MAIN(
    TEST_CASE(names_fat_holds_and_names_it_refuses),
    TEST_CASE(write_read_append_read_at_stat_list_remove),
    TEST_CASE(a_name_fat_refuses_is_refused_here_too_before_anything_is_made),
    TEST_CASE(replace_makes_the_file_hold_exactly_the_data_keeps_its_name_and_leaves_no_sibling),
    TEST_CASE(a_path_the_bare_metal_host_could_not_hold_is_refused_measured_as_it_would_spell_it),
    TEST_CASE(nothing_there_is_null_and_something_there_that_will_not_read_is_an_error),
    TEST_CASE(case_does_not_tell_two_names_apart_and_the_first_case_is_kept),
    TEST_CASE(a_path_with_an_empty_part_is_refused_under_a_root_as_metal_refuses_it),
    TEST_CASE(a_replace_keeps_the_file_s_mode_on_linux_an_executable_script_stays_one),
    TEST_CASE(a_replace_onto_a_folder_fails_and_leaves_no_temporary_file_beside_it),
    TEST_CASE(wyhash_its_test_vectors),
    TEST_CASE(dirname_and_basename_take_trailing_slashes_and_the_root),
    TEST_CASE(a_path_with_a_nul_inside_is_refused_before_any_call),
    TEST_CASE(an_errno_is_named_by_its_error_name)
)
