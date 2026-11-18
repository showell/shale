// test_secrets.c: `shale --secrets` (c/host/secrets_mode.c) and the settings
// file's JSON tree under it (c/host/settings_json.c). The tree's tests read,
// judge and write texts; the mode's run it in a child on a pseudo-terminal
// (forkpty), the test answering each prompt once it is shown, over planted
// mounts, a planted secrets folder and a planted HOME under
// c/out/secrets-tmp. Each holds what was written, its mode, what was left
// alone, and that nothing printed holds a secret's value.
#define _GNU_SOURCE
#include "test.h"

#include <dirent.h>
#include <ftw.h>
#include <poll.h>
#include <pty.h>
#include <sys/stat.h>

#include "../host/store.c"
#include "../host/path_run.c"
#include "../host/mounts.c"
#include "../host/paths.c"
#include "../host/swap_file.c"
#include "../host/settings_json.c"
#include "../host/secrets_mode.c"

static Str test_cs(const char *s) { // cstr: a test's literal
    return (Str){s, strlen(s)};
}

// ── the settings tree ───────────────────────────────────────────────────

typedef struct {
    bool refused;
    Arena a;
} Tree;

static void tree_init(Tree *t) {
    t->refused = false;
    arena_init(&t->a, (size_t)64 << 20, &t->refused);
}

// `text` read and written back, or "" with the reason printed.
static Str tree_round(Tree *t, Str text) {
    SettingsReader r;
    SettingsNode *root = NULL;
    if (settings_read(&r, &t->a, text, &root) != OK) {
        fprintf(stderr, "  does not parse: %.*s at %zu\n", (int)r.why.len, r.why.ptr, r.why_at);
        return S("");
    }
    Str out = S("");
    if (settings_write(&t->a, root, &out) != OK) return S("");
    return out;
}

TEST(settings_a_file_read_and_written_back_keeps_its_order_its_texts_and_lays_out_two_spaces_a_level) {
    Tree t;
    tree_init(&t);
    Str in = S("{\"b\":1,\"a\":{\"x\":[1,2.50,-3e+2],\"y\":[]},\"s\":\"q\\\"\\u00e9\\n\",\"t\":true,\"n\":null,\"e\":{}}");
    EXPECT_STR(tree_round(&t, in), S("{\n"
                                     "  \"b\": 1,\n"
                                     "  \"a\": {\n"
                                     "    \"x\": [\n"
                                     "      1,\n"
                                     "      2.50,\n"
                                     "      -3e+2\n"
                                     "    ],\n"
                                     "    \"y\": []\n"
                                     "  },\n"
                                     "  \"s\": \"q\\\"\\u00e9\\n\",\n"
                                     "  \"t\": true,\n"
                                     "  \"n\": null,\n"
                                     "  \"e\": {}\n"
                                     "}\n"));
    // What it writes reads back to itself, and space anywhere JSON allows
    // it is no change.
    Str laid = tree_round(&t, in);
    EXPECT_STR(tree_round(&t, laid), laid);
    EXPECT_STR(tree_round(&t, S(" \r\n\t{ \"k\" :\n[ ] } \n")), S("{\n  \"k\": []\n}\n"));
    EXPECT_STR(tree_round(&t, S("[]")), S("[]\n"));
    // A string's escapes are undone when it is compared, a surrogate pair
    // made one character.
    Str v = S("");
    EXPECT((settings_string_value(&t.a, S("\"a\\/b\\u00e9\\ud83d\\ude00\\t\""), &v)) == OK);
    EXPECT_STR(v, S("a/b\xc3\xa9\xf0\x9f\x98\x80\t"));
    Str text = S("");
    EXPECT((settings_string_text(&t.a, S("a\"b\\c\n"), &text)) == OK);
    EXPECT_STR(text, S("\"a\\\"b\\\\c\\u000a\""));
    arena_deinit(&t.a);
}

