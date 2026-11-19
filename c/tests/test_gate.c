// test_gate.c: c/gate/gate.c, shale-gate's decision. The first seven tests
// are the Zig gate's (legacy_zig/tools/shale_gate_test.zig), the same
// commands held to the same answers; the rest hold what those leave open:
// the refusals' whole texts, the limits (64 words, 4096 lines, a refusal
// past 1024 bytes), the log line's cut, the hook's JSON as std.json reads
// it, an escape's one append, and `shale --secrets` refused.
// c/build/gate.shale holds the program to the Zig gate's answers.
#include "test.h"
#include "../gate/gate.c"

#include <limits.h>

enum { TEST_GATE_LIMIT = 256 * 1024 * 1024 };

#define BOTH "`shale [--verbose] [--undo-on-stop] [FILE [--args-stdin]] [NAME=VALUE]... <<'TAG'`"

static Str test_cs(const char *s) { // cstr: a test's literal
    return (Str){s, strlen(s)};
}

static GateVerdict classified(Arena *a, Str cmd) {
    GateVerdict v;
    gate_classify(a, cmd, &v);
    return v;
}

static void passes_str(Str cmd) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    GateVerdict v = classified(&a, cmd);
    if (v.kind != GATE_LAUNCH) fprintf(stderr, "  refused or escaped: %.*s\n  %.*s\n", (int)cmd.len, cmd.ptr, (int)v.why.len, v.why.ptr);
    arena_deinit(&a);
    EXPECT(v.kind == GATE_LAUNCH);
}

static void passes(const char *cmd) { // cstr: a test's literal
    passes_str(test_cs(cmd));
}

// Whether the refusal of `cmd` (its bytes `len`, so a NUL may be in it)
// holds `says`.
static bool refused_saying(Str cmd, const char *says) { // cstr: a test's literal
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    GateVerdict v = classified(&a, cmd);
    bool ok = v.kind == GATE_REFUSED && gate_index_of(v.why, test_cs(says)) != SIZE_MAX;
    if (!ok) fprintf(stderr, "  %.*s\n  said: %.*s\n", (int)cmd.len, cmd.ptr, (int)v.why.len, v.why.ptr);
    arena_deinit(&a);
    return ok;
}

static void refuses(const char *cmd, const char *says) { // cstr: test literals
    EXPECT(refused_saying(test_cs(cmd), says));
}

TEST(the_launch_every_form_the_design_names) {
    passes("shale count.shale");
    passes("shale --verbose rename.shale from=old to=new");
    passes("shale --budget steps=99 --verbose x.shale");
    passes("/home/x/shale/zig-out/bin/shale a.shale");
    passes("cd ../roc-chat && shale a.shale");
    passes("shale <<'SHALE'\nprint(1)\nSHALE");
    passes("cd /home/x && shale <<'END_IT'\nprint(\"a $b `c` ; | & < > ( ) * ?\")\nEND_IT  ");
    passes("shale n=3 <<'SHALE'\nprint(arg(\"n\", 0))\nSHALE\n");
    passes("shale --undo-on-stop ~/s/split.shale --args-stdin <<'ARGS'\nnames=a|b c\ndoc=It's \"x\"\nARGS");
}

TEST(what_it_refuses_naming_both_forms) {
    refuses("ls -la", BOTH);
    refuses("ls -la", "`# want: <tag>: <text>`");
    refuses("shale -e 'print(1)'", BOTH);
    refuses("shale a.shale; rm -rf x", BOTH);
    refuses("shale a.shale && echo done", BOTH);
    refuses("shale $FILE", BOTH);
    refuses("shale a.shale | head", BOTH);
    refuses("shale `x`", BOTH);
    refuses("SHALE_ROOT=x shale a.shale", BOTH);
    refuses("/usr/bin/shalex a.shale", BOTH);
    refuses("shale --budget", BOTH);
    refuses("shale --budget steps", BOTH);
    refuses("shale --root here a.shale", BOTH);
    refuses("shale --write a.shale", BOTH);
    refuses("cd a b && shale x.shale", BOTH);
    refuses("cd $HOME && shale x.shale", BOTH);
    refuses("shale a.shale\nrm x", BOTH);
    refuses("", BOTH);
    refuses("ls -la", "`shale --help`");
}

