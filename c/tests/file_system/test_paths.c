// The path hook's tests: the mounts, the absolute-path stop, read-only
// mounts, the no-link rule, the secrets rule, the undo journal and the
// root's escapes, with Zig Shale's tests' inputs and answers. Where such a
// test runs a script, the C makes the calls the builtin would, in its order
// (fixture.c); what only the VM can run (a walk's entries, `edits_all`,
// `run` itself) is left to the tests that run scripts. Then the attacks the
// security auditor's list names, each planted.
#define _GNU_SOURCE
#include "test.h"

#include "fixture.c"

#define SAYS(lit) fixture_r(&f, lit)

// The plain stop's tail, for a path no mount may name.
#define PLAIN "is absolute: paths are relative to the data root, or a mount's, `name:path` (`shale --help` lists this box's mounts)"

// ── the root's own rules ────────────────────────────────────────────────

TEST(every_way_out_of_the_root_is_refused_before_the_file_system_is_asked) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_put(&f.base, "top.txt", "t");
    file_system_fixture_put(&f.base, "chat/a.md", "a");
    typedef struct {
        const char *who;
        const char *path;
        bool allow_root;
    } Escape;
    static const Escape escapes[] = {
        {"read", "../auth/secret", false}, {"read", "chat/../../auth/secret", false}, {"read", "/etc/passwd", false},
        {"read", "./top.txt", false}, {"read", "chat//a.md", false}, {"read", "chat/", false}, {"ls", "..", true},
        {"remove_tree", "", false}, {"remove", "", false}, {"replace", "", false}, {"mkdir", "", false},
        {"append", "../auth/secret", false}, {"remove_tree", "..", false}, {"remove", "../auth/secret", false},
        {"read", "", false}, {"read", "a\\b", false}, {"read", "chat/a.md:x", false},
    };
    SLICE_TYPE(Escapes, const Escape);
    Escapes all = {escapes, sizeof escapes / sizeof *escapes};
    for (size_t i = 0; i < all.len; i++) {
        fixture_again(&f);
        CheckedPath p;
        Status s = path_arg(&f.run, fixture_pos(), file_system_fixture_cs(at(all, i).who), file_system_fixture_cs(at(all, i).path), at(all, i).allow_root, &p);
        EXPECT(fixture_stopped(&f, s, CLASS_ARGUMENT, S("")));
    }
    // The texts of three of them, exactly.
    fixture_again(&f);
    CheckedPath p;
    EXPECT(fixture_said(&f, path_arg(&f.run, fixture_pos(), S("read"), S("../auth/secret"), false, &p), CLASS_ARGUMENT,
                   S("`read`: \"../auth/secret\" climbs out of the root with `..`: a folder beside it is reached through a mount, such as `repos:<name>/...` (`shale --help` lists this box's mounts)")));
    fixture_again(&f);
    EXPECT(fixture_said(&f, path_arg(&f.run, fixture_pos(), S("read"), S("a/./b"), false, &p), CLASS_ARGUMENT,
                   S("`read`: \"a/./b\" has a part (\".\") that is not a name: no `.`, `..`, empty parts or characters a strict name refuses")));
    fixture_again(&f);
    EXPECT(fixture_said(&f, path_arg(&f.run, fixture_pos(), S("mkdir"), S(""), false, &p), CLASS_ARGUMENT, S("`mkdir(\"\")` would act on the whole root: name a path of at least one part")));
    // With metal's hook, the absolute stop is the root's alone.
    PathRun bare = path_run_make(&f.ctx, f.base.root, fixture_charge, &f, &PATH_HOOK_NONE);
    fixture_again(&f);
    EXPECT(fixture_said(&f, path_arg(&bare, fixture_pos(), S("read"), S("/etc/passwd"), false, &p), CLASS_ARGUMENT, S("`read`: \"/etc/passwd\" is absolute: paths are relative to the data root")));
    // What is in the root is found, `.` is the root, and the steps are the
    // path's bytes.
    fixture_again(&f);
    uint64_t before = f.steps;
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("exists"), S("chat/a.md"), true, &p));
    EXPECT_STR(p.full.s, fixture_r(&f, "R/chat/a.md"));
    EXPECT_STR(p.rel, S("chat/a.md"));
    // Nine bytes, then a step for each of its two parts' lstat.
    EXPECT_EQ(f.steps - before, 11);
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("ls"), S("."), true, &p));
    EXPECT_STR(p.full.s, f.base.root);
    EXPECT_STR(p.rel, S(""));
    Str text = S("");
    EXPECT(file_system_fixture_file(&f.base, "top.txt", &text));
    EXPECT_STR(text, S("t"));
    fixture_deinit(&f);
}

// ── the mounts file ─────────────────────────────────────────────────────

TEST(mounts_a_mounts_paths_real_an_unknown_mount_or_a_missing_folder_named_the_files_mistakes_by_line) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "elsewhere/logs");
    Mounts ms;
    Str why = S("");
    EXPECT_EQ(mounts_parse(&f.host, fixture_r(&f, "# where things are\n\nruns = R/elsewhere\nhome = ~/\ngone = /no/such/folder\nmy-runs = R/elsewhere\n"), S("/home/someone"), &ms, &why), MOUNTS_OK);
    EXPECT_EQ(ms.len, 4);
    EXPECT_STR(at(ms, 1).dir, S("/home/someone"));
    // The filesystem's root is a folder too: its `/` is kept, not trimmed away.
    Mounts top;
    EXPECT_EQ(mounts_parse(&f.host, S("top = /\nslash = /tmp/\n"), S("/home/someone"), &top, &why), MOUNTS_OK);
    EXPECT_STR(at(top, 0).dir, S("/"));
    EXPECT_STR(at(top, 1).dir, S("/tmp"));
    f.paths.mounts = Mounts_items(&ms);
    EXPECT(paths_run_folders_make(&f.paths, &f.run));
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "replace", S("runs:logs/a.txt"), S("via the mount")));
    Str text = S("");
    EXPECT(file_system_fixture_file(&f.base, "elsewhere/logs/a.txt", &text));
    EXPECT_STR(text, S("via the mount"));
    CheckedPath p;
    fixture_again(&f);
    uint64_t before = f.steps;
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("read"), S("runs:logs/a.txt"), false, &p));
    EXPECT_STR(p.full.s, fixture_r(&f, "R/elsewhere/logs/a.txt"));
    EXPECT_STR(p.rel, S("runs:logs/a.txt"));
    // Its bytes, then a step for each part below the mount's folder.
    EXPECT_EQ(f.steps - before, 17);
    EXPECT_OK_IN(&f.ctx, fixture_look(&f, "exists", S("runs:")));
    Str real = S("");
    EXPECT_OK_IN(&f.ctx, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), S("runs:logs"), true, &real));
    EXPECT_STR(real, fixture_r(&f, "R/elsewhere/logs"));
    Str real_root = S("");
    EXPECT_OK_IN(&f.ctx, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), S(""), true, &real_root));
    EXPECT_STR(real_root, f.base.root);
    Str real_dot = S("");
    EXPECT_OK_IN(&f.ctx, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), S("."), true, &real_dot));
    EXPECT_STR(real_dot, real_root);
    // A mount's `.` is the mount, as the root's `.` is the root.
    Str m_dot = S("");
    Str m_bare = S("");
    EXPECT_OK_IN(&f.ctx, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), S("runs:."), true, &m_dot));
    EXPECT_OK_IN(&f.ctx, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), S("runs:"), true, &m_bare));
    EXPECT_STR(m_dot, m_bare);
    // A mount whose name has a `-` resolves as any other.
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("glob"), S("my-runs:logs"), true, &p));
    EXPECT_STR(p.full.s, fixture_r(&f, "R/elsewhere/logs"));
    EXPECT(fixture_stopped(&f, fixture_look(&f, "read", S("nope:x")), CLASS_ARGUMENT, S("the mounts are `runs`, `home`, `gone`")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("nope:x")), CLASS_ARGUMENT, S("\"nope:x\": `nope:` is not a mount: the mounts are `runs`, `home`, `gone`, `my-runs` (~/.config/shale/mounts)")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("gone:x")), CLASS_FILE_SYSTEM, S("the mount `gone` is /no/such/folder, which is not a folder there")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("runs:../x")), CLASS_ARGUMENT, S("\"runs:../x\" climbs out of its mount with `..`: a folder beside it is reached through a mount of its own (`shale --help` lists this box's mounts)")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("runs:/etc/passwd")), CLASS_ARGUMENT, S("\"runs:/etc/passwd\" is absolute inside its mount: a mount's paths are relative to it")));
    // No mounts at all: said so.
    MountsItems none = {NULL, 0};
    f.paths.mounts = none;
    EXPECT(paths_run_folders_make(&f.paths, &f.run));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("runs:x")), CLASS_ARGUMENT, S("\"runs:x\": `runs:` is not a mount: there are none (~/.config/shale/mounts)")));
    typedef struct {
        const char *text;
        const char *says;
    } Bad;
    static const Bad bad[] = {
        {"runs = relative/path\n", "line 1: `runs`'s value is relative: an absolute path, or one from `~/`"},
        {"a = /x\na = /y\n", "line 2: the mount `a` is named twice"},
        {"# c\njust words\n", "line 2: the line is not `name = value`"},
        {"bad name = /x\n", "line 1: the name before `=` is not a mount's name: letters, digits, `_` and `-`"},
    };
    SLICE_TYPE(Bads, const Bad);
    Bads bads = {bad, sizeof bad / sizeof *bad};
    for (size_t i = 0; i < bads.len; i++) {
        Mounts out;
        EXPECT_EQ(mounts_parse(&f.host, file_system_fixture_cs(at(bads, i).text), S("/home/x"), &out, &why), MOUNTS_MALFORMED);
        EXPECT_STR(why, file_system_fixture_cs(at(bads, i).says));
    }
    fixture_deinit(&f);
}