TEST(settings_a_text_that_does_not_parse_says_why_and_where) {
    Tree t;
    tree_init(&t);
    typedef struct {
        const char *text;
        const char *why;
        size_t at_;
    } Bad;
    static const Bad bad[] = {
        {"", "a value was expected", 0},
        {"tru", "a value was expected", 0},
        {"[", "a value was expected", 1},
        {"{\"a\":1,}", "a string was expected", 7},
        {"{\"a\" 1}", "a `:` was expected after a key", 5},
        {"[1 2]", "a `,` or `]` was expected in a list", 3},
        {"{\"a\":1 \"b\":2}", "a `,` or `}` was expected in an object", 7},
        {"{\"a\":\"x", "a string is not closed", 7},
        {"{} x", "there is more after the value", 3},
        {"01", "there is more after the value", 1},
        {"\"\\q\"", "a string has an escape JSON does not know", 2},
        {"\"\\u12g4\"", "a `\\u` escape needs four hex digits", 5},
        {"{\"a\":\"\x01\"}", "a control character is in a string", 6},
        {"-", "a number needs a digit here", 1},
        {"1.e5", "a number needs a digit here", 2},
    };
    SLICE_TYPE(Bads, const Bad);
    Bads all = {bad, sizeof bad / sizeof *bad};
    for (size_t i = 0; i < all.len; i++) {
        SettingsReader r;
        SettingsNode *root = NULL;
        Status s = settings_read(&r, &t.a, test_cs(at(all, i).text), &root);
        if (s == OK || !str_eq(r.why, test_cs(at(all, i).why)) || r.why_at != at(all, i).at_) {
            fprintf(stderr, "  %s: said %.*s at %zu\n", at(all, i).text, (int)r.why.len, r.why.ptr, r.why_at);
        }
        EXPECT(s == STOP);
        EXPECT_STR(r.why, test_cs(at(all, i).why));
        EXPECT_EQ(r.why_at, at(all, i).at_);
    }
    // Past 256 deep it is refused, not recursed into.
    Buf deep = buf_make(&t.a);
    for (int k = 0; k < 257; k++) EXPECT(buf_byte(&deep, '[') == OK);
    SettingsReader r;
    SettingsNode *root = NULL;
    EXPECT(settings_read(&r, &t.a, buf_view(&deep), &root) == STOP);
    EXPECT_STR(r.why, S("it nests deeper than 256"));
    EXPECT_EQ(r.why_at, 256);
    arena_deinit(&t.a);
}

// `text` read, judged against `rules` and, when it can hold them, given the
// missing ones: the written text, and the count missing in `*missing`.
static Str tree_add(Tree *t, Str text, Lits rules, size_t *missing, Str *why) {
    SettingsReader r;
    SettingsNode *root = NULL;
    *missing = SIZE_MAX;
    if (settings_read(&r, &t->a, text, &root) != OK) return S("");
    SettingsStrs want = SettingsStrs_make(&t->a);
    for (size_t k = 0; k < rules.len; k++) {
        if (SettingsStrs_push(&want, test_cs(at(rules, k))) != OK) return S("");
    }
    SettingsDenies d;
    if (settings_find_denies(&t->a, root, SettingsStrs_items(&want), &d, why) != OK) return S("");
    *missing = d.missing.len;
    Str out = S("");
    if (settings_add_denies(&t->a, root, &d) != OK || settings_write(&t->a, root, &out) != OK) return S("");
    return out;
}

TEST(settings_the_deny_rules_missing_are_found_and_added_inside_the_one_permissions) {
    Tree t;
    tree_init(&t);
    Lits rules = LITS("Read(//k/\x2a\x2a)", "Edit(//k/\x2a\x2a)");
    size_t missing = 0;
    Str why = S("");
    // `deny` holds one, spelled with escapes: the other goes after it, and
    // nothing else moves.
    Str got = tree_add(&t, S("{\"model\":\"x\",\"permissions\":{\"allow\":[\"Bash(ls)\"],\"deny\":[\"Read(\\/\\/k\\/\x2a\x2a)\"]},\"z\":0}"), rules, &missing, &why);
    EXPECT_EQ(missing, 1);
    EXPECT_STR(got, S("{\n"
                      "  \"model\": \"x\",\n"
                      "  \"permissions\": {\n"
                      "    \"allow\": [\n"
                      "      \"Bash(ls)\"\n"
                      "    ],\n"
                      "    \"deny\": [\n"
                      "      \"Read(\\/\\/k\\/\x2a\x2a)\",\n"
                      "      \"Edit(//k/\x2a\x2a)\"\n"
                      "    ]\n"
                      "  },\n"
                      "  \"z\": 0\n"
                      "}\n"));
    // No `permissions`: one is made, at the top level's end.
    got = tree_add(&t, S("{\"model\":\"x\"}"), rules, &missing, &why);
    EXPECT_EQ(missing, 2);
    EXPECT_STR(got, S("{\n  \"model\": \"x\",\n  \"permissions\": {\n    \"deny\": [\n      \"Read(//k/\x2a\x2a)\",\n      \"Edit(//k/\x2a\x2a)\"\n    ]\n  }\n}\n"));
    // `permissions` without `deny`: `deny` goes in it, never in a second block.
    got = tree_add(&t, S("{\"permissions\":{\"allow\":[]}}"), rules, &missing, &why);
    EXPECT_EQ(missing, 2);
    EXPECT_STR(got, S("{\n  \"permissions\": {\n    \"allow\": [],\n    \"deny\": [\n      \"Read(//k/\x2a\x2a)\",\n      \"Edit(//k/\x2a\x2a)\"\n    ]\n  }\n}\n"));
    // Both there: none missing, and a string that only resembles one (a
    // number, another rule) is not taken for it.
    got = tree_add(&t, S("{\"permissions\":{\"deny\":[1,\"Edit(//k/\x2a\x2a)\",\"Read(//k/\x2a)\",\"Read(//k/\x2a\x2a)\"]}}"), rules, &missing, &why);
    EXPECT_EQ(missing, 0);
    // A rule's `"` and `\` are escaped when it is written.
    got = tree_add(&t, S("{}"), LITS("Read(//a\"b\\c/\x2a\x2a)"), &missing, &why);
    EXPECT_EQ(missing, 1);
    EXPECT_STR(got, S("{\n  \"permissions\": {\n    \"deny\": [\n      \"Read(//a\\\"b\\\\c/\x2a\x2a)\"\n    ]\n  }\n}\n"));
    arena_deinit(&t.a);
}