TEST(shale_help_which_every_refusal_points_to_passes) {
    passes("shale --help");
    passes("shale --version");
}

TEST(a_heredocs_failure_modes) {
    refuses("shale <<SHALE\nprint(1)\nSHALE", BOTH);
    refuses("shale <<\"SHALE\"\nprint(1)\nSHALE", BOTH);
    refuses("shale <<'shale'\nprint(1)\nshale", BOTH);
    refuses("shale <<'SHALE'\nprint(1)\n", "no last line `SHALE`");
    refuses("shale <<'SHALE'\nprint(1)\nSHALE\nrm -rf /\nSHALE", "`SHALE` twice");
    refuses("shale <<'SHALE'\nprint(1)\nSHALE\nrm -rf /", "no last line `SHALE`");
    refuses("shale <<'SHALE' x.shale\nprint(1)\nSHALE", BOTH);
    refuses("shale <<'SHALE'", "no last line `SHALE`");
}

TEST(the_escape_a_want_or_an_out_of_scope_its_tag_and_text_read) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    GateVerdict w = classified(&a, S("# want: sed-in-place: one replacement in a big file\nsed -i s/a/b/ x"));
    EXPECT(w.kind == GATE_ESCAPE);
    EXPECT_STR(w.escape_kind, S("want"));
    EXPECT_STR(w.tag, S("sed-in-place"));
    EXPECT_STR(w.text, S("one replacement in a big file"));
    GateVerdict o = classified(&a, S("# out-of-scope: qemu: a KVM gate\nsudo ./gates.sh"));
    EXPECT(o.kind == GATE_ESCAPE);
    EXPECT_STR(o.escape_kind, S("out-of-scope"));
    EXPECT_STR(o.tag, S("qemu"));
    EXPECT_STR(o.text, S("a KVM gate"));
    // The text is trimmed of spaces, tabs and returns at both ends.
    GateVerdict t = classified(&a, S("# want: t: \t why \r\nls"));
    EXPECT(t.kind == GATE_ESCAPE);
    EXPECT_STR(t.text, S("why"));
    arena_deinit(&a);
    refuses("# want: Bad_Tag: x\nls", "`# want: <tag>: <text>`");
    refuses("# want: tag:\nls", "`# want: <tag>: <text>`");
    refuses("# want: tag\nls", "`# want: <tag>: <text>`");
    refuses("# wanted: tag: x\nls", "`# want: <tag>: <text>`");
    refuses(" # want: tag: x\nls", "`# want: <tag>: <text>`");
    refuses("# want: tag:  \t\nls", "`# want: <tag>: <text>`");
}

TEST(a_log_line_is_one_line_tabs_and_newlines_shown_the_commands_first_160_bytes) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    char cmd_bytes[19 + 300];
    Bytes cmd = {cmd_bytes, sizeof cmd_bytes};
    mem_copy(cmd_bytes, "# want: t: why\tnot\n", 19);
    for (size_t i = 19; i < cmd.len; i++) at(cmd, i) = 'x';
    GateVerdict e = {GATE_ESCAPE, S("want"), S("t"), S("why\tnot"), S(""), false, S("")};
    Buf b = buf_make(&a);
    EXPECT(gate_log_line(&b, S("2026-10-17T12:00:00Z"), S("sess-1"), &e, (Str){cmd.ptr, cmd.len}) == OK);
    Str line = buf_view(&b);
    EXPECT(gate_ends_with(line, S("\n")));
    size_t newlines = 0;
    for (size_t i = 0; i < line.len; i++) newlines += str_byte(line, i) == '\n';
    EXPECT_EQ(newlines, 1);
    EXPECT(str_starts_with(line, S("2026-10-17T12:00:00Z\tsess-1\twant\tt\twhy\\tnot\t# want: t: why\\tnot\\n")));
    // The command's first 160 bytes: 19, two of them escaped, then 141 x.
    EXPECT_EQ(line.len, 20 + 1 + 6 + 1 + 4 + 1 + 1 + 1 + 8 + 1 + (160 + 2) + 1);
    EXPECT(line.len < 4096);
    // A text past the line's room is cut, never the line.
    char long_bytes[5000];
    memset(long_bytes, 'y', sizeof long_bytes);
    GateVerdict big = {GATE_ESCAPE, S("want"), S("t"), {long_bytes, sizeof long_bytes}, S(""), false, S("")};
    Buf c = buf_make(&a);
    EXPECT(gate_log_line(&c, S("d"), S("s"), &big, S("c")) == OK);
    Str cut = buf_view(&c);
    EXPECT(cut.len < 4096 && gate_ends_with(cut, S("\n")));
    EXPECT_EQ(cut.len, 1 + 1 + 1 + 1 + 4 + 1 + 1 + 1 + 2048 + 1 + 1 + 1);
    arena_deinit(&a);
}