TEST(the_mounts_file_shale_mounts_pins_it_the_default_s_absence_is_no_mounts_a_pinned_one_s_a_mistake) {
    Fixture f;
    fixture_init(&f);
    Arena *a = &f.host;
    MountsLoaded got;
    Str why = S("");
    EXPECT_EQ(mounts_load(a, false, S(""), S("/no/such/home"), &got, &why), MOUNTS_OK);
    EXPECT_EQ(got.mounts.len, 0);
    EXPECT_STR(got.from, S("/no/such/home/.config/shale/mounts"));
    EXPECT_EQ(mounts_load(a, true, S("/no/such/mounts"), S("/h"), &got, &why), MOUNTS_MALFORMED);
    EXPECT_STR(why, S("SHALE_MOUNTS=/no/such/mounts: FileNotFound"));
    file_system_fixture_put(&f.base, "mounts", "runs = ~/runs\n");
    Str file = file_system_fixture_at(&f.base, "mounts").s;
    EXPECT_EQ(mounts_load(a, true, file, S("/h"), &got, &why), MOUNTS_OK);
    EXPECT_STR(at(got.mounts, 0).dir, S("/h/runs"));
    EXPECT_STR(got.from, file);
    file_system_fixture_put(&f.base, "mounts", "runs = runs\n");
    EXPECT_EQ(mounts_load(a, true, file, S("/h"), &got, &why), MOUNTS_MALFORMED);
    EXPECT(str_starts_with(why, file));
    Str tail = S(": line 1: `runs`'s value is relative: an absolute path, or one from `~/`");
    EXPECT(why.len >= tail.len && str_eq(str_sub(why, why.len - tail.len, why.len), tail));
    // A link in a mount's own path is resolved once, as the user's choice.
    file_system_fixture_mkdir(&f.base, "real-folder");
    fixture_link(&f, "real-folder", "linked");
    Buf text = buf_make(a);
    EXPECT(buf_lit(&text, "l = ") == OK && buf_str(&text, file_system_fixture_at(&f.base, "linked").s) == OK && buf_lit(&text, "\n") == OK);
    file_system_fixture_put(&f.base, "mounts", "");
    EXPECT_EQ(file_system_replace(&f.base.file_system, a, os_str_own(file), buf_view(&text)), FILE_SYSTEM_OK);
    EXPECT_EQ(mounts_load(a, true, file, S("/h"), &got, &why), MOUNTS_OK);
    EXPECT_STR(at(got.mounts, 0).dir, file_system_fixture_at(&f.base, "real-folder").s);
    // A folder that is missing keeps its spelling, for the run that uses it
    // to stop on; one that cannot be resolved for another cause is refused,
    // naming the mount and the cause, never the folder.
    file_system_fixture_mkdir(&f.base, "shut/inner");
    buf_clear(&text);
    EXPECT(buf_lit(&text, "gone = ") == OK && buf_str(&text, file_system_fixture_at(&f.base, "nowhere").s) == OK && buf_lit(&text, "\ns = ") == OK && buf_str(&text, file_system_fixture_at(&f.base, "shut/inner").s) == OK && buf_lit(&text, "\n") == OK);
    EXPECT_EQ(file_system_replace(&f.base.file_system, a, os_str_own(file), buf_view(&text)), FILE_SYSTEM_OK);
    EXPECT_EQ(file_system_fixture_chmod(&f.base, "shut", 0), 0);
    MountsResult shut = mounts_load(a, true, file, S("/h"), &got, &why);
    EXPECT_EQ(file_system_fixture_chmod(&f.base, "shut", 0755), 0);
    EXPECT_EQ(shut, MOUNTS_MALFORMED);
    EXPECT(str_starts_with(why, file));
    tail = S(": the mount `s`'s folder cannot be resolved: AccessDenied");
    EXPECT(why.len >= tail.len && str_eq(str_sub(why, why.len - tail.len, why.len), tail));
    EXPECT_EQ(mounts_load(a, true, file, S("/h"), &got, &why), MOUNTS_OK);
    EXPECT_STR(at(got.mounts, 0).dir, file_system_fixture_at(&f.base, "nowhere").s);
    // A NUL in a mount's path is refused where its line is known, not as
    // the arena's refusal it would read as at the kernel.
    Mounts ms;
    EXPECT_EQ(mounts_parse(a, ((Str){"ok = /x\nbad = /a\0b\n", 19}), S("/h"), &ms, &why), MOUNTS_MALFORMED);
    EXPECT_STR(why, S("line 2: `bad`'s path holds a NUL byte, which no path can carry"));
    fixture_deinit(&f);
}

TEST(ro_mounts_the_mounts_file_s_ro_and_tools_in_either_order_any_other_trailing_word_or_one_twice_refused_by_line) {
    bool refused = false;
    Arena a;
    arena_init(&a, (size_t)1 << 20, &refused);
    Mounts ms;
    Str why = S("");
    EXPECT_EQ(mounts_parse(&a, S("a = /x ro\nb = /y\nc = /z ro tools\nd = /w tools ro\ne = /v tools\n"), S("/home/x"), &ms, &why), MOUNTS_OK);
    EXPECT_EQ(at(ms, 0).access, MOUNT_READ_ONLY);
    EXPECT_EQ(at(ms, 1).access, MOUNT_READ_WRITE);
    EXPECT_STR(at(ms, 0).dir, S("/x"));
    static const bool tools[] = {false, false, true, true, true};
    SLICE_TYPE(Bools, const bool);
    Bools t = {tools, sizeof tools / sizeof *tools};
    EXPECT_EQ(ms.len, 5);
    for (size_t i = 0; i < t.len; i++) EXPECT_EQ(at(ms, i).tools, at(t, i));
    EXPECT_EQ(at(ms, 3).access, MOUNT_READ_ONLY);
    EXPECT_EQ(at(ms, 4).access, MOUNT_READ_WRITE);
    Mounts sec;
    EXPECT_EQ(mounts_parse(&a, S("k = /k secrets\n"), S("/home/x"), &sec, &why), MOUNTS_OK);
    EXPECT(at(sec, 0).secrets);
    EXPECT_EQ(at(sec, 0).access, MOUNT_READ_ONLY);
    typedef struct {
        const char *text;
        const char *says;
    } Bad;
    static const Bad bad[] = {
        {"a = /x rw\n", "line 1: only `ro`, `tools` and `secrets` may follow `a`'s path (and a path has no space)"},
        {"a = /x secrets tools\n", "line 1: `a` is `secrets` and `tools`: a toolchain's programs are not tokens"},
        {"a = /x secrets secrets\n", "line 1: `secrets` twice after `a`'s path"},
        {"a = /x y z\n", "line 1: only `ro`, `tools` and `secrets` may follow `a`'s path (and a path has no space)"},
        {"a = /x ro ro\n", "line 1: `ro` twice after `a`'s path"},
    };
    SLICE_TYPE(Bads, const Bad);
    Bads bads = {bad, sizeof bad / sizeof *bad};
    for (size_t i = 0; i < bads.len; i++) {
        Mounts out;
        EXPECT_EQ(mounts_parse(&a, file_system_fixture_cs(at(bads, i).text), S("/home/x"), &out, &why), MOUNTS_MALFORMED);
        EXPECT_STR(why, file_system_fixture_cs(at(bads, i).says));
    }
    arena_deinit(&a);
}

TEST(secrets_mounts_the_mounts_file_s_mistakes_name_the_line_and_the_mount_never_the_line_s_text_a_path_or_a_word_after_one) {
    bool refused = false;
    Arena a;
    arena_init(&a, (size_t)1 << 20, &refused);
    Str why = S("");
    // Each line holds a planted folder, `.planted-keys`, where a secrets
    // mount's would be, as in Zig's test of the same name.
    typedef struct {
        const char *text;
        const char *says;
    } Bad;
    static const Bad bad[] = {
        // No `=`: no mount can be named, so the line number alone.
        {"# c\n/home/x/.planted-keys secrets\n", "line 2: the line is not `name = value`"},
        // A path where the name goes: not a name, so not quoted.
        {"/home/x/.planted-keys = /y\n", "line 1: the name before `=` is not a mount's name: letters, digits, `_` and `-`"},
        // The name is judged before the words, so a bad one is never quoted
        // by a later message.
        {"~/.planted-keys = /y rw\n", "line 1: the name before `=` is not a mount's name: letters, digits, `_` and `-`"},
        {"~/.planted-keys = /y ro ro\n", "line 1: the name before `=` is not a mount's name: letters, digits, `_` and `-`"},
        // A path with a space: its tail is a word after the path.
        {"keys = /home/x/my .planted-keys secrets\n", "line 1: only `ro`, `tools` and `secrets` may follow `keys`'s path (and a path has no space)"},
        {"keys = home/x/.planted-keys secrets\n", "line 1: `keys`'s value is relative: an absolute path, or one from `~/`"},
        {"keys = /home/x/.planted-keys secrets secrets\n", "line 1: `secrets` twice after `keys`'s path"},
        {"keys = /home/x/.planted-keys secrets tools\n", "line 1: `keys` is `secrets` and `tools`: a toolchain's programs are not tokens"},
        {"keys = /home/x/.planted-keys secrets\nkeys = /home/x/.planted-keys secrets\n", "line 2: the mount `keys` is named twice"},
        // An ordinary mount's mistake still names its mount and its line.
        {"runs = /r\n\nwork = work/.planted-keys\n", "line 3: `work`'s value is relative: an absolute path, or one from `~/`"},
    };
    SLICE_TYPE(Bads, const Bad);
    Bads bads = {bad, sizeof bad / sizeof *bad};
    for (size_t i = 0; i < bads.len; i++) {
        Mounts out;
        EXPECT_EQ(mounts_parse(&a, file_system_fixture_cs(at(bads, i).text), S("/home/x"), &out, &why), MOUNTS_MALFORMED);
        EXPECT_STR(why, file_system_fixture_cs(at(bads, i).says));
        EXPECT(mem_find(why.ptr, why.len, "planted", strlen("planted")) == SIZE_MAX);
    }
    arena_deinit(&a);
}

// ── the absolute path's stop ────────────────────────────────────────────

