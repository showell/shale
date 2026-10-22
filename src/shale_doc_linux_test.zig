//! Runs SHALE-AGENTS.md's `shale-linux` blocks: those that need the Linux
//! prelude (`zig_rename`, `run`, git), which the core's doc test
//! (script/agent_test.zig) skips. Each runs on a fresh small checkout with the
//! CLI's summary diffs, and must print what the ```output block after it
//! holds, exactly, or stop as its last line says (`-- stops: class`).

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");

const Block = struct { line: usize, source: []const u8, prints: ?[]const u8, stops: ?[]const u8 };

/// Each ```shale-linux block, and the ```output block after it (blank lines
/// between) or its last line `-- stops: <class>`. An ```output block is the
/// next block's only when that block is a shale-linux one: the core's doc test
/// reads the ```shale blocks and theirs.
fn blocks(a: std.mem.Allocator, doc: []const u8) ![]Block {
    var out: std.ArrayList(Block) = .empty;
    var lines = std.mem.splitScalar(u8, doc, '\n');
    var n: usize = 0;
    var start: ?usize = null;
    var reading_output = false;
    var body: std.ArrayList(u8) = .empty;
    var awaiting: ?Block = null;
    while (lines.next()) |line| {
        n += 1;
        if (start == null and !reading_output) {
            if (awaiting) |b| {
                if (std.mem.eql(u8, line, "```output")) {
                    reading_output = true;
                } else if (std.mem.trim(u8, line, " ").len != 0) {
                    std.debug.print("docs/SHALE-AGENTS.md:{d}: a shale-linux block with no output block after it and no `-- stops:`\n", .{b.line});
                    return error.TestUnexpectedResult;
                }
                continue;
            }
            if (std.mem.eql(u8, line, "```shale-linux")) start = n;
            continue;
        }
        if (!std.mem.eql(u8, line, "```")) {
            try body.appendSlice(a, line);
            try body.append(a, '\n');
            continue;
        }
        const text = body.items;
        body = .empty;
        if (reading_output) {
            var b = awaiting.?;
            b.prints = text;
            try out.append(a, b);
            awaiting = null;
            reading_output = false;
            continue;
        }
        defer start = null;
        if (std.mem.indexOf(u8, text, "\n-- stops: ")) |at| {
            try out.append(a, .{ .line = start.?, .source = text[0 .. at + 1], .prints = null, .stops = std.mem.trim(u8, text[at + "\n-- stops: ".len ..], " \n") });
            continue;
        }
        if (std.mem.indexOf(u8, text, "-- prints:") != null) {
            std.debug.print("docs/SHALE-AGENTS.md:{d}: `-- prints:` is the old form: the output goes in an ```output block after the block\n", .{start.?});
            return error.TestUnexpectedResult;
        }
        awaiting = .{ .line = start.?, .source = text, .prints = null, .stops = null };
    }
    if (awaiting) |b| {
        std.debug.print("docs/SHALE-AGENTS.md:{d}: a shale-linux block with no output block after it and no `-- stops:`\n", .{b.line});
        return error.TestUnexpectedResult;
    }
    return out.items;
}

test "SHALE-AGENTS.md's shale-linux blocks: a block and its output block are read; no output block, or the old form, is refused" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const got = try blocks(a, "```shale-linux\nprint(1)\n```\n\n```output\n1\n```\n```shale-linux\nfail(\"x\")\n-- stops: fail\n```\n");
    try testing.expectEqual(@as(usize, 2), got.len);
    try testing.expectEqualStrings("1\n", got[0].prints.?);
    try testing.expectEqualStrings("fail", got[1].stops.?);
    try testing.expectError(error.TestUnexpectedResult, blocks(a, "```shale-linux\nprint(1)\n```\n\ntext\n"));
    try testing.expectError(error.TestUnexpectedResult, blocks(a, "```shale-linux\nprint(1)\n-- prints:\n-- 1\n```\n"));
}

/// The checkout: agent_test's two tcp files, and a host that still
/// declares what the move recipe would qualify.
const files = [_][2][]const u8{
    .{ "src/tcp.zig", "//! TCP. The deadlines are in `tcp_timeouts.zig:2`.\nconst timeouts = @import(\"tcp_timeouts.zig\");\npub const pastDeadline = timeouts.pastDeadline;\n" },
    .{ "src/tcp_timeouts.zig", "pub fn pastDeadline(now: u64) bool {\n    return now > 100;\n}\n" },
    .{ "src/linux_host.zig", "pub const kept_write_deadline_ms = 5000;\nfn late(ms: u64) bool {\n    return ms > kept_write_deadline_ms;\n}\n" },
};

test "docs/SHALE-AGENTS.md: every shale-linux block runs with the Linux prelude on a checkout, as the doc says" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const doc = try std.Io.Dir.cwd().readFileAlloc(threaded.io(), "docs/SHALE-AGENTS.md", a, .limited(1 << 20));
    const all = try blocks(a, doc);
    var wrong: usize = 0;
    for (all) |b| {
        var t: T = undefined;
        try t.init();
        defer t.deinit();
        t.preludes = &.{host_linux.linux_prelude};
        t.diffs = .summary;
        try t.tmp.dir.createDirPath(t.threaded.io(), "src");
        for (files) |f| try t.put(f[0], f[1]);
        var r = t.run(b.source, .atomic);
        defer r.deinit();
        if (b.stops) |class| {
            if (r.stop == null or !std.mem.eql(u8, @tagName(r.stop.?.class), class)) {
                std.debug.print("docs/SHALE-AGENTS.md:{d}: does not stop as `{s}`\n", .{ b.line, class });
                wrong += 1;
            }
        } else if (r.stop) |s| {
            std.debug.print("docs/SHALE-AGENTS.md:{d}: stopped {d}:{d} {s}: {s}\n", .{ b.line, s.line, s.column, @tagName(s.class), s.message });
            wrong += 1;
        } else if (!std.mem.eql(u8, r.output, b.prints.?)) {
            std.debug.print("docs/SHALE-AGENTS.md:{d}: prints\n{s}-- not --\n{s}", .{ b.line, r.output, b.prints.? });
            wrong += 1;
        }
    }
    try testing.expectEqual(@as(usize, 0), wrong);
    try testing.expect(all.len >= 2);
}
