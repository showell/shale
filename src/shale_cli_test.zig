//! Tests the build's own `shale` binary as a shell sees it.

const std = @import("std");
const testing = std.testing;
const shale_exe = @import("shale_test_options").shale;

test "cli: stdout and stderr into one file (`> f 2>&1`) keep both, in order" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var tmp = testing.tmpDir(.{});
    defer tmp.cleanup();
    const a = testing.allocator;
    const out = try std.fmt.allocPrint(a, ".zig-cache/tmp/{s}/both.txt", .{&tmp.sub_path});
    defer a.free(out);
    const script = try std.fmt.allocPrint(a, "'{s}' -e 'print(\"one line of output\") fail(\"x\")' > '{s}' 2>&1", .{ shale_exe, out });
    defer a.free(script);
    const r = try std.process.run(a, io, .{ .argv = &.{ "sh", "-c", script } });
    a.free(r.stdout);
    a.free(r.stderr);
    const both = try tmp.dir.readFileAlloc(io, "both.txt", a, .limited(1 << 16));
    defer a.free(both);
    try testing.expect(std.mem.startsWith(u8, both, "one line of output\n── stopped at -e:1:"));
}

test "cli: --undo-on-stop puts back what a stopped run changed: an edit, a file made, a folder made, a file removed, an executable's bytes and mode" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var tmp = testing.tmpDir(.{});
    defer tmp.cleanup();
    const a = testing.allocator;
    try tmp.dir.writeFile(io, .{ .sub_path = "a.txt", .data = "one\n" });
    try tmp.dir.writeFile(io, .{ .sub_path = "gone.txt", .data = "kept\n" });
    try tmp.dir.writeFile(io, .{ .sub_path = "x.sh", .data = "#!/bin/sh\necho old\n", .flags = .{ .permissions = @fromBackingInt(@intCast(0o755)) } });
    try tmp.dir.createDirPath(io, "there");
    try tmp.dir.writeFile(io, .{ .sub_path = "u.shale", .data =
        \\edit("a.txt", "one", "two")
        \\create("new.txt", "made\n")
        \\mkdir("made")
        \\create("made/inner.txt", "in\n")
        \\replace("x.sh", "#!/bin/sh\necho new\n")
        \\remove("gone.txt")
        \\create("there/n.txt", "n\n")
        \\fail("stopped on purpose")
        \\
    });
    const root = try std.fmt.allocPrint(a, ".zig-cache/tmp/{s}", .{&tmp.sub_path});
    defer a.free(root);
    const file = try std.fmt.allocPrint(a, "{s}/u.shale", .{root});
    defer a.free(file);
    const r = try std.process.run(a, io, .{ .argv = &.{ shale_exe, "--root", root, "--undo-on-stop", file } });
    defer a.free(r.stdout);
    defer a.free(r.stderr);
    errdefer std.debug.print("{s}\n", .{r.stderr});
    try testing.expectEqual(@as(u8, 1), r.term.exited);
    try testing.expect(std.mem.indexOf(u8, r.stderr, "── undone: 7 path(s) the run changed put back\n") != null);
    const back = struct {
        fn of(d: std.Io.Dir, i: std.Io, name: []const u8) ![]u8 {
            return d.readFileAlloc(i, name, testing.allocator, .limited(1 << 16));
        }
    };
    for ([_][2][]const u8{ .{ "a.txt", "one\n" }, .{ "gone.txt", "kept\n" }, .{ "x.sh", "#!/bin/sh\necho old\n" } }) |f| {
        const got = try back.of(tmp.dir, io, f[0]);
        defer a.free(got);
        try testing.expectEqualStrings(f[1], got);
    }
    for ([_][]const u8{ "new.txt", "made", "there/n.txt" }) |gone| {
        try testing.expectError(error.FileNotFound, tmp.dir.statFile(io, gone, .{}));
    }
    // The folder that was there is there still, and the executable still one.
    _ = try tmp.dir.statFile(io, "there", .{});
    const st = try tmp.dir.statFile(io, "x.sh", .{});
    try testing.expectEqual(@as(u32, 0o755), @backingInt(st.permissions) & 0o777);
}