TEST(the_hooks_input_the_command_of_bash_and_monitor_any_other_tool_passed_input_it_cannot_read_refused) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    GateHook h;
    bool ok = false;
    EXPECT(gate_read_hook(&a, S("{\"session_id\":\"s1\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"shale x.shale\",\"timeout\":5}}"), &h, &ok) == OK);
    EXPECT(ok && h.has_command);
    EXPECT_STR(h.command, S("shale x.shale"));
    EXPECT_STR(h.session, S("s1"));
    EXPECT(gate_read_hook(&a, S("{\"session_id\":\"s1\",\"tool_name\":\"Monitor\",\"tool_input\":{\"command\":\"tail -f x\"}}"), &h, &ok) == OK);
    EXPECT(ok && h.has_command);
    EXPECT_STR(h.command, S("tail -f x"));
    EXPECT(gate_read_hook(&a, S("{\"session_id\":\"s1\",\"tool_name\":\"Read\",\"tool_input\":{\"file_path\":\"/x\"}}"), &h, &ok) == OK);
    EXPECT(ok && !h.has_command);
    // A subagent's call carries its `agent_id`; it reads like any other.
    EXPECT(gate_read_hook(&a, S("{\"session_id\":\"s1\",\"agent_id\":\"a-9\",\"agent_type\":\"Explore\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"ls\"}}"), &h, &ok) == OK);
    EXPECT(ok && h.has_command);
    EXPECT_STR(h.command, S("ls"));
    EXPECT(gate_read_hook(&a, S("not json"), &h, &ok) == OK);
    EXPECT(!ok);
    EXPECT(gate_read_hook(&a, S("{\"tool_name\":\"Bash\",\"tool_input\":{}}"), &h, &ok) == OK);
    EXPECT(!ok);
    arena_deinit(&a);
}

// The whole refusal of `cmd`.
static Str refusal_of(Arena *a, Str cmd) {
    GateVerdict v = classified(a, cmd);
    EXPECT(v.kind == GATE_REFUSED);
    return v.why;
}