TEST(an_absolute_path_s_stop_the_deepest_mount_or_the_root_that_holds_it_and_how_to_write_it_there) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "scr/deep");
    file_system_fixture_mkdir(&f.base, "scr/tasks");
    // Each order of the nested mounts, so neither the first nor the last to
    // match wins by its place in the file.
    Lits os = LITS("side = R-side\nouter = R/scr\ndeep = R/scr/deep\n", "deep = R/scr/deep\nouter = R/scr\nside = R-side\n");
    typedef struct {
        const char *who;
        const char *path;
        const char *says;
    } Case;
    static const Case cases[] = {
        // A mount outside the root, below its folder and the folder itself.
        {"read", "R-side/notes/a.txt", "`read`: \"R-side/notes/a.txt\" is absolute: it's under the mount `side:`, so write \"side:notes/a.txt\""},
        {"exists", "R-side", "`exists`: \"R-side\" is absolute: it's under the mount `side:`, so write \"side:.\""},
        // Nested mounts: the deeper one where it holds the path.
        {"read", "R/scr/tasks/x.output", "`read`: \"R/scr/tasks/x.output\" is absolute: it's under the mount `outer:`, so write \"outer:tasks/x.output\""},
        {"read", "R/scr/deep/x.txt", "`read`: \"R/scr/deep/x.txt\" is absolute: it's under the mount `deep:`, so write \"deep:x.txt\""},
        {"stat", "R/scr/deep/", "`stat`: \"R/scr/deep/\" is absolute: it's under the mount `deep:`, so write \"deep:.\""},
        // The root, below it and itself, where no mount inside it holds the
        // path.
        {"read", "R/src/a.zig", "`read`: \"R/src/a.zig\" is absolute: it's under the data root, so write \"src/a.zig\""},
        {"ls", "R", "`ls`: \"R\" is absolute: it's under the data root, so write \".\""},
        // A folder that only begins with a mount's folder's name is not in it.
        {"read", "R/scrx/a.txt", "`read`: \"R/scrx/a.txt\" is absolute: it's under the data root, so write \"scrx/a.txt\""},
        {"read", "R-sidex/a.txt", "`read`: \"R-sidex/a.txt\" " PLAIN},
        // Held by nothing.
        {"read", "/etc/passwd", "`read`: \"/etc/passwd\" " PLAIN},
        // No script's path: written there it would only stop again.
        {"read", "R/scr/../x", "`read`: \"R/scr/../x\" " PLAIN},
    };
    SLICE_TYPE(Cases, const Case);
    Cases cs = {cases, sizeof cases / sizeof *cases};
    for (size_t o = 0; o < os.len; o++) {
        fixture_mounts(&f, at(os, o));
        for (size_t i = 0; i < cs.len; i++) {
            EXPECT(fixture_said(&f, fixture_look(&f, at(cs, i).who, fixture_r(&f, at(cs, i).path)), CLASS_ARGUMENT, fixture_r(&f, at(cs, i).says)));
        }
    }
    // A change and a program's folder take the same stop, and nothing is
    // written.
    EXPECT(fixture_said(&f, fixture_change(&f, "replace", SAYS("R/scr/tasks/new.txt"), S("x")), CLASS_ARGUMENT, SAYS("`replace`: \"R/scr/tasks/new.txt\" is absolute: it's under the mount `outer:`, so write \"outer:tasks/new.txt\"")));
    EXPECT(!file_system_fixture_there(&f.base, "scr/tasks/new.txt"));
    // A host call's stop names the call the script made, never `host`.
    Str os_path = S("");
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_os_path(&f.paths, &f.run, fixture_pos(), S("run"), SAYS("R/scr/deep"), false, &os_path), CLASS_ARGUMENT, SAYS("`run`: \"R/scr/deep\" is absolute: it's under the mount `deep:`, so write \"deep:.\"")));
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), SAYS("R/scr/deep/x.zig"), true, &os_path), CLASS_ARGUMENT, SAYS("`real`: \"R/scr/deep/x.zig\" is absolute: it's under the mount `deep:`, so write \"deep:x.zig\"")));
    EXPECT(fixture_said(&f, fixture_look(&f, "zig_rename", S("/etc/a.zig")), CLASS_ARGUMENT, S("`zig_rename`: \"/etc/a.zig\" " PLAIN)));
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), SAYS("R/scr/../x"), true, &os_path), CLASS_ARGUMENT, SAYS("`real`: \"R/scr/../x\" " PLAIN)));
    fixture_deinit(&f);
}

TEST(an_absolute_path_s_stop_and_a_host_call_s_in_a_builtin_s_checks_the_call_is_named) {
    Fixture f;
    fixture_init(&f);
    // `zig_rename`'s path is checked by the core's rules, its name the
    // call's.
    EXPECT(fixture_stopped(&f, fixture_look(&f, "zig_rename", S("a/../b.zig")), CLASS_ARGUMENT, S("`zig_rename`: \"a/../b.zig\" climbs out of the root with `..`")));
    // The name is the caller's for each call: a later stop of `read` is
    // `read`'s.
    Str os_path = S("");
    EXPECT_OK_IN(&f.ctx, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), S(""), true, &os_path));
    EXPECT(fixture_stopped(&f, fixture_look(&f, "read", S("/etc/x")), CLASS_ARGUMENT, S("`read`: \"/etc/x\"")));
    fixture_deinit(&f);
}

TEST(an_absolute_path_s_stop_a_path_in_a_secrets_mount_s_folder_names_neither_the_mount_nor_its_folder) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "scr/keys");
    file_system_fixture_put(&f.base, "scr/keys/tok", "s3cret\n");
    fixture_mounts(&f, "outer = R/scr\nvault = R/scr/keys secrets\n");
    Lits ss = LITS("R/scr/keys/tok", "R/scr/keys");
    Lits ws = LITS("read", "exists");
    for (size_t i = 0; i < ss.len; i++) {
        Status s = fixture_look(&f, at(ws, i), fixture_r(&f, at(ss, i)));
        EXPECT(fixture_stopped(&f, s, CLASS_ARGUMENT, S(PLAIN)));
        Str m = f.ctx.stop.message;
        EXPECT(str_eq(str_sub(m, m.len - strlen(PLAIN), m.len), S(PLAIN)));
        EXPECT(mem_find(m.ptr, m.len, "vault", 5) == SIZE_MAX);
        EXPECT(mem_find(m.ptr, m.len, "outer", 5) == SIZE_MAX);
        EXPECT(mem_find(m.ptr, m.len, "so write", 8) == SIZE_MAX);
    }
    // Beside the secrets folder, the mount above it is named as ever.
    EXPECT(fixture_said(&f, fixture_look(&f, "read", SAYS("R/scr/notes.txt")), CLASS_ARGUMENT, SAYS("`read`: \"R/scr/notes.txt\" is absolute: it's under the mount `outer:`, so write \"outer:notes.txt\"")));
    fixture_deinit(&f);
}

// ── read-only mounts, links, a walk through a mount ─────────────────────

TEST(ro_mounts_every_change_through_one_is_refused_before_anything_is_written_a_read_works) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "frozen/sub");
    file_system_fixture_put(&f.base, "frozen/a.zig", "const x = 1;\n\nconst y = x;\n");
    file_system_fixture_put(&f.base, "mine.zig", "const z = 1;\n");
    fixture_mounts(&f, "ro = R/frozen ro\n");
    EXPECT_OK_IN(&f.ctx, fixture_look(&f, "read", S("ro:a.zig")));
    EXPECT_OK_IN(&f.ctx, fixture_look(&f, "exists", S("ro:sub")));
    EXPECT_OK_IN(&f.ctx, fixture_look(&f, "ls", S("ro:")));
    // Every change's path, through the mount and by the root into its folder.
    Lits ps = LITS("ro:a.zig", "ro:new.zig", "ro:d", "ro:sub", "frozen/a.zig", "frozen/sub/x");
    for (size_t i = 0; i < ps.len; i++) {
        EXPECT(fixture_stopped(&f, fixture_change(&f, "replace", file_system_fixture_cs(at(ps, i)), S("x")), CLASS_REFUSED, S("mount `ro` is read-only")));
        Str text = S("");
        EXPECT(file_system_fixture_file(&f.base, "frozen/a.zig", &text));
        EXPECT_STR(text, S("const x = 1;\n\nconst y = x;\n"));
        EXPECT(file_system_fixture_file(&f.base, "mine.zig", &text));
        EXPECT_STR(text, S("const z = 1;\n"));
    }
    EXPECT(fixture_said(&f, fixture_change(&f, "append", S("ro:a.zig"), S("x")), CLASS_REFUSED, S("ro:a.zig: mount `ro` is read-only")));
    EXPECT(!file_system_fixture_there(&f.base, "frozen/new.zig"));
    EXPECT(!file_system_fixture_there(&f.base, "frozen/d"));
    // A path beside it that only begins with its folder's name may change.
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "replace", S("frozenx.txt"), S("y")));
    fixture_deinit(&f);
}

TEST(ro_mounts_a_writable_mount_inside_a_read_only_one_decides_for_its_own_folder) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "all/open");
    file_system_fixture_put(&f.base, "all/a.txt", "a\n");
    fixture_mounts(&f, "all = R/all ro\nopen = R/all/open\n");
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "replace", S("open:b.txt"), S("b")));
    Str text = S("");
    EXPECT(file_system_fixture_file(&f.base, "all/open/b.txt", &text));
    EXPECT_STR(text, S("b"));
    EXPECT(fixture_stopped(&f, fixture_change(&f, "replace", S("all:open/b.txt"), S("x")), CLASS_REFUSED, S("mount `all` is read-only")));
    EXPECT(fixture_stopped(&f, fixture_change(&f, "replace", S("all:a.txt"), S("x")), CLASS_REFUSED, S("mount `all` is read-only")));
    // By the root: into the writable one's folder it may, beside it it may not.
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "replace", S("all/open/c.txt"), S("c")));
    EXPECT(fixture_stopped(&f, fixture_change(&f, "replace", S("all/a.txt"), S("x")), CLASS_REFUSED, S("all/a.txt: mount `all` is read-only")));
    EXPECT(file_system_fixture_file(&f.base, "all/a.txt", &text));
    EXPECT_STR(text, S("a\n"));
    EXPECT(file_system_fixture_file(&f.base, "all/open/b.txt", &text));
    EXPECT_STR(text, S("b"));
    fixture_deinit(&f);
}

TEST(a_walk_through_a_mount_its_paths_are_the_mounts_and_each_lands_where_the_mount_points) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "elsewhere/sub");
    file_system_fixture_put(&f.base, "elsewhere/a.zig", "x");
    file_system_fixture_put(&f.base, "elsewhere/sub/b.zig", "yy");
    fixture_mounts(&f, "m = R/elsewhere\n");
    CheckedPath p;
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("walk"), S("m:"), true, &p));
    EXPECT_STR(p.full.s, fixture_r(&f, "R/elsewhere"));
    EXPECT_STR(p.rel, S("m:"));
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("walk"), S("m:."), true, &p));
    EXPECT_STR(p.rel, S("m:"));
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("walk"), S("m:sub"), true, &p));
    EXPECT_STR(p.full.s, fixture_r(&f, "R/elsewhere/sub"));
    FileSystemEntries es;
    EXPECT_EQ(file_system_list(&f.base.file_system, &f.base.values, p.full, &es), FILE_SYSTEM_OK);
    EXPECT_EQ(es.len, 1);
    EXPECT_STR(at(es, 0).name, S("b.zig"));
    FileSystemStat st;
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("walk"), S("m:sub/b.zig"), true, &p));
    EXPECT_EQ(file_system_stat(&f.base.file_system, p.full, &st), FILE_SYSTEM_OK);
    EXPECT_EQ(st.size, 2);
    fixture_deinit(&f);
}

