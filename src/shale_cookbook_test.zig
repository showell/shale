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

/// Whether `text` calls `name`: `name(` with no letter, digit or `_` before it, or
/// `attempt(name,`, the language's call by name.
fn calls(text: []const u8, name: []const u8) bool {
    var at: usize = 0;
    while (std.mem.indexOfPos(u8, text, at, name)) |i| : (at = i + 1) {
        const before_ok = i == 0 or !(std.ascii.isAlphanumeric(text[i - 1]) or text[i - 1] == '_');
        const after = i + name.len;
        if (!before_ok or after >= text.len) continue;
        if (text[after] == '(') return true;
        if (text[after] == ',' and std.mem.endsWith(u8, text[0..i], "attempt(")) return true;
    }
    return false;
}

test "preludes: every function of the Linux and git preludes is called by some test, or by a function a test calls" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    // Every test file's text, one string.
    var tests: std.ArrayList(u8) = .empty;
    var dir = try std.Io.Dir.cwd().openDir(io, "src", .{ .iterate = true });
    defer dir.close(io);
    var walker = try dir.walk(a);
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, "_test.zig")) continue;
        try tests.appendSlice(a, try dir.readFileAlloc(io, e.path, a, .limited(4 << 20)));
        try tests.append(a, '\n');
    }
    // Each function's name and its text, to its next `function` line.
    const Function = struct { path: []const u8, name: []const u8, body: []const u8, reached: bool = false };
    var functions: std.ArrayList(Function) = .empty;
    for ([_][]const u8{ "src/shale_prelude_linux.shale", "src/shale_prelude_git.shale" }) |path| {
        const source = try std.Io.Dir.cwd().readFileAlloc(io, path, a, .limited(1 << 20));
        var at: usize = 0;
        while (std.mem.indexOfPos(u8, source, at, "\nfunction ")) |i| {
            const head = i + "\nfunction ".len;
            const open = std.mem.indexOfScalarPos(u8, source, head, '(') orelse break;
            const next = std.mem.indexOfPos(u8, source, head, "\nfunction ") orelse source.len;
            try functions.append(a, .{ .path = path, .name = source[head..open], .body = source[open..next] });
            at = next;
        }
    }
    // Reached: called by a test, or by a function reached.
    for (functions.items) |*f| f.reached = calls(tests.items, f.name);
    var grew = true;
    while (grew) {
        grew = false;
        for (functions.items) |caller| {
            if (!caller.reached) continue;
            for (functions.items) |*f| {
                if (!f.reached and calls(caller.body, f.name)) {
                    f.reached = true;
                    grew = true;
                }
            }
        }
    }
    var uncalled: usize = 0;
    for (functions.items) |f| {
        if (f.reached) continue;
        std.debug.print("{s}: `{s}` is called by no test, nor by a function a test calls\n", .{ f.path, f.name });
        uncalled += 1;
    }
    try testing.expect(functions.items.len >= 40);
    try testing.expectEqual(@as(usize, 0), uncalled);
}

test "preludes: each one compiled whole, its lines with code written for tools/coverage.shale" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var out: std.ArrayList(u8) = .empty;
    // Each prelude parsed as a script, after those it calls, so every function is
    // compiled; a function's lines are its file's, as when it loads as a prelude.
    const files = [_]struct { path: []const u8, after: []const script.parse.Prelude }{
        .{ .path = "src/shale_prelude_linux.shale", .after = &.{} },
        .{ .path = "src/shale_prelude_git.shale", .after = &.{host_linux.linux_prelude} },
    };
    for (files) |f| {
        const source = try std.Io.Dir.cwd().readFileAlloc(io, f.path, a, .limited(1 << 20));
        var stop: ?script.diag.Stop = null;
        const program = try script.parse.parseMeasured(a, a, source, 200, &stop, null, f.after);
        const code = try script.compile.compile(a, a, &program, script.builtins.most_args, 2000);
        // Each function's ops run from its entry to the next function's.
        var entries: std.ArrayList(u32) = .empty;
        for (code.functions) |fun| try entries.append(a, fun.entry);
        try entries.append(a, code.main.entry);
        try entries.append(a, @intCast(code.ops.len));
        std.mem.sort(u32, entries.items, {}, std.sort.asc(u32));
        var lines: std.AutoArrayHashMapUnmanaged(u32, void) = .empty;
        for (code.functions) |fun| {
            // A function of a prelude this one calls is that prelude's.
            if (std.mem.startsWith(u8, fun.name, "the ")) continue;
            const end = for (entries.items) |e| (if (e > fun.entry) break e) else code.ops.len;
            for (code.ops[fun.entry..end]) |op| if (op.pos.line > 0) try lines.put(a, op.pos.line, {});
        }
        try testing.expect(lines.count() > 100);
        for (lines.keys()) |l| try out.print(a, "{s} {d}\n", .{ f.path, l });
    }
    try std.Io.Dir.cwd().createDirPath(io, ".zig-cache/coverage");
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = ".zig-cache/coverage/prelude_code.txt", .data = out.items });
}
