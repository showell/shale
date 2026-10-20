//! Parses each `.shale` recipe under cookbook/ as the CLI reads a script, with
//! the Linux and git preludes, so a recipe that calls a builtin or prelude
//! function that is gone, or calls it wrongly, fails here and not in the hands
//! of whoever copies it next.

const std = @import("std");
const testing = std.testing;
const script = @import("shale").script;
const host_linux = @import("shale_host_linux.zig");

test "cookbook: every recipe parses, with the Linux and git preludes" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var dir = try std.Io.Dir.cwd().openDir(io, "cookbook", .{ .iterate = true });
    defer dir.close(io);
    var walker = try dir.walk(a);
    var recipes: usize = 0;
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".shale")) continue;
        const source = try dir.readFileAlloc(io, e.path, a, .limited(1 << 20));
        var stop: ?script.diag.Stop = null;
        _ = script.parse.parseMeasured(a, a, source, 64, &stop, null, &.{ host_linux.linux_prelude, host_linux.git_prelude }) catch {
            if (stop) |s| std.debug.print("cookbook/{s}:{d}:{d}: {s}\n", .{ e.path, s.line, s.column, s.message });
            return error.TestUnexpectedResult;
        };
        recipes += 1;
    }
    // A walk that found nothing would pass, so require at least the zig recipes.
    try testing.expect(recipes >= 9);
}