TEST(the_file_system_follows_no_link_to_a_file_to_a_folder_in_the_middle_in_a_mount_out_of_the_root) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "dir");
    file_system_fixture_mkdir(&f.base, "frozen");
    file_system_fixture_put(&f.base, "real.txt", "real");
    file_system_fixture_put(&f.base, "dir/inner.txt", "inner");
    fixture_link(&f, "real.txt", "link_file");
    fixture_link(&f, "dir", "link_dir");
    fixture_link(&f, "../real.txt", "dir/up");
    fixture_link(&f, "../real.txt", "frozen/up");
    fixture_link(&f, "/etc/hostname", "outside");
    fixture_mounts(&f, "m = R/dir\nro = R/frozen ro\n");
    typedef struct {
        const char *who;
        const char *path;
        const char *says;
    } Case;
    static const Case cases[] = {
        {"read", "link_file", "link_file is a symbolic link"}, {"read", "link_dir/inner.txt", "link_dir is a symbolic link"},
        {"exists", "link_file", "is a symbolic link"}, {"stat", "link_dir", "is a symbolic link"}, {"ls", "link_dir", "is a symbolic link"},
        {"read", "m:up", "is a symbolic link"}, {"read", "ro:up", "is a symbolic link"}, {"read", "outside", "outside is a symbolic link"},
    };
    SLICE_TYPE(Cases, const Case);
    Cases cs = {cases, sizeof cases / sizeof *cases};
    for (size_t i = 0; i < cs.len; i++) EXPECT(fixture_stopped(&f, fixture_look(&f, at(cs, i).who, file_system_fixture_cs(at(cs, i).path)), CLASS_REFUSED, file_system_fixture_cs(at(cs, i).says)));
    EXPECT(fixture_stopped(&f, fixture_change(&f, "replace", S("link_file"), S("x")), CLASS_REFUSED, S("is a symbolic link")));
    // Its exact words, a mount's path spelled as the script wrote it.
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("link_dir/inner.txt")), CLASS_REFUSED, S("link_dir is a symbolic link: the file system follows none")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("m:up")), CLASS_REFUSED, S("m:up is a symbolic link: the file system follows none")));
    Str text = S("");
    EXPECT(file_system_fixture_file(&f.base, "real.txt", &text));
    EXPECT_STR(text, S("real"));
    // What is no link reads as ever.
    EXPECT_OK_IN(&f.ctx, fixture_look(&f, "read", S("real.txt")));
    EXPECT_OK_IN(&f.ctx, fixture_look(&f, "read", S("m:inner.txt")));
    EXPECT_OK_IN(&f.ctx, fixture_look(&f, "ls", S("")));
    fixture_deinit(&f);
}

// ── the secrets rule ────────────────────────────────────────────────────

// Whether the stop the run made names the secrets folder `keys` under the root.
static bool fixture_names_keys(Fixture *f) {
    Str dir = file_system_fixture_at(&f->base, "keys").s;
    Str m = f->ctx.stop.message;
    return mem_find(m.ptr, m.len, dir.ptr, dir.len) != SIZE_MAX;
}

TEST(secrets_mounts_no_read_listing_change_path_or_argument_reaches_a_token_by_the_mount_or_another_way) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "keys");
    file_system_fixture_put(&f.base, "keys/tok", "s3cret\n");
    fixture_link(&f, "keys", "alias");
    fixture_link(&f, ".", "up");
    fixture_mounts(&f, "k = R/keys secrets\nall = R\n");
    EXPECT_EQ(at(f.paths.mounts, 0).access, MOUNT_READ_ONLY);
    typedef struct {
        const char *who;
        const char *path;
        const char *says;
    } Case;
    static const Case looks[] = {
        {"read", "k:tok", "the mount `k` holds secrets"}, {"ls", "k:", "the mount `k` holds secrets"},
        {"exists", "k:tok", "the mount `k` holds secrets"}, {"stat", "k:tok", "the mount `k` holds secrets"},
        // The same folder by the root, or by a mount above it.
        {"read", "keys/tok", "is in the mount `k`, which holds secrets"}, {"ls", "keys", "is in the mount `k`, which holds secrets"},
        {"read", "all:keys/tok", "is in the mount `k`, which holds secrets"},
        // Through a link to it: the link is refused first.
        {"read", "alias/tok", "alias is a symbolic link"},
    };
    SLICE_TYPE(Cases, const Case);
    Cases ls = {looks, sizeof looks / sizeof *looks};
    for (size_t i = 0; i < ls.len; i++) {
        EXPECT(fixture_stopped(&f, fixture_look(&f, at(ls, i).who, file_system_fixture_cs(at(ls, i).path)), CLASS_REFUSED, file_system_fixture_cs(at(ls, i).says)));
        EXPECT(!fixture_names_keys(&f));
    }
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("k:tok")), CLASS_REFUSED, S("\"k:tok\": the mount `k` holds secrets: only `run`'s `secrets` and `secret_args` (or `http_get`'s `bearer`) may name its files")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("keys/tok")), CLASS_REFUSED, S("\"keys/tok\" is in the mount `k`, which holds secrets")));
    Str os = S("");
    fixture_again(&f);
    EXPECT(fixture_stopped(&f, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), S("k:tok"), true, &os), CLASS_REFUSED, S("the mount `k` holds secrets")));
    EXPECT(fixture_stopped(&f, fixture_change(&f, "replace", S("k:tok"), S("x")), CLASS_REFUSED, S("the mount `k` holds secrets")));
    // A program started in it.
    fixture_again(&f);
    EXPECT(fixture_stopped(&f, paths_os_path(&f.paths, &f.run, fixture_pos(), S("run"), S("k:"), false, &os), CLASS_REFUSED, S("the mount `k` holds secrets")));
    fixture_again(&f);
    EXPECT(fixture_stopped(&f, paths_os_path(&f.paths, &f.run, fixture_pos(), S("run"), S("all:keys"), false, &os), CLASS_REFUSED, S("the mount `k`")));
    // A program handed its path, by any spelling: whole, relative to the
    // program's folder, with `.` or `..`, after an option's `=`, through a
    // link, in `env`, or to a job; a folder that holds it; a link to one.
    Str argv_real = fixture_r(&f, "R/keys/tok");
    typedef struct {
        const char *who;
        Str arg;
        const char *says;
    } ArgCase;
    ArgCase args[] = {
        {"run", argv_real, "`run`'s argument 4 names the mount `k`'s folder, which holds secrets: hand one over as `secrets`"},
        {"run", S("keys/tok"), "`run`'s argument 4 names the mount `k`'s folder"},
        {"run", S("./keys/../keys/tok"), "`run`'s argument 4 names the mount `k`'s folder"},
        {"run", S("--file=keys/tok"), "`run`'s argument 4 names the mount `k`'s folder"},
        {"run", S("alias/tok"), "`run`'s argument 4 names the mount `k`'s folder"},
        {"job_start", S("keys/tok"), "`job_start`'s argument 4 names the mount `k`'s folder"},
        {"run", S("."), "`run`'s argument 4 holds the mount `k`'s folder, which holds secrets: name a folder beside it"},
        {"run", f.base.root, "`run`'s argument 4 holds the mount `k`'s folder"},
        {"run", S("up"), "`run`'s argument 4 holds the mount `k`'s folder"},
    };
    SLICE_TYPE(ArgCases, ArgCase);
    ArgCases as = {args, sizeof args / sizeof *args};
    PathsStrs none = {NULL, 0};
    for (size_t i = 0; i < as.len; i++) {
        const Str argv_items[4] = {S("shale-fixture"), S("lines"), S("1"), at(as, i).arg};
        PathsStrs argv = {argv_items, 4};
        fixture_again(&f);
        Status s = paths_refuse_secret_args(&f.paths, &f.run, fixture_pos(), file_system_fixture_cs(at(as, i).who), argv, f.base.root, none, none);
        EXPECT(fixture_stopped(&f, s, CLASS_REFUSED, file_system_fixture_cs(at(as, i).says)));
        EXPECT(!fixture_names_keys(&f));
    }
    const Str env_k[1] = {S("F")};
    const Str env_v[1] = {S("keys/tok")};
    PathsStrs ek = {env_k, 1};
    PathsStrs ev = {env_v, 1};
    const Str plain_argv[2] = {S("shale-fixture"), S("env")};
    PathsStrs pa = {plain_argv, 2};
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_refuse_secret_args(&f.paths, &f.run, fixture_pos(), S("run"), pa, f.base.root, ek, ev), CLASS_REFUSED,
                   S("`run`'s `env` F names the mount `k`'s folder, which holds secrets: hand one over as `secrets`")));
    // A program handed nothing of it runs.
    fixture_again(&f);
    EXPECT_OK_IN(&f.ctx, paths_refuse_secret_args(&f.paths, &f.run, fixture_pos(), S("run"), pa, f.base.root, none, none));
    Str text = S("");
    EXPECT(file_system_fixture_file(&f.base, "keys/tok", &text));
    EXPECT_STR(text, S("s3cret\n"));
    // `secrets` takes one; the mount itself is no secret.
    Str secret = S("");
    fixture_again(&f);
    EXPECT_OK_IN(&f.ctx, paths_secret_path(&f.paths, &f.run, fixture_pos(), S("run"), S("k:tok"), &secret));
    EXPECT_STR(secret, fixture_r(&f, "R/keys/tok"));
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_secret_path(&f.paths, &f.run, fixture_pos(), S("run"), S("k:"), &secret), CLASS_ARGUMENT, S("\"k:\": a secret is a file in the mount, not the mount")));
    // A path that is no secrets mount's is any path.
    fixture_again(&f);
    EXPECT_EQ(paths_secret_path(&f.paths, &f.run, fixture_pos(), S("run"), S("all:up"), &secret), STOP);
    EXPECT(fixture_stopped(&f, STOP, CLASS_REFUSED, S("up is a symbolic link")));
    // Names are not secrets: the folder above it lists.
    EXPECT_OK_IN(&f.ctx, fixture_look(&f, "ls", S("")));
    fixture_deinit(&f);
}

