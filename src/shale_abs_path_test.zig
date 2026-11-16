//! Tests the stop for an absolute path on Linux: it names the deepest of the
//! root and the mounts whose folder holds the path, and the path to write
//! there; a path no folder holds, one in a `secrets` mount's folder and one
//! that is no script's path get the plain stop, which names no mount.

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");

fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{host_linux.linux_prelude};
}

/// The run of `src` stops with exactly `want`, as an argument's stop.
fn expectSays(t: *T, src: []const u8, want: []const u8) !void {
    var r = t.run(src, .atomic);
    defer r.deinit();
    const s = r.stop orelse {
        std.debug.print("{s}: no stop\n", .{src});
        return error.TestUnexpectedResult;
    };
    errdefer std.debug.print("{s}: {s}: {s}\n", .{ src, @tagName(s.class), s.message });
    try testing.expectEqual(.argument, s.class);
    try testing.expectEqualStrings(want, s.message);
}

const plain = "is absolute: paths are relative to the data root, or a mount's, `name:path` (`shale --help` lists this box's mounts)";

fn mountsOf(t: *T, text: []const u8) !void {
    const a = t.arena.allocator();
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.mem.replaceOwned(u8, a, text, "R", t.root), "/home/x", &why);
}

test "an absolute path's stop: the deepest mount or the root that holds it, and how to write it there" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(t.threaded.io(), "scr/deep");
    try t.tmp.dir.createDirPath(t.threaded.io(), "scr/tasks");
    const r = t.root;
    // Each order of the nested mounts, so neither the first nor the last to
    // match wins by its place in the file.
    for ([_][]const u8{
        "side = R-side\nouter = R/scr\ndeep = R/scr/deep\n",
        "deep = R/scr/deep\nouter = R/scr\nside = R-side\n",
    }) |mounts| {
        try mountsOf(&t, mounts);
        for ([_]struct { []const u8, []const u8 }{
            // A mount outside the root, below its folder and the folder itself.
            .{ "read(\"R-side/notes/a.txt\")", "`read`: \"R-side/notes/a.txt\" is absolute: it's under the mount `side:`, so write \"side:notes/a.txt\"" },
            .{ "exists(\"R-side\")", "`exists`: \"R-side\" is absolute: it's under the mount `side:`, so write \"side:.\"" },
            // Nested mounts: the deeper one where it holds the path.
            .{ "read(\"R/scr/tasks/x.output\")", "`read`: \"R/scr/tasks/x.output\" is absolute: it's under the mount `outer:`, so write \"outer:tasks/x.output\"" },
            .{ "read(\"R/scr/deep/x.txt\")", "`read`: \"R/scr/deep/x.txt\" is absolute: it's under the mount `deep:`, so write \"deep:x.txt\"" },
            .{ "stat(\"R/scr/deep/\")", "`stat`: \"R/scr/deep/\" is absolute: it's under the mount `deep:`, so write \"deep:.\"" },
            // The root, below it and itself, where no mount inside it holds the path.
            .{ "read(\"R/src/a.zig\")", "`read`: \"R/src/a.zig\" is absolute: it's under the data root, so write \"src/a.zig\"" },
            .{ "ls(\"R\")", "`ls`: \"R\" is absolute: it's under the data root, so write \".\"" },
            // A folder that only begins with a mount's folder's name is not in it.
            .{ "read(\"R/scrx/a.txt\")", "`read`: \"R/scrx/a.txt\" is absolute: it's under the data root, so write \"scrx/a.txt\"" },
            .{ "read(\"R-sidex/a.txt\")", "`read`: \"R-sidex/a.txt\" " ++ plain },
            // Held by nothing.
            .{ "read(\"/etc/passwd\")", "`read`: \"/etc/passwd\" " ++ plain },
            // No script's path: written there it would only stop again.
            .{ "read(\"R/scr/../x\")", "`read`: \"R/scr/../x\" " ++ plain },
        }) |c| try expectSays(&t, try std.mem.replaceOwned(u8, a, c[0], "R", r), try std.mem.replaceOwned(u8, a, c[1], "R", r));
    }
    // A change and a program's folder take the same stop, and nothing is written.
    try expectSays(&t, try std.mem.replaceOwned(u8, a, "replace(\"R/scr/tasks/new.txt\", \"x\")", "R", r), try std.mem.replaceOwned(u8, a, "`replace`: \"R/scr/tasks/new.txt\" is absolute: it's under the mount `outer:`, so write \"outer:tasks/new.txt\"", "R", r));
    try testing.expectError(error.FileNotFound, t.file("scr/tasks/new.txt"));
    try t.expectStop(try std.mem.replaceOwned(u8, a, "run({\"shale-fixture\", \"pwd\"}, {timeout_ms = 10000, tail = 1, cwd = \"R/scr/deep\"})", "R", r), .atomic, .argument, "is absolute: it's under the mount `deep:`, so write \"deep:.\"");
}

test "an absolute path's stop: a path in a secrets mount's folder names neither the mount nor its folder, even inside a mount that may be named" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(t.threaded.io(), "scr/keys");
    try t.put("scr/keys/tok", "s3cret\n");
    try mountsOf(&t, "outer = R/scr\nvault = R/scr/keys secrets\n");
    for ([_][]const u8{ "read(\"R/scr/keys/tok\")", "exists(\"R/scr/keys\")" }) |src_r| {
        const src = try std.mem.replaceOwned(u8, a, src_r, "R", t.root);
        var res = t.run(src, .atomic);
        defer res.deinit();
        const s = res.stop orelse return error.TestUnexpectedResult;
        errdefer std.debug.print("{s}: {s}\n", .{ src, s.message });
        try testing.expect(std.mem.endsWith(u8, s.message, plain));
        try testing.expect(std.mem.indexOf(u8, s.message, "vault") == null);
        try testing.expect(std.mem.indexOf(u8, s.message, "outer") == null);
        try testing.expect(std.mem.indexOf(u8, s.message, "so write") == null);
    }
    // Beside the secrets folder, the mount above it is named as ever.
    try expectSays(&t, try std.mem.replaceOwned(u8, a, "read(\"R/scr/notes.txt\")", "R", t.root), try std.mem.replaceOwned(u8, a, "`read`: \"R/scr/notes.txt\" is absolute: it's under the mount `outer:`, so write \"outer:notes.txt\"", "R", t.root));
}