TEST(settings_a_tree_that_cannot_hold_the_rules_is_refused) {
    Tree t;
    tree_init(&t);
    typedef struct {
        const char *text;
        const char *why;
    } Bad;
    static const Bad bad[] = {
        {"[]", "its top level is not an object"},
        {"{\"permissions\":\"x\"}", "its `permissions` is not an object"},
        {"{\"permissions\":{\"deny\":{}}}", "its `permissions.deny` is not a list"},
        {"{\"permissions\":{},\"permissions\":{}}", "its top level names `permissions` twice"},
        {"{\"permissions\":{\"deny\":[],\"deny\":[]}}", "its `permissions` names `deny` twice"},
    };
    SLICE_TYPE(Bads, const Bad);
    Bads all = {bad, sizeof bad / sizeof *bad};
    for (size_t i = 0; i < all.len; i++) {
        size_t missing = 0;
        Str why = S("");
        Str got = tree_add(&t, test_cs(at(all, i).text), LITS("Read(//k/\x2a\x2a)"), &missing, &why);
        EXPECT_STR(got, S(""));
        EXPECT_EQ(missing, SIZE_MAX);
        EXPECT_STR(why, test_cs(at(all, i).why));
    }
    arena_deinit(&t.a);
}

// ── the mode, on a pseudo-terminal ──────────────────────────────────────

// A planted box: a root under c/out/secrets-tmp holding `keys` and `vault`
// (secrets folders), `plain` and `home`, the mounts a test names, and the
// mode over them.
typedef struct {
    bool refused;
    Arena a;
    char root_bytes[PATH_MAX];
    Str root;
    Secrets m;
} Sf;

// `lit` with each `@` the root's path.
static Str sf_at(Sf *f, const char *lit) { // cstr: a test's literal
    Buf b = buf_make(&f->a);
    for (const char *p = lit; *p != 0; p++) {
        Status s = *p == '@' ? buf_str(&b, f->root) : buf_byte(&b, *p);
        if (s != OK) abort();
    }
    return buf_view(&b);
}

// The root's `rel`, NUL-ended for the kernel.
static const char *sf_path(Sf *f, const char *rel) { // cstr: a test's literal, and the kernel's path
    Buf b = buf_make(&f->a);
    if (buf_str(&b, f->root) != OK || buf_byte(&b, '/') != OK || buf_str(&b, test_cs(rel)) != OK || buf_byte(&b, 0) != OK) abort();
    return b.ptr;
}

static void sf_init(Sf *f, const char *mounts) { // cstr: a test's literal
    // This unit makes no store call, so nothing calls path_store_failed,
    // which cannot be `static inline` as other functions this unit leaves
    // alone are: TCC 0.9.27 then refuses path_run.c ("index too large").
    // Naming it here is a use, for GCC's unused-function check.
    (void)path_store_failed;
    memset(f, 0, sizeof *f);
    arena_init(&f->a, (size_t)256 << 20, &f->refused);
    mkdir("c/out", 0777);
    mkdir("c/out/secrets-tmp", 0777);
    char tmpl[] = "c/out/secrets-tmp/t.XXXXXX";
    if (mkdtemp(tmpl) == NULL || realpath(tmpl, f->root_bytes) == NULL) abort();
    f->root = test_cs(f->root_bytes);
    if (mkdir(sf_path(f, "keys"), 0700) != 0 || mkdir(sf_path(f, "vault"), 0700) != 0) abort();
    if (mkdir(sf_path(f, "plain"), 0777) != 0 || mkdir(sf_path(f, "home"), 0777) != 0) abort();
    Mounts ms;
    Str why = S("");
    if (mounts_parse(&f->a, sf_at(f, mounts), sf_at(f, "@/home"), &ms, &why) != MOUNTS_OK) {
        fprintf(stderr, "mounts: %.*s\n", (int)why.len, why.ptr);
        abort();
    }
    Secrets m = {&f->a, {-1, -1, -1}, sf_at(f, "@/home"), f->root, Mounts_items(&ms), S("the planted mounts")};
    f->m = m;
}