TEST(stdout_secret_only_directly_in_a_secrets_mount_never_in_a_folder_of_it_never_through_a_link) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "keys/sub");
    file_system_fixture_mkdir(&f.base, "plain");
    file_system_fixture_put(&f.base, "keys/old", "was\n");
    fixture_link(&f, "old", "keys/ln");
    fixture_mounts(&f, "k = R/keys secrets\np = R/plain\n");
    typedef struct {
        const char *path;
        StopClass cls;
        const char *says;
    } Case;
    static const Case cases[] = {
        {"p:x", CLASS_REFUSED, "the mount `p` does not hold secrets"},
        {"x", CLASS_REFUSED, "is not in a mount"},
        {"k:sub/x", CLASS_REFUSED, "is in a folder of the mount"},
        {"k:../x", CLASS_REFUSED, "is in a folder of the mount"},
        {"k:..", CLASS_ARGUMENT, "climbs out of its mount with `..`"},
        {"k:", CLASS_ARGUMENT, "a secret is a file in the mount, not the mount"},
        {"k:.", CLASS_ARGUMENT, "a secret is a file in the mount, not the mount"},
        {"k:ln", CLASS_REFUSED, "is a symbolic link"},
    };
    SLICE_TYPE(Cases, const Case);
    Cases cs = {cases, sizeof cases / sizeof *cases};
    Str target = S("");
    for (size_t i = 0; i < cs.len; i++) {
        fixture_again(&f);
        EXPECT(fixture_stopped(&f, paths_secret_target(&f.paths, &f.run, fixture_pos(), file_system_fixture_cs(at(cs, i).path), &target), at(cs, i).cls, file_system_fixture_cs(at(cs, i).says)));
    }
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_secret_target(&f.paths, &f.run, fixture_pos(), S("k:sub/x"), &target), CLASS_REFUSED,
                   S("`run`'s `stdout_secret` \"k:sub/x\" is in a folder of the mount: a secret is a file directly in it, `\"k:NAME\"`")));
    fixture_again(&f);
    EXPECT_OK_IN(&f.ctx, paths_secret_target(&f.paths, &f.run, fixture_pos(), S("k:new"), &target));
    EXPECT_STR(target, fixture_r(&f, "R/keys/new"));
    fixture_deinit(&f);
}

TEST(the_secrets_rule_s_one_check_a_host_text_naming_a_secrets_folder_ends_the_process) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "keys");
    fixture_mounts(&f, "k = R/keys secrets\n");
    Str dir = file_system_fixture_at(&f.base, "keys/tok").s;
    // The script's own text may come back, as Zig's plain stop gives it back.
    fixture_again(&f);
    EXPECT(fixture_said(&f, path_fail(&f.run, fixture_pos(), CLASS_ARGUMENT, S("`read`: \"{s}\" is absolute"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(dir))), CLASS_ARGUMENT, fixture_r(&f, "`read`: \"R/keys/tok\" is absolute")));
    // A host text naming the folder is a bug in the C, never a message.
    EXPECT_DIES(if (path_fail(&f.run, fixture_pos(), CLASS_FILE_SYSTEM, S("--undo-on-stop: {s} cannot be read"), MESSAGE_ARGS(MESSAGE_FROM_HOST(dir))) != OK) {});
    // So is one naming it as the run sees it, through a relative root.
    bool names = false;
    EXPECT_OK_IN(&f.ctx, paths_names_secret(&f.paths, &f.run, fixture_pos(), fixture_r(&f, "x R/keys y"), &names));
    EXPECT(names);
    EXPECT_OK_IN(&f.ctx, paths_names_secret(&f.paths, &f.run, fixture_pos(), S("keys/tok"), &names));
    EXPECT(!names);
    // The folder itself, wherever it stands in a message.
    Lits namings = LITS("R/keys", "R/keys/tok", "R/keys.", "`R/keys`", "R/keys: shut", "R/keys2 or R/keys/t", "R/keys.bak and R/keys");
    for (size_t k = 0; k < namings.len; k++) {
        names = false;
        EXPECT_OK_IN(&f.ctx, paths_names_secret(&f.paths, &f.run, fixture_pos(), fixture_r(&f, at(namings, k)), &names));
        EXPECT(names);
    }
    // A sibling whose name only begins with the folder's is not it, so its
    // stop is a message, never the end of the process.
    Lits siblings = LITS("R/keysx", "R/keys2/f.txt/x", "R/keys-old", "R/keys_1", "R/keys.bak/tok");
    for (size_t k = 0; k < siblings.len; k++) {
        names = true;
        EXPECT_OK_IN(&f.ctx, paths_names_secret(&f.paths, &f.run, fixture_pos(), fixture_r(&f, at(siblings, k)), &names));
        EXPECT(!names);
    }
    // A mount's folder is a host text too, held to the same check.
    EXPECT_DIES(if (path_fail(&f.run, fixture_pos(), CLASS_FILE_SYSTEM, S("the mount `{s}` is {s}, which is not a folder there"), MESSAGE_ARGS(MESSAGE_FROM_HOST(S("k")), MESSAGE_FROM_HOST(fixture_r(&f, "R/keys")))) != OK) {});
    // So a mount's folder missing or shut is said by the mount's name alone
    // when the folder is a secrets mount's, or its path spells one; an
    // ordinary mount's names its folder.
    file_system_fixture_mkdir(&f.base, "shut/keys");
    file_system_fixture_mkdir(&f.base, "shut/plain");
    fixture_mounts(&f, "k = R/no-keys secrets\np = R/no-plain\nks = R/shut/keys secrets\nps = R/shut/plain\nin = R/no-keys/sub\nnear = R/no-keys-old\nkin = R/shut/keys/sub\n");
    Str secret = S("");
    // Through `run`'s `secrets` and `stdout_secret`, the mount alone.
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_secret_path(&f.paths, &f.run, fixture_pos(), S("run"), S("k:tok"), &secret), CLASS_FILE_SYSTEM, S("the mount `k` is a secrets mount whose folder is missing")));
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_secret_target(&f.paths, &f.run, fixture_pos(), S("k:tok"), &secret), CLASS_FILE_SYSTEM, S("the mount `k` is a secrets mount whose folder is missing")));
    // An ordinary mount's names its folder.
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("p:x")), CLASS_FILE_SYSTEM, SAYS("the mount `p` is R/no-plain, which is not a folder there")));
    // Unless its path spells a secrets mount's folder, inside it or only
    // beginning with it.
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("in:x")), CLASS_FILE_SYSTEM, S("the mount `in`'s folder is missing; its path holds a secrets mount's folder, so it is not named")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("near:x")), CLASS_FILE_SYSTEM, S("the mount `near`'s folder is missing; its path holds a secrets mount's folder, so it is not named")));
    // A folder there that cannot be opened: its parent shut.
    EXPECT_EQ(file_system_fixture_chmod(&f.base, "shut", 0), 0);
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_secret_path(&f.paths, &f.run, fixture_pos(), S("run"), S("ks:tok"), &secret), CLASS_FILE_SYSTEM, S("the mount `ks` is a secrets mount whose folder cannot be opened: AccessDenied")));
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_secret_target(&f.paths, &f.run, fixture_pos(), S("ks:tok"), &secret), CLASS_FILE_SYSTEM, S("the mount `ks` is a secrets mount whose folder cannot be opened: AccessDenied")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("ps:x")), CLASS_FILE_SYSTEM, SAYS("the mount `ps` is R/shut/plain, which cannot be opened: AccessDenied")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("kin:x")), CLASS_FILE_SYSTEM, S("the mount `kin`'s folder cannot be opened: AccessDenied; its path holds a secrets mount's folder, so it is not named")));
    EXPECT_EQ(file_system_fixture_chmod(&f.base, "shut", 0700), 0);
    fixture_deinit(&f);
}

// ── the prefix attack ───────────────────────────────────────────────────

TEST(a_folder_that_only_begins_with_another_s_name_is_not_in_it) {
    EXPECT(mounts_in_folder(S("/tmp/x"), S("/tmp/x")));
    EXPECT(mounts_in_folder(S("/tmp/x/a"), S("/tmp/x")));
    EXPECT(!mounts_in_folder(S("/tmp/xy"), S("/tmp/x")));
    EXPECT(!mounts_in_folder(S("/tmp/xy/a"), S("/tmp/x")));
    EXPECT(!mounts_in_folder(S("/tmp"), S("/tmp/x")));
    EXPECT(mounts_in_folder(S("/anything"), S("/")));
    EXPECT(!mounts_in_folder(S("relative"), S("/")));
    EXPECT_STR(mounts_trim_slash(S("/tmp/x//")), S("/tmp/x"));
    EXPECT_STR(mounts_trim_slash(S("//")), S("/"));
    EXPECT_STR(mounts_trim_slash(S("")), S(""));
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "x");
    file_system_fixture_mkdir(&f.base, "xy");
    file_system_fixture_mkdir(&f.base, "k");
    file_system_fixture_mkdir(&f.base, "kz");
    fixture_mounts(&f, "x = R/x ro\nk = R/k secrets\n");
    EXPECT(fixture_stopped(&f, fixture_change(&f, "replace", S("x/a.txt"), S("a")), CLASS_REFUSED, S("mount `x` is read-only")));
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "replace", S("xy/a.txt"), S("a")));
    EXPECT(fixture_stopped(&f, fixture_look(&f, "read", S("k/a")), CLASS_REFUSED, S("is in the mount `k`")));
    EXPECT_OK_IN(&f.ctx, fixture_look(&f, "read", S("kz/a")));
    EXPECT(!file_system_fixture_there(&f.base, "x/a.txt"));
    EXPECT(file_system_fixture_there(&f.base, "xy/a.txt"));
    fixture_deinit(&f);
}

// ── the undo journal ────────────────────────────────────────────────────

// A change the way `mkdir` and `remove_tree` make theirs: the hook first.
static STATUS_MUST_USE Status fixture_tree(Fixture *f, bool make, Str path) {
    fixture_again(f);
    CheckedPath p;
    TRY(path_arg(&f->run, fixture_pos(), make ? S("mkdir") : S("remove_tree"), path, false, &p));
    TRY(f->hook.may_change(f->hook.ctx, &f->run, fixture_pos(), p.rel, p.full.s));
    FileSystemErr e = make ? file_system_make_dir(&f->base.file_system, p.full) : file_system_remove_tree(&f->base.file_system, &f->base.values, p.full);
    return e == FILE_SYSTEM_OK ? OK : path_file_system_failed(&f->run, fixture_pos(), make ? S("mkdir") : S("remove_tree"), p.rel, e);
}

