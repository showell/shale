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
