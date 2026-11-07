//! Tests `shale_tokens`: a Shale file's tokens by the language's own lexer.

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");

fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{host_linux.linux_prelude};
}

/// A script printing each token of `k.shale`, by `tag`, `line` and `text`.
const rows = "for k in shale_tokens(\"k.shale\") do print(k.tag, k.line, k.text) end";

test "shale_tokens: a file's tokens in order, `{tag, text, line, start, stop}`, comments among them; a keyword inside a name is a name; `read(p, start, stop - start)` is the token" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("k.shale", "-- a\nlocal end_col = 12 + 'x'\n");
    try t.expectOutput(rows, "comment\t1\t-- a\nkw_local\t2\tlocal\nname\t2\tend_col\nassign\t2\t=\nint\t2\t12\nplus\t2\t+\nstring\t2\t'x'\n");
    try t.expectOutput("for k in shale_tokens(\"k.shale\") do print(k.start, k.stop, read(\"k.shale\", k.start, k.stop - k.start) == k.text) end", "0\t4\ttrue\n5\t10\ttrue\n11\t18\ttrue\n19\t20\ttrue\n21\t23\ttrue\n24\t25\ttrue\n26\t29\ttrue\n");
}

test "shale_tokens: `text` is the source's bytes: a string's quotes and escapes as written, a long string's opener and closer, and the newline after its opener" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("k.shale", "\"a\\n\" [=[\nb]=]");
    try t.expectOutput("for k in shale_tokens(\"k.shale\") do print(#k.text, k.stop - k.start) end", "5\t5\n8\t8\n");
    try t.expectOutput("print(shale_tokens(\"k.shale\")[1].text == \"\\\"a\\\\n\\\"\", shale_tokens(\"k.shale\")[2].text == \"[=[\\nb]=]\")", "true\ttrue\n");
}

test "shale_tokens: a comment's and a string's words are no tokens, a block comment and a long string span their lines, `--[=x` is a line comment, and a line comment leaves its newline and keeps a CR" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("k.shale", "--[=[ end\nif ]=] x --[=x then\ny = \"-- no\" -- z\r\nw = [==[ ]=] -- ]==] v");
    try t.expectOutput(rows, "comment\t1\t--[=[ end\nif ]=]\nname\t2\tx\ncomment\t2\t--[=x then\nname\t3\ty\nassign\t3\t=\nstring\t3\t\"-- no\"\ncomment\t3\t-- z\r\nname\t4\tw\nassign\t4\t=\nstring\t4\t[==[ ]=] -- ]==]\nname\t4\tv\n");
}

test "shale_tokens: a file of no tokens, an empty one or one of space, lists none; a file of one comment lists it, with no newline after it" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("empty.shale", "");
    try t.put("space.shale", " \n\t\r\n");
    try t.put("k.shale", "--");
    try t.expectOutput("print(#shale_tokens(\"empty.shale\"), #shale_tokens(\"space.shale\"))", "0\t0\n");
    try t.expectOutput(rows, "comment\t1\t--\n");
}

test "shale_tokens: `{tag = ...}` keeps one kind, a keyword or a comment, and none when the file holds none; a tag that is no token's stops, as does the end's, a bad key, and a tag or options of the wrong type" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("k.shale", "-- a\nif b then c() end -- d\n");
    try t.expectOutput("for k in shale_tokens(\"k.shale\", {tag = \"comment\"}) do print(k.text, k.line) end", "-- a\t1\n-- d\t2\n");
    try t.expectOutput("for k in shale_tokens(\"k.shale\", {tag = \"kw_end\"}) do print(k.text, k.start) end", "end\t19\n");
    try t.expectOutput("print(#shale_tokens(\"k.shale\", {tag = \"string\"}))", "0\n");
    try t.expectOutput("print(#shale_tokens(\"k.shale\"))", "9\n");
    try t.expectStop("shale_tokens(\"k.shale\", {tag = \"nope\"})", .atomic, .argument, "no token tag `nope`");
    try t.expectStop("shale_tokens(\"k.shale\", {tag = \"eof\"})", .atomic, .argument, "no token tag `eof`");
    try t.expectStop("shale_tokens(\"k.shale\", {kind = \"name\"})", .atomic, .argument, "takes `tag`, not `kind`");
    try t.expectStop("shale_tokens(\"k.shale\", {tag = 1})", .atomic, .type, "`tag` is a tag's name");
    try t.expectStop("shale_tokens(\"k.shale\", {\"name\"})", .atomic, .type, "options are a map, not a list");
    try t.expectStop("shale_tokens(\"k.shale\", 1)", .atomic, .type, "options are a map");
}

test "shale_tokens: a file that doesn't lex stops with the lexer's line and message, an unclosed comment as much as a refused construct; a missing file stops too, and a file that lexes but doesn't parse is listed" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("float.shale", "a\nb = 1.5\n");
    try t.put("comment.shale", "a\n\n--[=[ b");
    try t.put("nolevel.shale", "--[[ a ]]");
    try t.put("parses_not.shale", "end end (");
    try t.expectStop("shale_tokens(\"float.shale\")", .atomic, .argument, "float.shale does not lex as Shale at line 2: float literals");
    try t.expectStop("shale_tokens(\"comment.shale\")", .atomic, .argument, "at line 3: a long comment with no `]=]`");
    try t.expectStop("shale_tokens(\"nolevel.shale\")", .atomic, .argument, "a long comment needs a level");
    try t.expectOutput("print(#shale_tokens(\"parses_not.shale\"))", "3\n");
    var r = t.run("shale_tokens(\"absent.shale\")", .atomic);
    defer r.deinit();
    try testing.expect(r.stop != null);
}

test "shale_tokens: a step a token, kept or not, more for a file four times the size; `host` takes a path and options, no more or fewer" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    var steps: [2]u64 = undefined;
    for ([_]usize{ 25, 100 }, 0..) |n, k| {
        var src: std.ArrayList(u8) = .empty;
        for (0..n) |_| try src.appendSlice(a, "x = 1\n");
        try t.put("n.shale", src.items);
        var r = t.run("local d = shale_tokens(\"n.shale\", {tag = \"string\"})", .atomic);
        defer r.deinit();
        try testing.expect(r.stop == null);
        steps[k] = r.steps;
    }
    // Three tokens a line: 75 lines more are 225 more.
    try testing.expect(steps[1] >= steps[0] + 225);
    try t.expectStop("host(\"shale_tokens\")", .atomic, .argument, "takes a path and options, given 0");
    try t.expectStop("host(\"shale_tokens\", \"k\", {}, 1)", .atomic, .argument, "takes a path and options, given 3");
}