static void fixture_journal_kinds(Fixture *f, size_t *file, size_t *absent, size_t *dir) {
    *file = 0;
    *absent = 0;
    *dir = 0;
    for (size_t k = 0; k < f->paths.journal.len; k++) {
        PriorKind w = at(f->paths.journal, k).was;
        if (w == PRIOR_FILE) (*file)++;
        if (w == PRIOR_ABSENT) (*absent)++;
        if (w == PRIOR_DIR) (*dir)++;
    }
}

TEST(undo_each_path_is_journaled_once_before_its_first_change_as_a_file_s_bytes_a_folder_or_nothing_there) {
    Fixture f;
    fixture_init(&f);
    f.paths.undo = true;
    file_system_fixture_put(&f.base, "a.txt", "one\n");
    file_system_fixture_mkdir(&f.base, "d");
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "edit", S("a.txt"), S("two\n")));
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "edit", S("a.txt"), S("three\n")));
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "create", S("new.txt"), S("made\n")));
    EXPECT_OK_IN(&f.ctx, fixture_tree(&f, true, S("d/inner")));
    EXPECT_OK_IN(&f.ctx, fixture_tree(&f, false, S("d")));
    // a.txt once though changed twice; the rest each once.
    PriorsItems j = Priors_items(&f.paths.journal);
    EXPECT_EQ(j.len, 4);
    for (size_t k = 0; k < j.len; k++) {
        for (size_t q = k + 1; q < j.len; q++) EXPECT(!str_eq(at(j, k).abs, at(j, q).abs));
    }
    size_t file = 0;
    size_t absent = 0;
    size_t dir = 0;
    fixture_journal_kinds(&f, &file, &absent, &dir);
    EXPECT_EQ(file, 1);
    EXPECT_EQ(absent, 2);
    EXPECT_EQ(dir, 1);
    EXPECT_STR(at(j, 0).abs, fixture_r(&f, "R/a.txt"));
    EXPECT_STR(at(j, 0).bytes, S("one\n"));
    EXPECT_EQ(f.paths.journal_bytes, 4);
    Buf w = buf_make(&f.host);
    bool all = true;
    EXPECT_EQ(undo_changes(&f.paths, &w, &all), OK);
    // The folder was removed whole, which undo cannot put back: said, false.
    EXPECT(!all);
    Str said = buf_view(&w);
    EXPECT_STR(said, fixture_r(&f, "── not put back: R/d: a folder removed whole, and its files with it\n── undone: 4 path(s) the run changed put back, but not all (above)\n"));
    Str text = S("");
    EXPECT(file_system_fixture_file(&f.base, "a.txt", &text));
    EXPECT_STR(text, S("one\n"));
    EXPECT(!file_system_fixture_there(&f.base, "new.txt"));
    fixture_deinit(&f);
}

TEST(undo_a_change_past_the_journal_s_room_a_file_it_cannot_read_a_path_neither_file_nor_folder_and_one_it_cannot_look_at_are_refused) {
    Fixture f;
    fixture_init(&f);
    f.paths.undo = true;
    file_system_fixture_put(&f.base, "a.txt", "one\n");
    // The room left is less than a.txt.
    f.paths.journal_bytes = PATHS_JOURNAL_MAX - 2;
    EXPECT(fixture_stopped(&f, fixture_change(&f, "replace", S("a.txt"), S("x")), CLASS_REFUSED, S("--undo-on-stop keeps at most 67108864 bytes of what a run changes, and ")));
    EXPECT(fixture_said(&f, fixture_change(&f, "replace", S("a.txt"), S("x")), CLASS_REFUSED, SAYS("--undo-on-stop keeps at most 67108864 bytes of what a run changes, and R/a.txt would pass it")));
    f.paths.journal_bytes = 0;
    Str text = S("");
    EXPECT(file_system_fixture_file(&f.base, "a.txt", &text));
    EXPECT_STR(text, S("one\n"));
    // A file that may be stat'd but not read.
    file_system_fixture_put(&f.base, "sealed.txt", "s\n");
    EXPECT_EQ(file_system_fixture_chmod(&f.base, "sealed.txt", 0), 0);
    EXPECT(fixture_stopped(&f, fixture_change(&f, "replace", S("sealed.txt"), S("x")), CLASS_FILE_SYSTEM, S("sealed.txt cannot be read (AccessDenied), so it could not be put back")));
    EXPECT_EQ(file_system_fixture_chmod(&f.base, "sealed.txt", 0600), 0);
    // A named pipe.
    FileSystemCPath fifo_cp;
    const char *fifo = NULL;
    EXPECT_EQ(file_system_cstr(&fifo_cp, file_system_fixture_at(&f.base, "pipe"), &fifo), FILE_SYSTEM_OK);
    EXPECT_EQ(mkfifo(fifo, 0600), 0);
    EXPECT(fixture_stopped(&f, fixture_change(&f, "remove", S("pipe"), S("")), CLASS_REFUSED, S("pipe is neither a file nor a folder, and could not be put back")));
    // A path in a folder that cannot be searched.
    file_system_fixture_mkdir(&f.base, "locked");
    EXPECT_EQ(file_system_fixture_chmod(&f.base, "locked", 0), 0);
    EXPECT(fixture_stopped(&f, fixture_change(&f, "append", S("locked/x.txt"), S("x")), CLASS_FILE_SYSTEM, S("locked/x.txt cannot be looked at (AccessDenied), so it could not be put back")));
    EXPECT_EQ(file_system_fixture_chmod(&f.base, "locked", 0700), 0);
    EXPECT_EQ(f.paths.journal.len, 0);
    EXPECT(file_system_fixture_file(&f.base, "sealed.txt", &text));
    EXPECT_STR(text, S("s\n"));
    fixture_deinit(&f);
}

TEST(undo_the_journal_s_own_allocations_failing_at_each_stop_the_run_on_memory) {
    size_t store_stops = 0;
    size_t arena_stops = 0;
    bool done = false;
    for (size_t limit = 0; limit < 65536 && !done; limit += 8) {
        Fixture f;
        fixture_init(&f);
        f.paths.undo = true;
        file_system_fixture_put(&f.base, "a.txt", "one\n");
        f.host.limit = limit;
        Status s = fixture_change(&f, "replace", S("a.txt"), S("two\n"));
        if (s == OK) s = fixture_change(&f, "create", S("new.txt"), S("n"));
        if (s == OK) {
            done = true;
        } else if (f.ctx.stop.cls == CLASS_FILE_SYSTEM) {
            // A failed read of a.txt's bytes is the file system's, named.
            EXPECT(mem_find(f.ctx.stop.message.ptr, f.ctx.stop.message.len, "a.txt cannot be read (OutOfMemory)", 34) != SIZE_MAX);
            store_stops++;
        } else {
            // Any other failure is the arena's budget.
            EXPECT_EQ(f.ctx.stop.cls, CLASS_BUDGET);
            EXPECT_EQ(f.ctx.stop.budget, BUDGET_ARENA);
            arena_stops++;
        }
        fixture_deinit(&f);
    }
    EXPECT(done);
    EXPECT(store_stops >= 1);
    EXPECT(arena_stops >= 1);
}

TEST(undo_a_relative_root_is_taken_against_the_working_folder_so_real_gives_an_absolute_path) {
    Fixture f;
    fixture_init(&f);
    Str abs = f.base.root;
    // The same folder, spelled from the working folder.
    f.run.root = str_sub(abs, f.cwd.len + 1, abs.len);
    EXPECT(paths_run_folders_make(&f.paths, &f.run));
    EXPECT(str_byte(f.run.root, 0) != '/');
    Str real = S("");
    EXPECT_OK_IN(&f.ctx, paths_os_path(&f.paths, &f.run, fixture_pos(), S("real"), S(""), true, &real));
    EXPECT_STR(real, abs);
    fixture_deinit(&f);
}

TEST(undo_on_stop_puts_back_an_edit_a_file_made_a_folder_made_a_file_removed_an_executable_s_bytes_and_mode) {
    Fixture f;
    fixture_init(&f);
    f.paths.undo = true;
    file_system_fixture_put(&f.base, "a.txt", "one\n");
    file_system_fixture_put(&f.base, "gone.txt", "kept\n");
    file_system_fixture_put(&f.base, "x.sh", "#!/bin/sh\necho old\n");
    EXPECT_EQ(file_system_fixture_chmod(&f.base, "x.sh", 0755), 0);
    file_system_fixture_mkdir(&f.base, "there");
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "edit", S("a.txt"), S("two\n")));
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "create", S("new.txt"), S("made\n")));
    EXPECT_OK_IN(&f.ctx, fixture_tree(&f, true, S("made")));
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "create", S("made/inner.txt"), S("in\n")));
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "replace", S("x.sh"), S("#!/bin/sh\necho new\n")));
    // `remove`: the hook, then the file system's remove.
    fixture_again(&f);
    CheckedPath p;
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("remove"), S("gone.txt"), false, &p));
    EXPECT_OK_IN(&f.ctx, f.hook.may_change(f.hook.ctx, &f.run, fixture_pos(), p.rel, p.full.s));
    EXPECT_EQ(file_system_remove(&f.base.file_system, p.full), FILE_SYSTEM_OK);
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "create", S("there/n.txt"), S("n\n")));
    Buf w = buf_make(&f.host);
    bool all = false;
    EXPECT_EQ(undo_changes(&f.paths, &w, &all), OK);
    EXPECT(all);
    EXPECT_STR(buf_view(&w), S("── undone: 7 path(s) the run changed put back\n"));
    Str text = S("");
    EXPECT(file_system_fixture_file(&f.base, "a.txt", &text));
    EXPECT_STR(text, S("one\n"));
    EXPECT(file_system_fixture_file(&f.base, "gone.txt", &text));
    EXPECT_STR(text, S("kept\n"));
    EXPECT(file_system_fixture_file(&f.base, "x.sh", &text));
    EXPECT_STR(text, S("#!/bin/sh\necho old\n"));
    EXPECT(!file_system_fixture_there(&f.base, "new.txt"));
    EXPECT(!file_system_fixture_there(&f.base, "made"));
    EXPECT(!file_system_fixture_there(&f.base, "there/n.txt"));
    // The folder that was there is there still, and the executable still one.
    EXPECT(file_system_fixture_there(&f.base, "there"));
    struct stat st;
    EXPECT_EQ(file_system_stat_exact(file_system_fixture_at(&f.base, "x.sh"), true, &st), FILE_SYSTEM_OK);
    EXPECT_EQ(st.st_mode & 0777, 0755);
    fixture_deinit(&f);
}