test "cli: --args-stdin gives a file's values from stdin, raw: quotes, `|`, `$`, a backslash" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var tmp = testing.tmpDir(.{});
    defer tmp.cleanup();
    const a = testing.allocator;
    try tmp.dir.writeFile(io, .{ .sub_path = "p.shale", .data = "print(arg(\"doc\", \"\"))\nprint(arg(\"n\", \"\"))\n" });
    try tmp.dir.writeFile(io, .{ .sub_path = "args", .data = "doc=It's \"quoted\" | $HOME \\ as is\n\n" });
    const dir = try std.fmt.allocPrint(a, ".zig-cache/tmp/{s}", .{&tmp.sub_path});
    defer a.free(dir);
    const cmd = try std.fmt.allocPrint(a, "'{s}' --root '{s}' '{s}/p.shale' --args-stdin n=3 < '{s}/args'", .{ shale_exe, dir, dir, dir });
    defer a.free(cmd);
    const r = try std.process.run(a, io, .{ .argv = &.{ "sh", "-c", cmd } });
    defer a.free(r.stdout);
    defer a.free(r.stderr);
    errdefer std.debug.print("{s}\n", .{r.stderr});
    try testing.expectEqualStrings("It's \"quoted\" | $HOME \\ as is\n3\n", r.stdout);
}

test "cli: a script named through a mount (`lib:hi.shale`) is read from the mount's folder; a name no mount has says so" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var tmp = testing.tmpDir(.{});
    defer tmp.cleanup();
    const a = testing.allocator;
    try tmp.dir.createDirPath(io, "libdir");
    try tmp.dir.writeFile(io, .{ .sub_path = "libdir/hi.shale", .data = "print(\"from the mount\")\n" });
    const dir = try std.fmt.allocPrint(a, ".zig-cache/tmp/{s}", .{&tmp.sub_path});
    defer a.free(dir);
    const cmd = try std.fmt.allocPrint(a, "printf 'lib = %s/{s}/libdir\\n' \"$PWD\" > '{s}/mounts' && SHALE_MOUNTS='{s}/mounts' '{s}' --root '{s}' lib:hi.shale", .{ dir, dir, dir, shale_exe, dir });
    defer a.free(cmd);
    const r = try std.process.run(a, io, .{ .argv = &.{ "sh", "-c", cmd } });
    defer a.free(r.stdout);
    defer a.free(r.stderr);
    errdefer std.debug.print("{s}\n", .{r.stderr});
    try testing.expectEqualStrings("from the mount\n", r.stdout);

    const none = try std.fmt.allocPrint(a, "SHALE_MOUNTS='{s}/mounts' '{s}' --root '{s}' nope:hi.shale", .{ dir, shale_exe, dir });
    defer a.free(none);
    const n = try std.process.run(a, io, .{ .argv = &.{ "sh", "-c", none } });
    defer a.free(n.stdout);
    defer a.free(n.stderr);
    try testing.expect(std.mem.indexOf(u8, n.stderr, "no mount named `nope`") != null);
}

test "cli: a root spelled with `.`, `..` or a link is the folder itself: a read-only or secrets mount inside it holds, and real(\".\") is the folder" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var tmp = testing.tmpDir(.{});
    defer tmp.cleanup();
    const a = testing.allocator;
    try tmp.dir.createDirPath(io, "r/locked");
    try tmp.dir.createDirPath(io, "r/tok");
    try tmp.dir.createDirPath(io, "r/sub");
    try tmp.dir.writeFile(io, .{ .sub_path = "r/locked/a.txt", .data = "orig\n" });
    try tmp.dir.writeFile(io, .{ .sub_path = "r/tok/t", .data = "secret\n" });
    try tmp.dir.symLink(io, "r", "link", .{ .is_directory = true });
    try tmp.dir.writeFile(io, .{ .sub_path = "p.shale", .data =
        \\function w() replace("locked/a.txt", "changed\n") end
        \\function r() return read("tok/t") end
        \\print(sub(real("."), -2))
        \\print(attempt(w).class, attempt(r).class)
        \\
    });
    const dir = try std.fmt.allocPrint(a, ".zig-cache/tmp/{s}", .{&tmp.sub_path});
    defer a.free(dir);
    for ([_][]const u8{ "r/.", "r/sub/..", "link" }) |spelled| {
        const cmd = try std.fmt.allocPrint(a, "printf 'locked = %s/{s}/r/locked ro\\ntok = %s/{s}/r/tok secrets\\n' \"$PWD\" \"$PWD\" > '{s}/mounts' && SHALE_MOUNTS='{s}/mounts' '{s}' --root \"$PWD/{s}/{s}\" '{s}/p.shale'", .{ dir, dir, dir, dir, shale_exe, dir, spelled, dir });
        defer a.free(cmd);
        const r = try std.process.run(a, io, .{ .argv = &.{ "sh", "-c", cmd } });
        defer a.free(r.stdout);
        defer a.free(r.stderr);
        errdefer std.debug.print("{s}: {s}{s}\n", .{ spelled, r.stdout, r.stderr });
        try testing.expectEqualStrings("/r\nrefused\trefused\n", r.stdout);
        const kept = try tmp.dir.readFileAlloc(io, "r/locked/a.txt", a, .limited(1 << 16));
        defer a.free(kept);
        try testing.expectEqualStrings("orig\n", kept);
    }
}