TEST(each_refusal_is_its_whole_line_word_for_word) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    EXPECT_STR(refusal_of(&a, S("ls -la")), S("shale-gate: `ls` is not `shale`; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("# nope")), S("shale-gate: an escape is `# want: <tag>: <text>` or `# out-of-scope: <tag>: <text>`, the tag `[a-z0-9-]`; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("cd $X && shale a")), S("shale-gate: `cd $X` is not a plain folder; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("shale <<EOF")), S("shale-gate: `<<EOF`: a heredoc is `<<'TAG'`, quoted, the TAG `[A-Z_]+`; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("cd x &&")), S("shale-gate: no `shale`; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("shale a;b")), S("shale-gate: `a;b` is not a plain word; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("shale --budget x")), S("shale-gate: `--budget` takes `NAME=N`; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("shale --root d")), S("shale-gate: `--root` is not an option the gate passes; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("shale a\n b")), S("shale-gate: a launch is one line, or a heredoc's; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("shale <<'T'\nx")), S("shale-gate: the heredoc has no last line `T`; only " GATE_FORMS));
    EXPECT_STR(refusal_of(&a, S("shale <<'T'\nT\nT")), S("shale-gate: the heredoc meets `T` twice: a line before its end would close it; only " GATE_FORMS));
    // Each byte the shell would read as more than a word's.
    Str specials = S("'\"$`;|&<>()*?\\\t\r");
    for (size_t i = 0; i < specials.len; i++) {
        char word[4] = {'a', (char)str_byte(specials, i), 'b', '\0'};
        Buf cmd = buf_make(&a);
        EXPECT(buf_lit(&cmd, "shale ") == OK && buf_str(&cmd, test_cs(word)) == OK);
        Buf says = buf_make(&a);
        EXPECT(buf_lit(&says, "shale-gate: `") == OK && buf_str(&says, test_cs(word)) == OK && buf_lit(&says, "` is not a plain word; only " GATE_FORMS) == OK);
        EXPECT_STR(refusal_of(&a, buf_view(&cmd)), buf_view(&says));
    }
    // A launch's trailing blank lines are no second line.
    passes("shale a.shale\n \t\r\n");
    arena_deinit(&a);
}

TEST(the_limits_64_words_4096_lines_and_a_refusal_past_1024_bytes) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    Buf words = buf_make(&a);
    EXPECT(buf_lit(&words, "shale") == OK);
    for (int i = 1; i < 64; i++) EXPECT(buf_lit(&words, " w") == OK);
    passes_str(buf_view(&words));
    EXPECT(buf_lit(&words, " w") == OK);
    EXPECT_STR(refusal_of(&a, buf_view(&words)), S("shale-gate: too many words; only " GATE_FORMS));
    // 4096 lines, the last the tag, pass; one more is refused before any
    // other check of the body, and a final newline's empty piece is one
    // more while they are counted, as Zig's split counts it.
    Buf lines = buf_make(&a);
    EXPECT(buf_lit(&lines, "shale <<'T'") == OK);
    for (int i = 1; i < 4096; i++) EXPECT(buf_lit(&lines, "\nx") == OK);
    EXPECT(buf_lit(&lines, "\nT") == OK);
    passes_str(buf_view(&lines));
    EXPECT(buf_lit(&lines, "\n") == OK);
    EXPECT_STR(refusal_of(&a, buf_view(&lines)), S("shale-gate: a heredoc of more than 4096 lines; only " GATE_FORMS));
    // A refusal whose line would pass 1024 bytes is the plain one.
    Buf wide = buf_make(&a);
    EXPECT(buf_lit(&wide, "shale ") == OK);
    for (int i = 0; i < 1000; i++) EXPECT(buf_lit(&wide, "$") == OK);
    EXPECT_STR(refusal_of(&a, buf_view(&wide)), S("shale-gate: refused: only " GATE_FORMS));
    // A NUL is a plain byte to the gate, as to Zig's.
    passes_str((Str){"shale a\0b", 9});
    EXPECT(refused_saying((Str){"shale a\0;", 9}, "is not a plain word"));
    arena_deinit(&a);
}

TEST(a_log_line_past_its_room_is_cut_at_4095_bytes_mid_escape) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    // `d`, a tab, then a session of 4092 bytes and a tab: the line's room
    // ends inside the tab's escape, whose first byte alone is kept.
    Buf session = buf_make(&a);
    for (int i = 0; i < 4092; i++) EXPECT(buf_byte(&session, 's') == OK);
    EXPECT(buf_lit(&session, "\tzz") == OK);
    GateVerdict e = {GATE_ESCAPE, S("want"), S("t"), S("x"), S(""), false, S("")};
    Buf b = buf_make(&a);
    EXPECT(gate_log_line(&b, S("d"), buf_view(&session), &e, S("c")) == OK);
    Str line = buf_view(&b);
    EXPECT_EQ(line.len, 4096);
    EXPECT(gate_ends_with(line, S("ssss\\\n")));
    arena_deinit(&a);
}

// Whether std.json would read `json` as a hook the gate can read.
static bool reads(Str json) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    GateHook h;
    bool ok = false;
    EXPECT(gate_read_hook(&a, json, &h, &ok) == OK);
    arena_deinit(&a);
    return ok;
}

// The command `json`'s hook carries, read into `a`.
static Str command_of(Arena *a, Str json) {
    GateHook h;
    bool ok = false;
    EXPECT(gate_read_hook(a, json, &h, &ok) == OK);
    EXPECT(ok && h.has_command);
    return h.command;
}