static int sf_remove_one(const char *p, const struct stat *st, int kind, struct FTW *w) { // cstr: nftw's path
    (void)st;
    (void)w;
    return kind == FTW_DP ? rmdir(p) : unlink(p);
}

// Removes the planted root and everything in it, then the arena.
static void sf_deinit(Sf *f) {
    if (nftw(f->root_bytes, sf_remove_one, 16, FTW_DEPTH | FTW_PHYS) != 0) abort();
    arena_deinit(&f->a);
}

static void sf_put(Sf *f, const char *rel, const char *text) { // cstr: test literals
    int fd = open(sf_path(f, rel), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    Str s = test_cs(text);
    if (fd < 0 || write(fd, s.ptr, s.len) != (ssize_t)s.len) abort();
    close(fd);
}

// The file's bytes, or "<none>" when it is not there.
static Str sf_read(Sf *f, const char *rel) { // cstr: a test's literal
    Str out = S("");
    Str why = S("");
    secrets_read_file(&f->a, test_cs(sf_path(f, rel)), (size_t)1 << 20, &out, &why);
    return why.len > 0 ? S("<none>") : out;
}

static unsigned sf_mode(Sf *f, const char *rel) { // cstr: a test's literal
    struct stat st;
    if (stat(sf_path(f, rel), &st) != 0) return 0;
    return (unsigned)(st.st_mode & 07777);
}

// How many entries a folder holds, `.` and `..` left out.
static size_t sf_count(Sf *f, const char *rel) { // cstr: a test's literal
    DIR *d = opendir(sf_path(f, rel));
    if (d == NULL) return SIZE_MAX;
    size_t n = 0;
    for (struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        Str name = test_cs(e->d_name);
        if (!str_eq(name, S(".")) && !str_eq(name, S(".."))) n++;
    }
    closedir(d);
    return n;
}

// The mode run in a child on a pseudo-terminal, and what it printed so far
// (its `\r`s dropped).
typedef struct {
    int master;
    pid_t pid;
    Buf seen;
    // Where the next tty_expect starts looking.
    size_t from;
} Tty;

static void tty_start(Tty *t, Sf *f) {
    t->seen = buf_make(&f->a);
    t->from = 0;
    pid_t pid = forkpty(&t->master, NULL, NULL, NULL);
    if (pid < 0) abort();
    if (pid == 0) {
        SecretsIo io = {0, 1, 2};
        f->m.io = io;
        _exit(secrets_main(&f->m));
    }
    t->pid = pid;
}

// Takes what the child printed, waiting at most `ms`: 1 when some came, 0
// when none did in time, -1 once the child's side is closed.
static int tty_pull(Tty *t, int ms) {
    struct pollfd p = {t->master, POLLIN, 0};
    int r = poll(&p, 1, ms);
    if (r == 0) return 0;
    if (r < 0) return -1;
    char chunk[4096];
    ssize_t n = read(t->master, chunk, sizeof chunk);
    if (n <= 0) return -1;
    Bytes got = {chunk, (size_t)n};
    for (size_t k = 0; k < got.len; k++) {
        if (at(got, k) != '\r' && buf_byte(&t->seen, at(got, k)) != OK) abort();
    }
    return 1;
}

// Whether the child prints `text` after what the last expect saw, within
// five seconds.
static bool tty_expect(Tty *t, const char *text) { // cstr: a test's literal
    Str want = test_cs(text);
    for (;;) {
        Str seen = buf_view(&t->seen);
        Str rest = str_sub(seen, t->from, seen.len);
        size_t k = mem_find(rest.ptr, rest.len, want.ptr, want.len);
        if (k != SIZE_MAX) {
            t->from += k + want.len;
            return true;
        }
        if (tty_pull(t, 5000) <= 0) {
            fprintf(stderr, "  never printed: %s\n  printed: %.*s\n", text, (int)seen.len, seen.ptr);
            return false;
        }
    }
}

static void tty_send(Tty *t, const char *line) { // cstr: a test's literal
    Str s = test_cs(line);
    if (write(t->master, s.ptr, s.len) != (ssize_t)s.len || write(t->master, "\n", 1) != 1) abort();
}

// Reads the rest the child prints and waits for it: its exit code.
static int tty_end(Tty *t) {
    for (int k = 0; k < 1000 && tty_pull(t, 5000) > 0; k++) continue;
    int status = 0;
    if (waitpid(t->pid, &status, 0) != t->pid) return -1;
    close(t->master);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool tty_said(Tty *t, const char *text) { // cstr: a test's literal
    Str seen = buf_view(&t->seen);
    Str want = test_cs(text);
    return mem_find(seen.ptr, seen.len, want.ptr, want.len) != SIZE_MAX;
}

#define ONE_SECRETS_MOUNT "keys = @/keys secrets\nplain = @/plain\n"

// What deny writes to a settings file that had none, for the `keys` mount.
#define DENY_ONLY "{\n  \"permissions\": {\n    \"deny\": [\n      \"Read(/@/keys/\x2a\x2a)\",\n      \"Edit(/@/keys/\x2a\x2a)\"\n    ]\n  }\n}\n"

TEST(the_mode_refuses_unless_stdin_and_stdout_are_both_a_terminal) {
    Sf f;
    sf_init(&f, ONE_SECRETS_MOUNT);
    // Each pipe's read end, then its write end.
    SLICE_TYPE(Fds, int);
    int in_fds[2];
    int out_fds[2];
    int err_fds[2];
    Fds in = {in_fds, 2};
    Fds out = {out_fds, 2};
    Fds err = {err_fds, 2};
    EXPECT(pipe(in_fds) == 0 && pipe(out_fds) == 0 && pipe(err_fds) == 0);
    SecretsIo io = {at(in, 0), at(out, 1), at(err, 1)};
    f.m.io = io;
    EXPECT_EQ(secrets_main(&f.m), 2);
    // A terminal on stdin alone is refused all the same.
    int master = -1;
    int slave = -1;
    EXPECT(openpty(&master, &slave, NULL, NULL, NULL) == 0);
    SecretsIo half = {slave, at(out, 1), at(err, 1)};
    f.m.io = half;
    EXPECT_EQ(secrets_main(&f.m), 2);
    close(at(out, 1));
    close(at(err, 1));
    char said[1024];
    Bytes room = {said, sizeof said};
    ssize_t n = read(at(err, 0), said, sizeof said);
    EXPECT(n > 0);
    Str s = {room.ptr, n > 0 ? (size_t)n : 0};
    Str line = S("shale --secrets: run it in your own terminal: it reads a secret from the keyboard, so its stdin and its stdout must both be a terminal\n");
    EXPECT_STR(s, secrets_cat(&f.m, line, line, S("")));
    // Nothing went to stdout, and nothing was made.
    EXPECT_EQ(read(at(out, 0), said, sizeof said), 0);
    EXPECT_EQ(sf_count(&f, "keys"), 0);
    EXPECT_EQ(sf_count(&f, "home"), 0);
    close(master);
    close(slave);
    sf_deinit(&f);
}

TEST(the_menu_refuses_a_choice_it_does_not_offer_and_mounts_with_no_secrets_mount) {
    Sf f;
    sf_init(&f, ONE_SECRETS_MOUNT);
    Tty t;
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "  1. where: show each secrets mount's folder\n  2. store: store a new secret\n  3. deny: add the deny rules to Claude Code's settings\nWhich one (1, 2 or 3)? "));
    tty_send(&t, "4");
    EXPECT_EQ(tty_end(&t), 2);
    EXPECT(tty_said(&t, "shale --secrets: `4` is not 1, 2 or 3\n"));
    sf_deinit(&f);
    sf_init(&f, "plain = @/plain\n");
    tty_start(&t, &f);
    EXPECT_EQ(tty_end(&t), 2);
    EXPECT_STR(buf_view(&t.seen), S("shale --secrets: no mount is a `secrets` mount (the planted mounts): add a line such as `keys = ~/.shale-keys secrets` first\n"));
    sf_deinit(&f);
}

