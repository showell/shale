//! Tests regex.zig through its own API: what a pattern compiles to, and
//! what it refuses.

const std = @import("std");
const regex = @import("regex.zig");
const testing = std.testing;

/// Where `pattern` first matches in `s`, or null.
fn found(pattern: []const u8, s: []const u8) !?regex.Found {
    var refused: regex.Refusal = undefined;
    var p = try regex.compile(testing.allocator, pattern, .{}, &refused);
    defer p.deinit(testing.allocator);
    var sc = try regex.Scratch.init(testing.allocator, &p);
    defer sc.deinit(testing.allocator);
    return regex.search(&p, &sc, s, s.len + 1).found;
}

fn refusal(pattern: []const u8) !regex.Refusal {
    var refused: regex.Refusal = undefined;
    if (regex.compile(testing.allocator, pattern, .{}, &refused)) |p| {
        var program = p;
        program.deinit(testing.allocator);
        return error.TestUnexpectedResult;
    } else |e| try testing.expectEqual(error.Refused, e);
    return refused;
}

test "regex: `{n,m}` is refused where a part starts and after a repetition, at its brace" {
    for ([_]struct { []const u8, usize }{ .{ "{2}", 0 }, .{ "a*{2}", 2 }, .{ "a{2}", 1 } }) |c| {
        const r = try refusal(c[0]);
        try testing.expectEqual(c[1], r.at);
        try testing.expect(std.mem.startsWith(u8, r.message, "`{n,m}` is not in this subset"));
    }
}

test "regex: `\\n`, `\\t` and `\\r` are the control bytes, alone and in a class" {
    try testing.expectEqual(@as(usize, 1), (try found("\\n", "a\nb")).?.start);
    try testing.expectEqual(@as(usize, 1), (try found("\\t", "a\tb")).?.start);
    try testing.expectEqual(@as(usize, 1), (try found("\\r", "a\rb")).?.start);
    try testing.expectEqual(@as(usize, 2), (try found("[\\t\\r]", "ab\r")).?.start);
    try testing.expect(try found("\\n", "anb") == null);
}

test "regex: a class holds a shorthand, an escaped byte, and a range whose end is escaped" {
    try testing.expectEqual(@as(usize, 3), (try found("[\\d]", "abc7")).?.start);
    try testing.expect(try found("[\\d]", "abc") == null);
    try testing.expectEqual(@as(usize, 1), (try found("[\\.]", "x.")).?.start);
    try testing.expect(try found("[\\.]", "xy") == null);
    // `!` through `/`, the end written `\/`.
    try testing.expectEqual(@as(usize, 1), (try found("[!-\\/]", "a/")).?.start);
    try testing.expectEqual(@as(usize, 1), (try found("[!-\\/]", "a!")).?.start);
    try testing.expect(try found("[!-\\/]", "a0") == null);
}

test "regex: a pattern of more than 256 bytes is refused at its 257th byte, unread" {
    const long: [regex.pattern_most + 1]u8 = @splat('a');
    const r = try refusal(&long);
    try testing.expectEqual(@as(usize, regex.pattern_most), r.at);
    try testing.expectEqualStrings("a pattern of more than 256 bytes", r.message);
    _ = try found(long[0..regex.pattern_most], &long);
}

test "regex: compile, Scratch.init and Cache.put give back everything they took when any one allocation fails" {
    const pattern = "(a|b)[x-z\\d]+c?";
    // Twice: as an allocator gives, and with every resize refused, so each
    // list's shrink to its length is an allocation that can fail too.
    for ([_]usize{ std.math.maxInt(usize), 0 }) |resizes| try sweep(pattern, resizes);
}

fn sweep(pattern: []const u8, resizes: usize) !void {
    var i: usize = 0;
    while (true) : (i += 1) {
        var failing: testing.FailingAllocator = .init(testing.allocator, .{ .fail_index = i, .resize_fail_index = resizes });
        const a = failing.allocator();
        var cache: regex.Cache = .{};
        var refused: regex.Refusal = undefined;
        if (cache.put(a, pattern, .{}, &refused)) |got| {
            try testing.expect(regex.search(&got.program, &got.scratch, "zbx9c", 6).found != null);
            cache.deinit(a);
            try testing.expectEqual(failing.allocations, failing.deallocations);
            break;
        } else |e| {
            try testing.expectEqual(error.OutOfMemory, e);
            try testing.expectEqual(failing.allocations, failing.deallocations);
        }
    }
    // The sweep reached the last of them: a program, its sets, the scratch's
    // five arrays and the pattern's copy.
    try testing.expect(i >= 8);
}

/// The match of `p` in `s` from `from`, through `sc`: start, end and groups,
/// and what the search skipped.
fn searched(p: *const regex.Program, sc: *regex.Scratch, s: []const u8, from: usize) regex.Searched {
    const r = regex.searchFrom(p, sc, s, from, std.math.maxInt(u64));
    std.debug.assert(r.finished);
    return r;
}

