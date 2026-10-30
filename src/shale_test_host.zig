//! A Linux host for tests: a temporary root, the host over it, the build's
//! fixture program on `PATH`, and runs through the gate.

const std = @import("std");
const shale = @import("shale");
const script = shale.script;
const testing = std.testing;
const Host = @import("shale_host_linux.zig").Host;
const fixture = @import("shale_test_options").fixture;
const shale_exe = @import("shale_test_options").shale;

var out_buf: [64 * 1024]u8 = undefined;

/// The preludes, by the name their functions run under, and their files.
pub const prelude_files = [_]struct { shown: []const u8, path: []const u8 }{
    .{ .shown = "the Linux prelude's ", .path = "src/shale_prelude_linux.shale" },
    .{ .shown = "the git prelude's ", .path = "src/shale_prelude_git.shale" },
};

/// **THE PRELUDES' LINES THE TESTS RAN** (metal-vmm QUEUE 241 F): `file << 32 | line`,
/// over every run of this process through a `T`, each op's line in a prelude function
/// (`interp.LineHook`). Written out as each `T` ends, for tools/coverage.shale.
var prelude_hits: std.AutoArrayHashMapUnmanaged(u64, void) = .empty;

fn preludeHit(_: *anyopaque, function: []const u8, line: u32) void {
    for (prelude_files, 0..) |f, i| if (std.mem.startsWith(u8, function, f.shown)) {
        prelude_hits.put(std.heap.page_allocator, @as(u64, i) << 32 | line, {}) catch {};
        return;
    };
}

const line_hook: script.interp.LineHook = .{ .ctx = @ptrCast(&prelude_hits), .hit = preludeHit };

/// This process's hits, `<path> <line>` a line, to `<dir>/<pid>.txt`: `dir` the
/// `SHALE_PRELUDE_HITS` folder tools/coverage.shale made for its run alone, else
/// `.zig-cache/coverage/prelude_hits`.
fn writeHits(io: std.Io) void {
    var text: std.ArrayList(u8) = .empty;
    defer text.deinit(std.heap.page_allocator);
    for (prelude_hits.keys()) |k| text.print(std.heap.page_allocator, "{s} {d}\n", .{ prelude_files[@intCast(k >> 32)].path, @as(u32, @truncate(k)) }) catch return;
    const dir: []const u8 = std.testing.environ.getPosix("SHALE_PRELUDE_HITS") orelse ".zig-cache/coverage/prelude_hits";
    var name: [std.fs.max_path_bytes]u8 = undefined;
    const path = std.fmt.bufPrint(&name, "{s}/{d}.txt", .{ dir, std.os.linux.getpid() }) catch return;
    std.Io.Dir.cwd().createDirPath(io, dir) catch return;
    std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = text.items }) catch return;
}

pub const T = struct {
    tmp: std.testing.TmpDir,
    threaded: std.Io.Threaded,
    arena: std.heap.ArenaAllocator,
    root: []const u8,
    host: Host,
    path_env: []const u8,
    /// The preludes after the core's each run is given.
    preludes: []const script.parse.Prelude = &.{},
    /// How an edit's diff prints (the CLI's is `summary`).
    diffs: script.interp.Diffs = .full,
    /// The budgets each run is given (the defaults unless a test sets them).
    budgets: script.Budgets = .{},

    pub fn init(t: *T) !void {
        t.preludes = &.{};
        t.diffs = .full;
        t.budgets = .{};
        t.tmp = testing.tmpDir(.{});
        t.threaded = std.Io.Threaded.init(testing.allocator, .{});
        t.arena = std.heap.ArenaAllocator.init(testing.allocator);
        const a = t.arena.allocator();
        var cwd: [std.fs.max_path_bytes]u8 = undefined;
        const n = std.os.linux.getcwd(&cwd, cwd.len);
        const here = std.mem.sliceTo(cwd[0..n], 0);
        t.root = try std.fmt.allocPrint(a, "{s}/.zig-cache/tmp/{s}", .{ here, &t.tmp.sub_path });
        const fixture_abs = if (std.fs.path.isAbsolute(fixture)) fixture else try std.fmt.allocPrint(a, "{s}/{s}", .{ here, fixture });
        t.path_env = try std.fmt.allocPrint(a, "PATH={s}:/usr/bin:/bin", .{std.fs.path.dirname(fixture_abs).?});
        const reaper = if (std.fs.path.isAbsolute(shale_exe)) shale_exe else try std.fmt.allocPrint(a, "{s}/{s}", .{ here, shale_exe });
        t.host = .{ .alloc = testing.allocator, .root_abs = t.root, .cwd_abs = try a.dupe(u8, here), .env = try a.dupe([]const u8, &.{t.path_env}), .reaper = reaper };
    }

    pub fn deinit(t: *T) void {
        writeHits(t.threaded.io());
        t.host.deinit();
        t.arena.deinit();
        t.threaded.deinit();
        t.tmp.cleanup();
    }

    pub fn run(t: *T, src: []const u8, mode: script.process.Mode) script.Result {
        const o = t.host.options();
        return script.runWith(t.threaded.io(), testing.allocator, t.root, src, t.budgets, 0, &out_buf, .{ .mode = mode, .host = o.host, .path_hook = o.path_hook, .preludes = t.preludes, .diffs = t.diffs, .line_hook = line_hook });
    }

    /// One run of the VM, outside the gate: for a script whose effects
    /// on the world cannot happen three times alike (a commit, a push).
    pub fn runOnce(t: *T, src: []const u8, mode: script.process.Mode) script.Result {
        const o = t.host.options();
        return script.runOn(.vm, t.threaded.io(), testing.allocator, t.root, src, t.budgets, 0, &out_buf, .{ .mode = mode, .host = o.host, .path_hook = o.path_hook, .preludes = t.preludes, .line_hook = line_hook });
    }

    pub fn expectOutput(t: *T, src: []const u8, want: []const u8) !void {
        var r = t.run(src, .atomic);
        defer r.deinit();
        if (r.stop) |s| {
            std.debug.print("{s}\n  stopped {d}:{d} {s}: {s}\n", .{ src, s.line, s.column, @tagName(s.class), s.message });
            return error.TestUnexpectedResult;
        }
        try testing.expectEqualStrings(want, r.output);
    }

    pub fn expectStop(t: *T, src: []const u8, mode: script.process.Mode, class: script.diag.Class, says: []const u8) !void {
        var r = t.run(src, mode);
        defer r.deinit();
        const s = r.stop orelse {
            std.debug.print("{s}: no stop\n", .{src});
            return error.TestUnexpectedResult;
        };
        errdefer std.debug.print("{s}: {s}: {s}\n", .{ src, @tagName(s.class), s.message });
        try testing.expectEqual(class, s.class);
        try testing.expect(std.mem.indexOf(u8, s.message, says) != null);
    }

    pub fn put(t: *T, name: []const u8, data: []const u8) !void {
        try t.tmp.dir.writeFile(t.threaded.io(), .{ .sub_path = name, .data = data });
    }

    pub fn file(t: *T, name: []const u8) ![]u8 {
        return t.tmp.dir.readFileAlloc(t.threaded.io(), name, t.arena.allocator(), .limited(1 << 20));
    }
};