TEST(where_names_each_secrets_mount_and_its_folder_and_no_other_mount) {
    Sf f;
    sf_init(&f, "keys = @/keys secrets\nplain = @/plain\nvault = @/vault/ secrets\n");
    Tty t;
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
    tty_send(&t, "1");
    EXPECT_EQ(tty_end(&t), 0);
    Str seen = buf_view(&t.seen);
    Str tail = str_sub(seen, t.from, seen.len);
    // The answer echoed, then the two mounts, nothing else.
    EXPECT_STR(tail, sf_at(&f, "1\n  keys: @/keys\n  vault: @/vault\n"));
    EXPECT_EQ(sf_count(&f, "home"), 0);
    sf_deinit(&f);
}

TEST(store_writes_the_value_0600_says_its_bytes_never_the_value_then_adds_the_deny_rules) {
    Sf f;
    sf_init(&f, ONE_SECRETS_MOUNT);
    Tty t;
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
    tty_send(&t, "2");
    EXPECT(tty_expect(&t, "The secret's name (a file directly in `keys`): "));
    tty_send(&t, "tok");
    EXPECT(tty_expect(&t, "The value (it is not shown as you type): "));
    tty_send(&t, "s3cret-VALUE-9x");
    EXPECT(tty_expect(&t, "stored keys:tok (15 bytes)\n"));
    EXPECT(tty_expect(&t, "they go in ~/.claude/settings.json's `permissions.deny`:\n"));
    Str rules = sf_at(&f, "  Read(/@/keys/\x2a\x2a)\n  Edit(/@/keys/\x2a\x2a)\nAdd them? [y/n] ");
    char rules_c[PATH_MAX * 3];
    EXPECT(rules.len < sizeof rules_c);
    snprintf(rules_c, sizeof rules_c, "%.*s", (int)rules.len, rules.ptr);
    EXPECT(tty_expect(&t, rules_c));
    tty_send(&t, "y");
    EXPECT(tty_expect(&t, "Added 2 rules to ~/.claude/settings.json.\n"));
    EXPECT_EQ(tty_end(&t), 0);
    // The secret: its bytes alone, mode 0600, and no temporary file beside it.
    EXPECT_STR(sf_read(&f, "keys/tok"), S("s3cret-VALUE-9x"));
    EXPECT_EQ(sf_mode(&f, "keys/tok"), 0600);
    EXPECT_EQ(sf_count(&f, "keys"), 1);
    // The settings file, made with its folder; there was no old one to keep.
    EXPECT_STR(sf_read(&f, "home/.claude/settings.json"), sf_at(&f, DENY_ONLY));
    EXPECT_STR(sf_read(&f, "home/.claude/settings.json.bak"), S("<none>"));
    EXPECT_EQ(sf_count(&f, "home/.claude"), 1);
    // The value was never echoed nor printed.
    EXPECT(!tty_said(&t, "s3cret"));
    EXPECT(!tty_said(&t, "VALUE-9x"));
    sf_deinit(&f);
}

