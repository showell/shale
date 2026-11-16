//! Tests `--undo-on-stop`'s journal in process: the host's `undo` set, so
//! each change a run makes journals its path first (`noteBefore` in
//! shale_host_linux.zig), and `undoChanges` puts back what was journaled.
//! The CLI's own test (shale_cli_test.zig) runs the whole flag through
//! `shale`; these reach each of the journal's refusals and its memory
//! failures, which a child process's run cannot be measured for. Also a
//! run whose root is relative, taken against the working folder.

const std = @import("std");
const testing = std.testing;
const shale = @import("shale");
const script = shale.script;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");

fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{host_linux.linux_prelude};
    t.host.undo = true;
}

fn onceStop(t: *T, src: []const u8, class: script.diag.Class, says: []const u8) !void {
    var r = t.runOnce(src, .atomic);
    defer r.deinit();
    const s = r.stop orelse return error.TestUnexpectedResult;
    errdefer std.debug.print("{s}: {s}: {s}\n", .{ src, @tagName(s.class), s.message });
    try testing.expectEqual(class, s.class);
    try testing.expect(std.mem.indexOf(u8, s.message, says) != null);
}

test "undo: each path is journaled once, before its first change, as a file's bytes, a folder or nothing there; undoChanges puts them back" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const io = t.threaded.io();
    try t.put("a.txt", "one\n");
    try t.tmp.dir.createDirPath(io, "d");
    try onceStop(&t,
        \\edit("a.txt", "one", "two")
        \\edit("a.txt", "two", "three")
        \\create("new.txt", "made\n")
        \\mkdir("d/inner")
        \\remove_tree("d")
        \\fail("stopped on purpose")
    , .fail, "stopped on purpose");
    // a.txt once though changed twice; the rest each once.
    const j = t.host.journal.items;
    try testing.expect(j.len >= 4);
    var file: usize = 0;
    var absent: usize = 0;
    var dir: usize = 0;
    for (j, 0..) |p, k| {
        for (j[k + 1 ..]) |q| try testing.expect(!std.mem.eql(u8, p.abs, q.abs));
        switch (p.was) {
            .file => |f| {
                try testing.expect(std.mem.endsWith(u8, p.abs, "/a.txt"));
                try testing.expectEqualStrings("one\n", f.bytes);
                file += 1;
            },
            .absent => absent += 1,
            .dir => dir += 1,
        }
    }
    try testing.expectEqual(@as(usize, 1), file);
    try testing.expect(absent >= 2);
    try testing.expect(dir >= 1);
    try testing.expectEqual(@as(usize, 4), t.host.journal_bytes);
    var w: std.Io.Writer.Allocating = .init(testing.allocator);
    defer w.deinit();
    // The folder was removed whole, which undo cannot put back: said, false.
    try testing.expect(!try host_linux.undoChanges(&t.host, io, &w.writer));
    try testing.expect(std.mem.indexOf(u8, w.written(), ": a folder removed whole, and its files with it\n") != null);
    try testing.expectEqualStrings("one\n", try t.file("a.txt"));
    try testing.expectError(error.FileNotFound, t.tmp.dir.statFile(io, "new.txt", .{}));
}

test "undo: a change past the journal's room, a file it cannot read, a path neither file nor folder, and one it cannot look at are refused before anything changes" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const io = t.threaded.io();
    const a = t.arena.allocator();
    try t.put("a.txt", "one\n");
    // The room left is less than a.txt.
    t.host.journal_bytes = host_linux.journal_most - 2;
    try onceStop(&t, "replace(\"a.txt\", \"x\")", .refused, "--undo-on-stop keeps at most 67108864 bytes of what a run changes, and ");
    t.host.journal_bytes = 0;
    try testing.expectEqualStrings("one\n", try t.file("a.txt"));

    // A file that may be stat'd but not read.
    try t.put("sealed.txt", "s\n");
    const sealed = try std.fmt.allocPrintSentinel(a, "{s}/sealed.txt", .{t.root}, 0);
    try testing.expectEqual(@as(usize, 0), std.os.linux.chmod(sealed, 0o000));
    try onceStop(&t, "replace(\"sealed.txt\", \"x\")", .store, "sealed.txt cannot be read (AccessDenied), so it could not be put back");
    try testing.expectEqual(@as(usize, 0), std.os.linux.chmod(sealed, 0o600));

    // A named pipe.
    const fifo = try std.fmt.allocPrintSentinel(a, "{s}/pipe", .{t.root}, 0);
    try testing.expectEqual(@as(usize, 0), std.os.linux.mknodat(std.os.linux.AT.FDCWD, fifo, std.os.linux.S.IFIFO | 0o600, 0));
    try onceStop(&t, "remove(\"pipe\")", .refused, "pipe is neither a file nor a folder, and could not be put back");

    // A path in a folder that cannot be searched.
    try t.tmp.dir.createDirPath(io, "locked");
    const locked = try std.fmt.allocPrintSentinel(a, "{s}/locked", .{t.root}, 0);
    try testing.expectEqual(@as(usize, 0), std.os.linux.chmod(locked, 0o000));
    defer _ = std.os.linux.chmod(locked, 0o700);
    try onceStop(&t, "append(\"locked/x.txt\", \"x\")", .store, "locked/x.txt cannot be looked at (AccessDenied), so it could not be put back");
    try testing.expectEqual(@as(usize, 0), t.host.journal.items.len);
}

test "undo: the journal's own allocations failing, at each, stop the run on memory, and nothing leaks" {
    var i: usize = 0;
    var failed: usize = 0;
    while (true) : (i += 1) {
        var t: T = undefined;
        try init(&t);
        defer t.deinit();
        try t.put("a.txt", "one\n");
        var failing: testing.FailingAllocator = .init(testing.allocator, .{ .fail_index = i });
        t.host.alloc = failing.allocator();
        var r = t.runOnce("replace(\"a.txt\", \"two\\n\") create(\"new.txt\", \"n\")", .atomic);
        defer r.deinit();
        const s = r.stop orelse break;
        errdefer std.debug.print("at {d}: {s}: {s}\n", .{ i, @tagName(s.class), s.message });
        // A failed read of a.txt's bytes is the store's, named; any other
        // failure is the arena's budget.
        if (s.class == .store) {
            try testing.expect(std.mem.indexOf(u8, s.message, "a.txt cannot be read (OutOfMemory)") != null);
        } else {
            try testing.expectEqual(.budget, s.class);
            try testing.expectEqual(.arena, s.budget.?);
        }
        failed += 1;
    }
    // a.txt's bytes, its path's copy and the journal's room; then new.txt's
    // copy and room.
    try testing.expect(failed >= 4);
}

test "host: a relative root is taken against the working folder, so `real` gives an absolute path" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const abs = t.root;
    // The same folder, spelled from the working folder.
    t.root = abs[t.host.cwd_abs.len + 1 ..];
    try testing.expect(!std.fs.path.isAbsolute(t.root));
    var r = t.runOnce("print(real(\"\"))", .atomic);
    defer r.deinit();
    if (r.stop) |s| {
        std.debug.print("stopped: {s}\n", .{s.message});
        return error.TestUnexpectedResult;
    }
    try testing.expectEqualStrings(abs, std.mem.trimEnd(u8, r.output, "\n"));
}