test "regex: a search that skips the bytes no match can begin with finds what one examining every position finds, groups and all, from every start" {
    const patterns = [_][]const u8{
        "a",      "ab|cd",         "\\[([^\\]]*)\\]\\(([^)]*)\\)", "`([^`\n]+)`", "\\bstep(s)?\\b",
        "x+y",    "(a)(b)?(c)",    "[0-9]+",                       "\\Bb",        "(?:ab)+c",
        "a$",     "\\bcat\\b|dog", "(x|y)z*",                      ".b",          "[^a]c",
        "(a*)*b", "\\n",           "(a|ab)(c|bcd)",                "\\$x",        "b\\b",
    };
    const strings = [_][]const u8{
        "",                                "a",                    "xyz",
        "see [the docs](a.md) and [x](y)", "`code` and `more` ``", "Steps: one step, two steps; misstep",
        "aab abc cd ab",                   "xxxy xy y x",          "123 abc 45",
        "abb bab b",                       "ababab ababc abc",     "a cat, a dog, concat",
        "a\nb\na",                         "[unclosed (paren",     "abcbcd abcd",
        "$x $$x x",                        "ab.b cb\nb",           "aaaaaaaaab aaa",
    };
    var skipped_somewhere = false;
    for (patterns) |pattern| for ([_]bool{ false, true }) |nocase| {
        var refused: regex.Refusal = undefined;
        var p = try regex.compile(testing.allocator, pattern, .{ .nocase = nocase }, &refused);
        defer p.deinit(testing.allocator);
        // The same program, with nothing to skip by.
        var every = p;
        every.first = null;
        var sc = try regex.Scratch.init(testing.allocator, &p);
        defer sc.deinit(testing.allocator);
        for (strings) |s| for (0..s.len + 1) |from| {
            const got = searched(&p, &sc, s, from);
            const want = searched(&every, &sc, s, from);
            errdefer std.debug.print("/{s}/{s} on \"{f}\" from {d}\n", .{ pattern, if (nocase) "i" else "", std.zig.fmtString(s), from });
            try testing.expectEqual(want.skipped, 0);
            try testing.expectEqual(want.found == null, got.found == null);
            if (got.found) |g| {
                const w = want.found.?;
                try testing.expectEqual(w.start, g.start);
                try testing.expectEqual(w.end, g.end);
                for (w.groups, g.groups) |wg, gg| try testing.expectEqual(wg, gg);
            }
            // A skipped byte is a position not examined.
            try testing.expect(got.positions + got.skipped <= want.positions + 1);
            if (got.skipped > 0) skipped_somewhere = true;
        };
    };
    try testing.expect(skipped_somewhere);
}

test "regex: a pattern that can match nothing, or begin with any byte, skips nothing; one that cannot names the bytes it can begin with" {
    for ([_][]const u8{ "", "a*", "^", "$", "\\b", "(a|)", "x?", "(a*)*", "a|b*", "[\\s\\S]" }) |pattern| {
        var refused: regex.Refusal = undefined;
        var p = try regex.compile(testing.allocator, pattern, .{}, &refused);
        defer p.deinit(testing.allocator);
        errdefer std.debug.print("/{s}/\n", .{pattern});
        try testing.expect(p.first == null);
    }
    for ([_]struct { []const u8, []const u8, bool }{
        .{ "ab|cd", "ac", false },     .{ "\\bstep", "s", false }, .{ "step", "sS", true },
        .{ "(?:x|y)+z", "xy", false }, .{ "a?b", "ab", false },    .{ "[0-2]x", "012", false },
    }) |c| {
        var refused: regex.Refusal = undefined;
        var p = try regex.compile(testing.allocator, c[0], .{ .nocase = c[2] }, &refused);
        defer p.deinit(testing.allocator);
        errdefer std.debug.print("/{s}/\n", .{c[0]});
        try testing.expectEqual(c[1].len, p.first.?.count());
        for (c[1]) |b| try testing.expect(p.first.?.isSet(b));
    }
}

test "regex: a search stops unfinished past the bytes it may skip, as past the positions it may examine" {
    var refused: regex.Refusal = undefined;
    var p = try regex.compile(testing.allocator, "z", .{}, &refused);
    defer p.deinit(testing.allocator);
    var sc = try regex.Scratch.init(testing.allocator, &p);
    defer sc.deinit(testing.allocator);
    const s = "aaaaaaaaaaz";
    const short = regex.searchWithin(&p, &sc, s, 0, .{ .positions = 100, .skipped = 9 });
    try testing.expect(!short.finished and short.found == null and short.skipped == 10);
    const enough = regex.searchWithin(&p, &sc, s, 0, .{ .positions = 100, .skipped = 10 });
    try testing.expect(enough.finished and enough.found.?.start == 10 and enough.positions == 2);
}