TEST(store_over_a_secret_asks_first_a_no_keeps_it_a_yes_replaces_it) {
    Sf f;
    sf_init(&f, "keys = @/keys secrets\nvault = @/vault secrets\n");
    sf_put(&f, "keys/tok", "old-bytes");
    // Both mounts' rules are there already, so no deny question follows.
    if (mkdir(sf_path(&f, "home/.claude"), 0700) != 0) abort();
    Str settings = sf_at(&f, "{\"permissions\":{\"deny\":[\"Read(/@/keys/\x2a\x2a)\",\"Edit(/@/keys/\x2a\x2a)\",\"Read(/@/vault/\x2a\x2a)\",\"Edit(/@/vault/\x2a\x2a)\"]}}");
    char settings_c[PATH_MAX * 5];
    snprintf(settings_c, sizeof settings_c, "%.*s", (int)settings.len, settings.ptr);
    sf_put(&f, "home/.claude/settings.json", settings_c);
    struct stat before;
    EXPECT(stat(sf_path(&f, "home/.claude/settings.json"), &before) == 0);
    Tty t;
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
    tty_send(&t, "store");
    EXPECT(tty_expect(&t, "Which mount (keys, vault)? "));
    tty_send(&t, "keys");
    EXPECT(tty_expect(&t, "The secret's name (a file directly in `keys`): "));
    tty_send(&t, "tok");
    EXPECT(tty_expect(&t, "keys:tok is there already. Replace it? [y/n] "));
    tty_send(&t, "n");
    EXPECT(tty_expect(&t, "Nothing is stored.\n"));
    EXPECT_EQ(tty_end(&t), 0);
    EXPECT(!tty_said(&t, "The value"));
    EXPECT_STR(sf_read(&f, "keys/tok"), S("old-bytes"));
    EXPECT_EQ(sf_count(&f, "keys"), 1);
    // A yes replaces it.
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
    tty_send(&t, "2");
    EXPECT(tty_expect(&t, "Which mount (keys, vault)? "));
    tty_send(&t, "keys");
    EXPECT(tty_expect(&t, "): "));
    tty_send(&t, "tok");
    EXPECT(tty_expect(&t, "Replace it? [y/n] "));
    tty_send(&t, "y");
    EXPECT(tty_expect(&t, "The value (it is not shown as you type): "));
    tty_send(&t, "new-PASS-77");
    EXPECT(tty_expect(&t, "stored keys:tok (11 bytes)\n"));
    EXPECT_EQ(tty_end(&t), 0);
    EXPECT_STR(sf_read(&f, "keys/tok"), S("new-PASS-77"));
    EXPECT_EQ(sf_mode(&f, "keys/tok"), 0600);
    EXPECT_EQ(sf_count(&f, "keys"), 1);
    EXPECT_EQ(sf_count(&f, "vault"), 0);
    EXPECT(!tty_said(&t, "new-PASS"));
    EXPECT(!tty_said(&t, "Add them"));
    // The settings file, already right, was not written: its bytes, its
    // inode and its time are as they were, and there is no .bak.
    struct stat after;
    EXPECT(stat(sf_path(&f, "home/.claude/settings.json"), &after) == 0);
    EXPECT_STR(sf_read(&f, "home/.claude/settings.json"), settings);
    EXPECT_EQ(after.st_ino, before.st_ino);
    EXPECT_EQ(after.st_mtim.tv_sec, before.st_mtim.tv_sec);
    EXPECT_EQ(after.st_mtim.tv_nsec, before.st_mtim.tv_nsec);
    EXPECT_EQ(sf_count(&f, "home/.claude"), 1);
    sf_deinit(&f);
}