TEST(undo_of_nothing_there_whose_path_is_gone_already_is_put_back_as_nothing) {
    Fixture f;
    fixture_init(&f);
    f.paths.undo = true;
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "create", S("brief.txt"), S("b")));
    // Something else took it away before the undo: nothing to do, all put back.
    EXPECT_EQ(file_system_remove(&f.base.file_system, file_system_fixture_at(&f.base, "brief.txt")), FILE_SYSTEM_OK);
    Buf w = buf_make(&f.host);
    bool all = false;
    EXPECT_EQ(undo_changes(&f.paths, &w, &all), OK);
    EXPECT(all);
    EXPECT_STR(buf_view(&w), S("── undone: 1 path(s) the run changed put back\n"));
    // An empty journal says so, and is all put back.
    Paths empty;
    paths_make(&empty, &f.host, f.base.root, f.cwd, f.paths.mounts, S(""));
    buf_clear(&w);
    EXPECT_EQ(undo_changes(&empty, &w, &all), OK);
    EXPECT(all);
    EXPECT_STR(buf_view(&w), S("── undone: 0 path(s) the run changed put back\n"));
    fixture_deinit(&f);
}

TEST(a_step_budget_met_in_the_walk_of_a_path_stops_one_past_it) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "a/b");
    // `a/b/c` charges 5 for its bytes, then 1 for each of its 3 parts.
    f.steps_budget = 7;
    CheckedPath p;
    fixture_again(&f);
    EXPECT(fixture_stopped(&f, path_arg(&f.run, fixture_pos(), S("read"), S("a/b/c"), false, &p), CLASS_BUDGET, S("the step budget (")));
    EXPECT_EQ(f.ctx.stop.budget, BUDGET_STEPS);
    EXPECT_EQ(f.steps, 8);
    f.steps = 0;
    f.steps_budget = 8;
    fixture_again(&f);
    EXPECT_OK_IN(&f.ctx, path_arg(&f.run, fixture_pos(), S("read"), S("a/b/c"), false, &p));
    EXPECT_EQ(f.steps, 8);
    fixture_deinit(&f);
}

TEST(a_message_whose_arguments_miss_its_placeholders_ends_the_process) {
    bool refused = false;
    Arena a;
    arena_init(&a, 1 << 20, &refused);
    Buf b = buf_make(&a);
    MessageArgs none = {NULL, 0};
    EXPECT_EQ(message_format(&b, S("`{s}` and {{braces}}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(S("x")))), OK);
    EXPECT_STR(buf_view(&b), S("`x` and {braces}"));
    // A placeholder with no argument, and an argument with no placeholder.
    EXPECT_DIES(if (message_format(&b, S("`{s}` is {s}"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(S("x")))) != OK) {});
    EXPECT_DIES(if (message_format(&b, S("no placeholder"), MESSAGE_ARGS(MESSAGE_FROM_SCRIPT(S("x")))) != OK) {});
    EXPECT_DIES(if (message_format(&b, S("{s}"), none) != OK) {});
    arena_deinit(&a);
}

TEST(a_host_report_line_naming_a_secrets_folder_ends_the_process) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "keys");
    fixture_mounts(&f, "k = R/keys secrets\n");
    fixture_again(&f);
    // The undo's line for a file outside the folder is said.
    EXPECT_OK_IN(&f.ctx, paths_report_names_no_secret(&f.paths, &f.run, fixture_r(&f, "undo: put back R/notes.txt")));
    // One naming the folder ends the process before it is said.
    EXPECT_DIES(if (paths_report_names_no_secret(&f.paths, &f.run, fixture_r(&f, "undo: put back R/keys/tok")) != OK) {});
    fixture_deinit(&f);
}

// ── the secrets folders, worked out once a run ──────────────────────────

// Two secrets mounts under the root and one that reaches both, so each check
// is seen to walk past the first secrets folder to the second.
static void fixture_two_vaults(Fixture *f) {
    file_system_fixture_mkdir(&f->base, "keys");
    file_system_fixture_mkdir(&f->base, "deep/vault");
    file_system_fixture_put(&f->base, "deep/vault/tok", "s3cret\n");
    fixture_mounts(f, "k = R/keys secrets\nall = R\nv = R/deep/vault secrets\n");
}

TEST(secrets_folders_are_worked_out_once_a_run_so_a_walk_s_check_of_each_entry_allocates_nothing) {
    Fixture f;
    fixture_init(&f);
    fixture_two_vaults(&f);
    EXPECT_EQ(f.paths.run.secrets.len, 2);
    EXPECT_STR(at(f.paths.run.secrets, 1).name, S("v"));
    EXPECT_STR(at(f.paths.run.secrets, 1).run_dir, fixture_r(&f, "R/deep/vault"));
    EXPECT_EQ(f.paths.run.dirs.len, 3);
    EXPECT_STR(at(f.paths.run.dirs, 1), f.base.root);
    Str plain = fixture_r(&f, "R/deep/notes.txt");
    Str sibling = fixture_r(&f, "R/deep/vault2/x");
    Str in_first = fixture_r(&f, "R/keys/tok");
    Str in_second = fixture_r(&f, "R/deep/vault/sub/tok");
    size_t values = f.base.values.used;
    size_t host = f.host.used;
    // A thousand entries' checks, as a walk of a thousand files makes them:
    // not a byte of either arena for the secrets folders.
    size_t secret = 0;
    for (size_t i = 0; i < 1000; i++) {
        Str full = i % 4 == 0 ? plain : i % 4 == 1 ? sibling : i % 4 == 2 ? in_first : in_second;
        bool in = false;
        EXPECT_OK_IN(&f.ctx, f.hook.in_secrets(f.hook.ctx, &f.run, fixture_pos(), full, &in));
        secret += in ? 1 : 0;
    }
    EXPECT_EQ(secret, 500);
    EXPECT_EQ(f.base.values.used, values);
    EXPECT_EQ(f.host.used, host);
    fixture_deinit(&f);
}

TEST(secrets_folders_worked_out_once_every_refusal_still_fires_for_the_second_mount) {
    Fixture f;
    fixture_init(&f);
    fixture_two_vaults(&f);
    // A secrets mount under the root, by a root path and by another mount's.
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("deep/vault/tok")), CLASS_REFUSED, S("\"deep/vault/tok\" is in the mount `v`, which holds secrets")));
    EXPECT(fixture_said(&f, fixture_look(&f, "read", S("all:deep/vault/tok")), CLASS_REFUSED, S("\"all:deep/vault/tok\" is in the mount `v`, which holds secrets")));
    // A secrets folder named in a program's argument, and in its `env`.
    const Str argv_items[2] = {S("shale-fixture"), S("deep/vault/tok")};
    PathsStrs argv = {argv_items, 2};
    PathsStrs none = {NULL, 0};
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_refuse_secret_args(&f.paths, &f.run, fixture_pos(), S("run"), argv, f.base.root, none, none), CLASS_REFUSED,
                   S("`run`'s argument 2 names the mount `v`'s folder, which holds secrets: hand one over as `secrets`")));
    const Str env_k[1] = {S("F")};
    const Str env_v[1] = {S("deep/vault")};
    PathsStrs ek = {env_k, 1};
    PathsStrs ev = {env_v, 1};
    fixture_again(&f);
    EXPECT(fixture_said(&f, paths_refuse_secret_args(&f.paths, &f.run, fixture_pos(), S("job_start"), none, f.base.root, ek, ev), CLASS_REFUSED,
                   S("`job_start`'s `env` F names the mount `v`'s folder, which holds secrets: hand one over as `secrets`")));
    // An absolute path inside one has no hint; one beside it has one.
    AbsoluteHint hint;
    fixture_again(&f);
    EXPECT_OK_IN(&f.ctx, f.hook.absolute_hint(f.hook.ctx, &f.run, fixture_pos(), fixture_r(&f, "R/deep/vault/tok"), &hint));
    EXPECT_EQ(hint.kind, ABSOLUTE_UNNAMED);
    EXPECT_OK_IN(&f.ctx, f.hook.absolute_hint(f.hook.ctx, &f.run, fixture_pos(), fixture_r(&f, "R/deep/vault2/x"), &hint));
    EXPECT_EQ(hint.kind, ABSOLUTE_HINTED);
    EXPECT_STR(hint.spelled, S("deep/vault2/x"));
    // A text naming the second folder is a text naming a secret.
    bool names = false;
    EXPECT_OK_IN(&f.ctx, f.hook.names_secret(f.hook.ctx, &f.run, fixture_pos(), fixture_r(&f, "at R/deep/vault."), &names));
    EXPECT(names);
    EXPECT_OK_IN(&f.ctx, f.hook.names_secret(f.hook.ctx, &f.run, fixture_pos(), fixture_r(&f, "at R/deep/vault2"), &names));
    EXPECT(!names);
    fixture_deinit(&f);
}

TEST(secrets_folders_worked_out_for_other_mounts_or_another_root_end_the_process) {
    Fixture f;
    fixture_init(&f);
    fixture_two_vaults(&f);
    bool in = false;
    Str full = fixture_r(&f, "R/deep/vault/tok");
    // The mounts changed and the list not made again: it would miss them.
    MountsItems kept = f.paths.mounts;
    MountsItems fewer = {kept.ptr, 1};
    f.paths.mounts = fewer;
    EXPECT_DIES(if (f.hook.in_secrets(f.hook.ctx, &f.run, fixture_pos(), full, &in) != OK) {});
    f.paths.mounts = kept;
    // Another root.
    Str root = f.run.root;
    f.run.root = S("/elsewhere");
    EXPECT_DIES(if (f.hook.in_secrets(f.hook.ctx, &f.run, fixture_pos(), full, &in) != OK) {});
    f.run.root = root;
    // As made, it answers.
    EXPECT_OK_IN(&f.ctx, f.hook.in_secrets(f.hook.ctx, &f.run, fixture_pos(), full, &in));
    EXPECT(in);
    fixture_deinit(&f);
}