TEST(the_hooks_json_is_read_as_std_json_reads_it) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    EXPECT(reads(S(" \t\r\n{\"tool_name\":\"Read\"} \n")));
    EXPECT(!reads(S("")));
    EXPECT(!reads(S("[]")));
    EXPECT(!reads(S("\"x\"")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\"} x")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\"}{}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"a\":[1,]}")));
    EXPECT(!reads(S("{\"tool_name\" \"Read\"}")));
    EXPECT(!reads(S("{'tool_name':\"Read\"}")));
    EXPECT(!reads(S("\xEF\xBB\xBF{\"tool_name\":\"Read\"}")));
    // A key twice, at the top or deep inside, even spelt two ways.
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"tool_name\":\"Read\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"x\":[{\"a\":1,\"b\":2,\"a\":3}]}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"x\":{\"a\":1,\"\\u0061\":2}}")));
    EXPECT(reads(S("{\"tool_name\":\"Read\",\"x\":{\"a\":1},\"y\":{\"a\":1}}")));
    // Numbers by RFC 8259's grammar, and the three literals.
    EXPECT(reads(S("{\"tool_name\":\"Read\",\"n\":[0,-0,1.5,-2e10,3E+2,4e-1,123456789012345678901234567890,1e999]}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"n\":01}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"n\":1.}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"n\":.5}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"n\":+1}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"n\":1e}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"n\":-}")));
    EXPECT(reads(S("{\"tool_name\":\"Read\",\"l\":[true,false,null]}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"l\":tru}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"l\":nul}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"l\":True}")));
    // Strings: escapes decoded, a surrogate pair joined, a half alone, a
    // control byte or bad UTF-8 refused.
    EXPECT_STR(command_of(&a, S("{\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"\\u0041\\/\\\"\\\\\\b\\f\\n\\r\\t\\u00e9\\ud83d\\ude00\"}}")), S("A/\"\\\b\f\n\r\t\xC3\xA9\xF0\x9F\x98\x80"));
    EXPECT_STR(command_of(&a, S("{\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"a\\u0000b\"}}")), ((Str){"a\0b", 3}));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\\ud83d\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\\ude00\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\\ud83d\\u0041\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\\x41\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\\u12g4\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"a\tb\"}")));
    EXPECT(reads(S("{\"tool_name\":\"Read\",\"s\":\"\x7F\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xF4\x8F\xBF\xBF\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\xC0\xAF\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\xE0\x80\xAF\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\xED\xA0\x80\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\xF4\x90\x80\x80\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\xF5\x80\x80\x80\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\xC3\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"\x80\"}")));
    EXPECT(!reads(S("{\"tool_name\":\"Read\",\"s\":\"abc")));
    // What the gate reads of it: `tool_name` a string, then for Bash a
    // `tool_input` object with a `command` string; a session not a string
    // is none.
    EXPECT(!reads(S("{\"tool_name\":1}")));
    EXPECT(!reads(S("{\"tool_name\":\"Bash\",\"tool_input\":[]}")));
    EXPECT(!reads(S("{\"tool_name\":\"Bash\",\"tool_input\":{\"command\":1}}")));
    EXPECT(!reads(S("{\"tool_name\":\"Bash\",\"tool_input\":{\"x\":{\"command\":\"ls\"}}}")));
    EXPECT(!reads(S("{\"tool_name\":\"Bash\",\"x\":{\"tool_input\":{\"command\":\"ls\"}}}")));
    EXPECT(reads(S("{\"tool_name\":\"Edit\",\"tool_input\":7}")));
    GateHook h;
    bool ok = false;
    EXPECT(gate_read_hook(&a, S("{\"session_id\":7,\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"x\"}}"), &h, &ok) == OK);
    EXPECT(ok);
    EXPECT_STR(h.session, S(""));
    arena_deinit(&a);
}