TEST(store_refuses_a_name_that_is_not_a_file_directly_in_the_mount) {
    typedef struct {
        const char *name;
        const char *says;
    } Bad;
    static const Bad bad[] = {
        {"sub/x", "shale --secrets: `run`'s `stdout_secret` \"keys:sub/x\" is in a folder of the mount: a secret is a file directly in it, `\"keys:NAME\"`; nothing is stored\n"},
        {"..", "shale --secrets: \"keys:..\" climbs out of its mount with `..`: a folder beside it is reached through a mount of its own (`shale --help` lists this box's mounts); nothing is stored\n"},
        {"a:b", "shale --secrets: \"keys:a:b\" has a part (\"a:b\") that is not a name: no `.`, `..`, empty parts or characters FAT refuses; nothing is stored\n"},
        {".", "shale --secrets: \"keys:.\": a secret is a file in the mount, not the mount; nothing is stored\n"},
    };
    SLICE_TYPE(Bads, const Bad);
    Bads all = {bad, sizeof bad / sizeof *bad};
    for (size_t i = 0; i < all.len; i++) {
        Sf f;
        sf_init(&f, ONE_SECRETS_MOUNT);
        if (mkdir(sf_path(&f, "keys/sub"), 0700) != 0) abort();
        Tty t;
        tty_start(&t, &f);
        EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
        tty_send(&t, "2");
        EXPECT(tty_expect(&t, "): "));
        tty_send(&t, at(all, i).name);
        EXPECT_EQ(tty_end(&t), 1);
        EXPECT(tty_expect(&t, at(all, i).says));
        EXPECT(!tty_said(&t, "The value"));
        EXPECT_EQ(sf_count(&f, "keys"), 1);
        EXPECT_EQ(sf_count(&f, "keys/sub"), 0);
        EXPECT_EQ(sf_count(&f, "home"), 0);
        sf_deinit(&f);
    }
}

TEST(store_an_empty_value_stores_nothing) {
    Sf f;
    sf_init(&f, ONE_SECRETS_MOUNT);
    Tty t;
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
    tty_send(&t, "2");
    EXPECT(tty_expect(&t, "): "));
    tty_send(&t, "tok");
    EXPECT(tty_expect(&t, "The value (it is not shown as you type): "));
    tty_send(&t, "");
    EXPECT(tty_expect(&t, "The value is empty: nothing is stored.\n"));
    EXPECT_EQ(tty_end(&t), 0);
    EXPECT_EQ(sf_count(&f, "keys"), 0);
    EXPECT_EQ(sf_count(&f, "home"), 0);
    sf_deinit(&f);
}