TEST(read_only_mounts_folders_are_worked_out_once_a_run_so_a_change_s_check_allocates_nothing) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "frozen/open");
    fixture_mounts(&f, "ro = R/frozen ro\nopen = R/frozen/open\n");
    Str inside = fixture_r(&f, "R/frozen/open/a.txt");
    Str beside = fixture_r(&f, "R/notes.txt");
    size_t values = f.base.values.used;
    size_t host = f.host.used;
    // A thousand changes' checks: the writable mount inside the read-only
    // one decides for its folder, and not a byte is taken for the folders.
    for (size_t i = 0; i < 1000; i++) {
        fixture_again(&f);
        EXPECT_OK_IN(&f.ctx, f.hook.may_change(f.hook.ctx, &f.run, fixture_pos(), S("x"), i % 2 == 0 ? inside : beside));
    }
    EXPECT_EQ(f.base.values.used, values);
    EXPECT_EQ(f.host.used, host);
    // The read-only folder itself is refused, by a root path and by its name.
    EXPECT(fixture_said(&f, fixture_change(&f, "replace", S("frozen/a.txt"), S("x")), CLASS_REFUSED, S("frozen/a.txt: mount `ro` is read-only")));
    EXPECT(fixture_said(&f, fixture_change(&f, "replace", S("ro:a.txt"), S("x")), CLASS_REFUSED, S("ro:a.txt: mount `ro` is read-only")));
    EXPECT_OK_IN(&f.ctx, fixture_change(&f, "replace", S("frozen/open/b.txt"), S("x")));
    fixture_deinit(&f);
}

TEST(a_path_run_has_a_hook_always_metal_s_answers_nothing_resolved_no_hint_and_no_secret) {
    Fixture f;
    fixture_init(&f);
    EXPECT_DIES((void)path_run_make(&f.ctx, f.base.root, fixture_charge, &f, NULL));
    PathRun bare = path_run_make(&f.ctx, f.base.root, fixture_charge, &f, &PATH_HOOK_NONE);
    CheckedPath p;
    // A mount's spelling is a root path's part that is no strict name.
    fixture_again(&f);
    EXPECT(fixture_said(&f, path_arg(&bare, fixture_pos(), S("read"), S("k:tok"), false, &p), CLASS_ARGUMENT, S("`read`: \"k:tok\" has a part (\"k:tok\") that is not a name: no `.`, `..`, empty parts or characters a strict name refuses")));
    fixture_again(&f);
    EXPECT_OK_IN(&f.ctx, path_arg(&bare, fixture_pos(), S("read"), S("a/b"), false, &p));
    EXPECT_STR(p.full.s, fixture_r(&f, "R/a/b"));
    bool yes = true;
    EXPECT_OK_IN(&f.ctx, PATH_HOOK_NONE.names_secret(NULL, &bare, fixture_pos(), f.base.root, &yes));
    EXPECT(!yes);
    yes = true;
    EXPECT_OK_IN(&f.ctx, PATH_HOOK_NONE.in_secrets(NULL, &bare, fixture_pos(), f.base.root, &yes));
    EXPECT(!yes);
    EXPECT_OK_IN(&f.ctx, PATH_HOOK_NONE.may_change(NULL, &bare, fixture_pos(), S("a"), f.base.root));
    fixture_deinit(&f);
}

TEST(a_root_path_s_names_are_the_core_s_to_check_before_the_hook_walks_its_links) {
    Fixture f;
    fixture_init(&f);
    file_system_fixture_mkdir(&f.base, "elsewhere");
    fixture_link(&f, "elsewhere", "lnk");
    CheckedPath p;
    // A bad part after a link: the core's message, never the link's.
    fixture_again(&f);
    EXPECT(fixture_said(&f, path_arg(&f.run, fixture_pos(), S("read"), S("lnk//x"), false, &p), CLASS_ARGUMENT, S("`read`: \"lnk//x\" has a part (\"\") that is not a name: no `.`, `..`, empty parts or characters a strict name refuses")));
    fixture_again(&f);
    EXPECT(fixture_said(&f, path_arg(&f.run, fixture_pos(), S("read"), S("lnk/../x"), false, &p), CLASS_ARGUMENT, S("`read`: \"lnk/../x\" climbs out of the root with `..`: a folder beside it is reached through a mount, such as `repos:<name>/...` (`shale --help` lists this box's mounts)")));
    // Every part a name: the hook's walk refuses the link.
    fixture_again(&f);
    EXPECT(fixture_stopped(&f, path_arg(&f.run, fixture_pos(), S("read"), S("lnk/x"), false, &p), CLASS_REFUSED, S("lnk is a symbolic link: the file system follows none")));
    fixture_deinit(&f);
}

// A run's root that is the home, holds ~/.ssh, ~/.config or ~/.claude, or
// lies inside ~/.ssh or ~/.config is barred, each said; ~/showell_repos, a
// folder below it, a sibling that only begins with `.ssh` and a folder
// inside ~/.claude (the memory's mount) are not.
TEST(a_root_that_is_or_holds_the_home_s_keys_is_refused) {
    bool refused = false;
    Arena a;
    arena_init(&a, (size_t)1 << 20, &refused);
    typedef struct {
        const char *root;
        const char *says;
    } Case;
    static const Case cases[] = {
        {"/home/x", "is the home folder, or holds it"},
        {"/home", "is the home folder, or holds it"},
        {"/", "is the home folder, or holds it"},
        {"/home/x/.ssh", "holds ~/.ssh"},
        {"/home/x/.ssh/keys", "is inside ~/.ssh"},
        {"/home/x/.config", "holds ~/.config"},
        {"/home/x/.config/shale", "is inside ~/.config"},
        {"/home/x/.claude", "holds ~/.claude"},
        {"/home/x/showell_repos", ""},
        {"/home/x/showell_repos/shale", ""},
        {"/home/x/.sshd", ""},
        {"/home/x/.claude/projects/memory", ""},
        {"/home/xy", ""},
    };
    SLICE_TYPE(Cases, const Case);
    Cases all = {cases, sizeof cases / sizeof *cases};
    for (size_t i = 0; i < all.len; i++) {
        Str said = S("x");
        EXPECT(mounts_root_kept_out(&a, S("/home/x"), file_system_fixture_cs(at(all, i).root), &said));
        EXPECT_STR(said, file_system_fixture_cs(at(all, i).says));
    }
    arena_deinit(&a);
}

TEST_MAIN(
    TEST_CASE(every_way_out_of_the_root_is_refused_before_the_file_system_is_asked),
    TEST_CASE(mounts_a_mounts_paths_real_an_unknown_mount_or_a_missing_folder_named_the_files_mistakes_by_line),
    TEST_CASE(the_mounts_file_shale_mounts_pins_it_the_default_s_absence_is_no_mounts_a_pinned_one_s_a_mistake),
    TEST_CASE(ro_mounts_the_mounts_file_s_ro_and_tools_in_either_order_any_other_trailing_word_or_one_twice_refused_by_line),
    TEST_CASE(secrets_mounts_the_mounts_file_s_mistakes_name_the_line_and_the_mount_never_the_line_s_text_a_path_or_a_word_after_one),
    TEST_CASE(an_absolute_path_s_stop_the_deepest_mount_or_the_root_that_holds_it_and_how_to_write_it_there),
    TEST_CASE(an_absolute_path_s_stop_and_a_host_call_s_in_a_builtin_s_checks_the_call_is_named),
    TEST_CASE(an_absolute_path_s_stop_a_path_in_a_secrets_mount_s_folder_names_neither_the_mount_nor_its_folder),
    TEST_CASE(ro_mounts_every_change_through_one_is_refused_before_anything_is_written_a_read_works),
    TEST_CASE(ro_mounts_a_writable_mount_inside_a_read_only_one_decides_for_its_own_folder),
    TEST_CASE(a_walk_through_a_mount_its_paths_are_the_mounts_and_each_lands_where_the_mount_points),
    TEST_CASE(the_file_system_follows_no_link_to_a_file_to_a_folder_in_the_middle_in_a_mount_out_of_the_root),
    TEST_CASE(secrets_mounts_no_read_listing_change_path_or_argument_reaches_a_token_by_the_mount_or_another_way),
    TEST_CASE(stdout_secret_only_directly_in_a_secrets_mount_never_in_a_folder_of_it_never_through_a_link),
    TEST_CASE(the_secrets_rule_s_one_check_a_host_text_naming_a_secrets_folder_ends_the_process),
    TEST_CASE(a_folder_that_only_begins_with_another_s_name_is_not_in_it),
    TEST_CASE(undo_each_path_is_journaled_once_before_its_first_change_as_a_file_s_bytes_a_folder_or_nothing_there),
    TEST_CASE(undo_a_change_past_the_journal_s_room_a_file_it_cannot_read_a_path_neither_file_nor_folder_and_one_it_cannot_look_at_are_refused),
    TEST_CASE(undo_the_journal_s_own_allocations_failing_at_each_stop_the_run_on_memory),
    TEST_CASE(undo_a_relative_root_is_taken_against_the_working_folder_so_real_gives_an_absolute_path),
    TEST_CASE(undo_on_stop_puts_back_an_edit_a_file_made_a_folder_made_a_file_removed_an_executable_s_bytes_and_mode),
    TEST_CASE(undo_of_nothing_there_whose_path_is_gone_already_is_put_back_as_nothing),
    TEST_CASE(a_step_budget_met_in_the_walk_of_a_path_stops_one_past_it),
    TEST_CASE(a_message_whose_arguments_miss_its_placeholders_ends_the_process),
    TEST_CASE(a_host_report_line_naming_a_secrets_folder_ends_the_process),
    TEST_CASE(secrets_folders_are_worked_out_once_a_run_so_a_walk_s_check_of_each_entry_allocates_nothing),
    TEST_CASE(secrets_folders_worked_out_once_every_refusal_still_fires_for_the_second_mount),
    TEST_CASE(secrets_folders_worked_out_for_other_mounts_or_another_root_end_the_process),
    TEST_CASE(read_only_mounts_folders_are_worked_out_once_a_run_so_a_change_s_check_allocates_nothing),
    TEST_CASE(a_path_run_has_a_hook_always_metal_s_answers_nothing_resolved_no_hint_and_no_secret),
    TEST_CASE(a_root_path_s_names_are_the_core_s_to_check_before_the_hook_walks_its_links),
    TEST_CASE(a_root_that_is_or_holds_the_home_s_keys_is_refused)
)