TEST(a_nesting_a_megabyte_deep_is_read_without_recursion) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    Buf b = buf_make(&a);
    EXPECT(buf_lit(&b, "{\"tool_name\":\"Read\",\"x\":") == OK);
    for (int i = 0; i < 300000; i++) EXPECT(buf_lit(&b, "[{\"a\":") == OK);
    EXPECT(buf_lit(&b, "1") == OK);
    for (int i = 0; i < 300000; i++) EXPECT(buf_lit(&b, "}]") == OK);
    EXPECT(buf_lit(&b, "}") == OK);
    EXPECT(reads(buf_view(&b)));
    arena_deinit(&a);
}

TEST(the_decision_each_way_the_gate_answers) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    GateDecision d;
    gate_decide(&a, false, S(""), &d);
    EXPECT_EQ(d.code, 2);
    EXPECT_STR(d.say, S("shale-gate: the hook's input could not be read: refused"));
    gate_decide(&a, true, S("not json"), &d);
    EXPECT_EQ(d.code, 2);
    EXPECT_STR(d.say, S("shale-gate: the hook's input is not the JSON it expects (tool_name, tool_input.command): refused"));
    gate_decide(&a, true, S("{\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"shale a.shale\"}}"), &d);
    EXPECT(d.code == 0 && !d.log && d.say.len == 0);
    gate_decide(&a, true, S("{\"tool_name\":\"Read\"}"), &d);
    EXPECT(d.code == 0 && !d.log && d.say.len == 0);
    gate_decide(&a, true, S("{\"tool_name\":\"Monitor\",\"tool_input\":{\"command\":\"tail -f x\"}}"), &d);
    EXPECT_EQ(d.code, 2);
    EXPECT_STR(d.say, S("shale-gate: `tail` is not `shale`; only " GATE_FORMS));
    gate_decide(&a, true, S("{\"session_id\":\"s7\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"# want: t: x\\nls\"}}"), &d);
    EXPECT(d.code == 0 && d.log && d.say.len == 0);
    EXPECT_STR(d.escape.tag, S("t"));
    EXPECT_STR(d.hook.session, S("s7"));
    arena_deinit(&a);
}

// A fresh folder under c/out/gate-tmp, absolute, as `home`.
static Str fresh_home(Arena *a) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == NULL) abort();
    mkdir("c/out", 0777);
    mkdir("c/out/gate-tmp", 0777);
    char tmpl[] = "c/out/gate-tmp/h.XXXXXX";
    if (mkdtemp(tmpl) == NULL) abort();
    Buf b = buf_make(a);
    if (buf_str(&b, test_cs(cwd)) != OK || buf_lit(&b, "/") != OK || buf_str(&b, test_cs(tmpl)) != OK) abort();
    return buf_view(&b);
}

// What the file `rel` under `home` holds.
static Str read_under(Arena *a, Str home, const char *rel) { // cstr: a test's literal
    Buf p = buf_make(a);
    if (buf_str(&p, home) != OK || buf_str(&p, test_cs(rel)) != OK) abort();
    int fd = open(arena_cstr(a, os_str_own(buf_view(&p))), O_RDONLY);
    if (fd < 0) return S("(none)");
    Buf b = buf_make(a);
    if (buf_reserve(&b, 1 << 16) != OK) abort();
    ssize_t n = read(fd, b.ptr, b.cap);
    close(fd);
    if (n < 0) abort();
    b.len = (size_t)n;
    return buf_view(&b);
}

static void remove_under(Arena *a, Str home) {
    Str part_items[] = {S_ITEM("/.local/state/shale/wants.tsv"), S_ITEM("/.local/state/shale"), S_ITEM("/.local/state"), S_ITEM("/.local"), S_ITEM("")};
    GateStrs parts = {part_items, sizeof part_items / sizeof *part_items};
    for (size_t i = 0; i < parts.len; i++) {
        Buf p = buf_make(a);
        if (buf_str(&p, home) != OK || buf_str(&p, at(parts, i)) != OK) abort();
        (void)remove(arena_cstr(a, os_str_own(buf_view(&p)))); // ignored: a test's clean-up of what may not be there
    }
}

SLICE_TYPE(TestGateCStrs, const char *); // cstr: test literals

