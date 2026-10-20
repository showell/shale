//! Scans the sources of src/ and tools/ as text, beside portable_test.zig's scan, for
//! skipped tests and stray hunts. A test that skips is one `zig build test` counts and
//! never runs, so no source may name the skip. A test named `expensive ...` belongs to the
//! hunt (src/script/hunt.zig, build.zig's `hunt_files`), which only `zig build fuzz` runs;
//! one outside those files would run in `test` under a hunt's name, or in no step at all.

const std = @import("std");
const testing = std.testing;
const hunt_files = @import("shale_test_options").hunt_files;

// Built by concatenation, so this file's own text holds neither.
const skip = "Skip" ++ "ZigTest";
const hunt_test = "test \"" ++ "expensive";

const Finding = struct { line: usize, what: []const u8 };

/// The first line of `text` (the file at `path`, from the repo's root)
/// that names the skip, or opens a hunt outside `hunts`.
pub fn finding(path: []const u8, text: []const u8, hunts: []const []const u8) ?Finding {
    const is_hunt = for (hunts) |h| {
        if (std.mem.eql(u8, h, path)) break true;
    } else false;
    var line_no: usize = 0;
    var lines = std.mem.splitScalar(u8, text, '\n');
    while (lines.next()) |line| {
        line_no += 1;
        if (std.mem.indexOf(u8, line, skip) != null) return .{ .line = line_no, .what = "names the skip" };
        if (!is_hunt and std.mem.startsWith(u8, std.mem.trimStart(u8, line, " "), hunt_test))
            return .{ .line = line_no, .what = "opens a hunt outside build.zig's hunt_files" };
    }
    return null;
}

test "hunt guard: a skip anywhere, or a hunt outside the hunt files, is found with its line" {
    const hunts: []const []const u8 = &.{"src/script/fuzz_test.zig"};
    try testing.expectEqual(@as(?Finding, null), finding("src/a.zig", "test \"a\" {}\n", hunts));
    try testing.expectEqual(@as(usize, 2), finding("src/a.zig", "test \"a\" {\n    return error." ++ skip ++ ";\n}\n", hunts).?.line);
    try testing.expectEqual(@as(usize, 1), finding("src/script/fuzz_test.zig", "// " ++ skip ++ "\n", hunts).?.line);
    try testing.expectEqual(@as(usize, 1), finding("tools/b.zig", hunt_test ++ " fuzz: x\" {}\n", hunts).?.line);
    try testing.expectEqual(@as(?Finding, null), finding("src/script/fuzz_test.zig", hunt_test ++ " fuzz: x\" {}\n", hunts));
}

test "hunt guard: no file of src/ or tools/ carries a skip, and every hunt is in hunt_files" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var hunts_seen: usize = 0;
    var bad: usize = 0;
    for ([_][]const u8{ "src", "tools" }) |root| {
        var dir = try std.Io.Dir.cwd().openDir(io, root, .{ .iterate = true });
        defer dir.close(io);
        var walker = try dir.walk(a);
        while (try walker.next(io)) |e| {
            if (e.kind != .file) continue;
            const path = try std.fmt.allocPrint(a, "{s}/{s}", .{ root, e.path });
            const text = try dir.readFileAlloc(io, e.path, a, .limited(8 << 20));
            for (hunt_files) |h| {
                if (std.mem.eql(u8, h, path)) hunts_seen += 1;
            }
            if (finding(path, text, hunt_files)) |f| {
                std.debug.print("{s}:{d} {s}\n", .{ path, f.line, f.what });
                bad += 1;
            }
        }
    }
    try testing.expectEqual(@as(usize, 0), bad);
    // Not vacuous: the walk read every hunt file where build.zig says it is.
    try testing.expectEqual(hunt_files.len, hunts_seen);
}