TEST(deny_adds_only_what_is_missing_keeps_the_old_file_as_bak_and_leaves_a_correct_file_untouched) {
    Sf f;
    sf_init(&f, ONE_SECRETS_MOUNT);
    if (mkdir(sf_path(&f, "home/.claude"), 0700) != 0) abort();
    Str old = sf_at(&f, "{\"model\": \"opus\", \"permissions\": {\"deny\": [\"Read(/@/keys/\x2a\x2a)\"]}}");
    char old_c[PATH_MAX * 2];
    snprintf(old_c, sizeof old_c, "%.*s", (int)old.len, old.ptr);
    sf_put(&f, "home/.claude/settings.json", old_c);
    // An earlier .bak is overwritten.
    sf_put(&f, "home/.claude/settings.json.bak", "earlier");
    Tty t;
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
    tty_send(&t, "3");
    EXPECT(tty_expect(&t, "Add them? [y/n] "));
    EXPECT(!tty_said(&t, "Read(/"));
    tty_send(&t, "y");
    EXPECT(tty_expect(&t, "Added 1 rule to ~/.claude/settings.json (the old file is ~/.claude/settings.json.bak).\n"));
    EXPECT_EQ(tty_end(&t), 0);
    EXPECT(tty_said(&t, "Edit(/"));
    EXPECT_STR(sf_read(&f, "home/.claude/settings.json"),
               sf_at(&f, "{\n  \"model\": \"opus\",\n  \"permissions\": {\n    \"deny\": [\n      \"Read(/@/keys/\x2a\x2a)\",\n      \"Edit(/@/keys/\x2a\x2a)\"\n    ]\n  }\n}\n"));
    EXPECT_STR(sf_read(&f, "home/.claude/settings.json.bak"), old);
    EXPECT_EQ(sf_count(&f, "home/.claude"), 2);
    // Run again, it finds nothing missing and writes nothing.
    Str now = sf_read(&f, "home/.claude/settings.json");
    struct stat before;
    EXPECT(stat(sf_path(&f, "home/.claude/settings.json"), &before) == 0);
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
    tty_send(&t, "deny");
    EXPECT(tty_expect(&t, "~/.claude/settings.json holds every deny rule already: nothing is written.\n"));
    EXPECT_EQ(tty_end(&t), 0);
    struct stat after;
    EXPECT(stat(sf_path(&f, "home/.claude/settings.json"), &after) == 0);
    EXPECT_STR(sf_read(&f, "home/.claude/settings.json"), now);
    EXPECT_EQ(after.st_ino, before.st_ino);
    EXPECT_EQ(after.st_mtim.tv_sec, before.st_mtim.tv_sec);
    EXPECT_EQ(after.st_mtim.tv_nsec, before.st_mtim.tv_nsec);
    EXPECT_STR(sf_read(&f, "home/.claude/settings.json.bak"), old);
    sf_deinit(&f);
}

TEST(deny_a_file_that_does_not_parse_or_a_no_writes_nothing) {
    Sf f;
    sf_init(&f, ONE_SECRETS_MOUNT);
    if (mkdir(sf_path(&f, "home/.claude"), 0700) != 0) abort();
    sf_put(&f, "home/.claude/settings.json", "{\"model\": \"opus\",}");
    Tty t;
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
    tty_send(&t, "3");
    EXPECT_EQ(tty_end(&t), 1);
    EXPECT(tty_expect(&t, "shale --secrets: ~/.claude/settings.json does not parse: a string was expected at byte 17; nothing is written\n"));
    EXPECT(!tty_said(&t, "Add them"));
    EXPECT_STR(sf_read(&f, "home/.claude/settings.json"), S("{\"model\": \"opus\",}"));
    EXPECT_EQ(sf_count(&f, "home/.claude"), 1);
    // A no leaves a file that parses as it was, with no .bak.
    sf_put(&f, "home/.claude/settings.json", "{}");
    tty_start(&t, &f);
    EXPECT(tty_expect(&t, "Which one (1, 2 or 3)? "));
    tty_send(&t, "3");
    EXPECT(tty_expect(&t, "Add them? [y/n] "));
    tty_send(&t, "n");
    EXPECT(tty_expect(&t, "Nothing is written.\n"));
    EXPECT_EQ(tty_end(&t), 0);
    EXPECT_STR(sf_read(&f, "home/.claude/settings.json"), S("{}"));
    EXPECT_EQ(sf_count(&f, "home/.claude"), 1);
    sf_deinit(&f);
}

TEST_MAIN(TEST_CASE(settings_a_file_read_and_written_back_keeps_its_order_its_texts_and_lays_out_two_spaces_a_level),
          TEST_CASE(settings_a_text_that_does_not_parse_says_why_and_where),
          TEST_CASE(settings_the_deny_rules_missing_are_found_and_added_inside_the_one_permissions),
          TEST_CASE(settings_a_tree_that_cannot_hold_the_rules_is_refused),
          TEST_CASE(the_mode_refuses_unless_stdin_and_stdout_are_both_a_terminal),
          TEST_CASE(the_menu_refuses_a_choice_it_does_not_offer_and_mounts_with_no_secrets_mount),
          TEST_CASE(where_names_each_secrets_mount_and_its_folder_and_no_other_mount),
          TEST_CASE(store_writes_the_value_0600_says_its_bytes_never_the_value_then_adds_the_deny_rules),
          TEST_CASE(store_over_a_secret_asks_first_a_no_keeps_it_a_yes_replaces_it),
          TEST_CASE(store_refuses_a_name_that_is_not_a_file_directly_in_the_mount),
          TEST_CASE(store_an_empty_value_stores_nothing),
          TEST_CASE(deny_adds_only_what_is_missing_keeps_the_old_file_as_bak_and_leaves_a_correct_file_untouched),
          TEST_CASE(deny_a_file_that_does_not_parse_or_a_no_writes_nothing))