// gate_run with no shale beside it: a launch by path refuses.
static int gate_run_here(Arena *a, bool read_ok, Str input, bool has_home, Str home) {
    return gate_run(a, read_ok, input, has_home, home, S("/nonexistent"));
}

// `<home>/<rel>` as a C string.
static const char *under(Arena *a, Str home, const char *rel) { // cstr: a test's literal
    Buf p = buf_make(a);
    if (buf_str(&p, home) != OK || buf_str(&p, test_cs(rel)) != OK) abort();
    return arena_cstr(a, os_str_own(buf_view(&p)));
}

// The hook's input for a Bash call of `cmd`, put in a JSON string as it is
// (so a `\n` in it is JSON's).
static Str bash_call(Arena *a, Str cmd) {
    Buf b = buf_make(a);
    if (buf_lit(&b, "{\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"") != OK || buf_str(&b, cmd) != OK || buf_lit(&b, "\"}}") != OK) abort();
    return buf_view(&b);
}

// The gate passes `/any/folder/shale`, so a file of that name that is a link
// to another program (a shell) would run a heredoc's body: a launch by path
// passes only as the shale beside the gate, linked to or not.
TEST(a_launch_by_path_is_the_shale_beside_the_gate) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    Str home = fresh_home(&a);
    EXPECT_EQ(mkdir(under(&a, home, "/bin"), 0700), 0);
    EXPECT_EQ(mkdir(under(&a, home, "/link"), 0700), 0);
    EXPECT_EQ(mkdir(under(&a, home, "/other"), 0700), 0);
    int fd = open(under(&a, home, "/bin/shale"), O_WRONLY | O_CREAT | O_EXCL, 0700);
    EXPECT(fd >= 0);
    close(fd);
    EXPECT_EQ(symlink(under(&a, home, "/bin/shale"), under(&a, home, "/link/shale")), 0);
    // The decoy: a link named `shale` to another program, never run here.
    EXPECT_EQ(symlink("/bin/true", under(&a, home, "/other/shale")), 0);
    Str beside = test_cs(under(&a, home, "/bin"));
    // Each refusal's line goes to stderr: kept out of the test's output.
    int saved = dup(2);
    int null = open("/dev/null", O_WRONLY);
    EXPECT(saved >= 0 && null >= 0 && dup2(null, 2) == 2);
    const char *ok_items[] = {"/bin/shale a.shale", "/link/shale a.shale"};
    const char *no_items[] = {"/other/shale a.shale", "/other/shale <<'SHALE'\\nprint(1)\\nSHALE", "/none/shale a.shale"};
    TestGateCStrs ok = {ok_items, sizeof ok_items / sizeof *ok_items};
    TestGateCStrs no = {no_items, sizeof no_items / sizeof *no_items};
    for (size_t i = 0; i < ok.len; i++) EXPECT_EQ(gate_run(&a, true, bash_call(&a, test_cs(under(&a, home, at(ok, i)))), false, S(""), beside), 0);
    for (size_t i = 0; i < no.len; i++) EXPECT_EQ(gate_run(&a, true, bash_call(&a, test_cs(under(&a, home, at(no, i)))), false, S(""), beside), 2);
    // A bare `shale` is PATH's, held to nothing here; `cd DIR && ` changes
    // nothing.
    EXPECT_EQ(gate_run(&a, true, bash_call(&a, S("shale a.shale")), false, S(""), beside), 0);
    Buf cd = buf_make(&a);
    EXPECT(buf_lit(&cd, "cd / && ") == OK && buf_str(&cd, home) == OK && buf_lit(&cd, "/other/shale a.shale") == OK);
    EXPECT_EQ(gate_run(&a, true, bash_call(&a, buf_view(&cd)), false, S(""), beside), 2);
    EXPECT(dup2(saved, 2) == 2);
    close(saved);
    close(null);
    const char *made_items[] = {"/link/shale", "/other/shale", "/bin/shale", "/link", "/other", "/bin", ""};
    TestGateCStrs made = {made_items, sizeof made_items / sizeof *made_items};
    for (size_t i = 0; i < made.len; i++) (void)remove(under(&a, home, at(made, i))); // ignored: a test's clean-up
    arena_deinit(&a);
}

TEST(an_escape_is_one_append_to_homes_wants_tsv_its_folders_made) {
    bool refused = false;
    Arena a;
    arena_init(&a, TEST_GATE_LIMIT, &refused);
    Str home = fresh_home(&a);
    Str want = S("{\"session_id\":\"s7\",\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"# want: sed-in-place: one edit\\nsed -i s/a/b/ x\"}}");
    EXPECT_EQ(gate_run_here(&a, true, want, true, home), 0);
    EXPECT_EQ(gate_run_here(&a, true, S("{\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"shale a.shale\"}}"), true, home), 0);
    Str log = read_under(&a, home, "/.local/state/shale/wants.tsv");
    size_t newlines = 0;
    for (size_t i = 0; i < log.len; i++) newlines += str_byte(log, i) == '\n';
    EXPECT_EQ(newlines, 1);
    EXPECT(gate_ends_with(log, S("Z\ts7\twant\tsed-in-place\tone edit\t# want: sed-in-place: one edit\\nsed -i s/a/b/ x\n")));
    // The date: YYYY-MM-DDTHH:MM:SSZ, then the tab.
    EXPECT(log.len > 21 && str_byte(log, 4) == '-' && str_byte(log, 10) == 'T' && str_byte(log, 19) == 'Z' && str_byte(log, 20) == '\t');
    // A second escape appends; the first line stays.
    EXPECT_EQ(gate_run_here(&a, true, S("{\"tool_name\":\"Monitor\",\"tool_input\":{\"command\":\"# out-of-scope: q: r\\nx\"}}"), true, home), 0);
    Str two = read_under(&a, home, "/.local/state/shale/wants.tsv");
    EXPECT(str_starts_with(two, log));
    EXPECT(gate_ends_with(two, S("\t\tout-of-scope\tq\tr\t# out-of-scope: q: r\\nx\n")));
    remove_under(&a, home);
    arena_deinit(&a);
}

// `shale --secrets` is for a person at a terminal: the gate refuses it in
// every form a launch takes, so an agent's shell cannot start it.
TEST(shale_secrets_is_refused_in_every_form_of_a_launch) {
    refuses("shale --secrets", "`--secrets` is not an option the gate passes; ");
    refuses("cd /home/x && shale --secrets", "`--secrets` is not an option the gate passes; ");
    refuses("/home/x/tools/shale/bin/shale --secrets", "`--secrets` is not an option the gate passes; ");
    refuses("shale --verbose --secrets", "`--secrets` is not an option the gate passes; ");
    refuses("shale --secrets <<'SHALE'\n2\nkeys\nSHALE", "`--secrets` is not an option the gate passes; ");
    refuses("shale --secrets", BOTH);
}

TEST_MAIN(
    TEST_CASE(the_launch_every_form_the_design_names),
    TEST_CASE(what_it_refuses_naming_both_forms),
    TEST_CASE(shale_help_which_every_refusal_points_to_passes),
    TEST_CASE(a_heredocs_failure_modes),
    TEST_CASE(the_escape_a_want_or_an_out_of_scope_its_tag_and_text_read),
    TEST_CASE(a_log_line_is_one_line_tabs_and_newlines_shown_the_commands_first_160_bytes),
    TEST_CASE(the_hooks_input_the_command_of_bash_and_monitor_any_other_tool_passed_input_it_cannot_read_refused),
    TEST_CASE(each_refusal_is_its_whole_line_word_for_word),
    TEST_CASE(the_limits_64_words_4096_lines_and_a_refusal_past_1024_bytes),
    TEST_CASE(a_log_line_past_its_room_is_cut_at_4095_bytes_mid_escape),
    TEST_CASE(the_hooks_json_is_read_as_std_json_reads_it),
    TEST_CASE(a_nesting_a_megabyte_deep_is_read_without_recursion),
    TEST_CASE(the_decision_each_way_the_gate_answers),
    TEST_CASE(an_escape_is_one_append_to_homes_wants_tsv_its_folders_made),
    TEST_CASE(shale_secrets_is_refused_in_every_form_of_a_launch),
    TEST_CASE(a_launch_by_path_is_the_shale_beside_the_gate)
)
