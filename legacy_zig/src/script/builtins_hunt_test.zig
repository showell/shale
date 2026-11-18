//! The builtins' hunt (`zig build fuzz`, src/script/hunt.zig): a sweep too long
//! for `zig build test`, against a real store in a temporary folder. It has its
//! own copy of the part of builtins_test.zig's fixture it uses, since a file
//! belongs to one module only: builtins_test.zig is in the `shale` module,
//! which the hunt imports as a module (importing it by path would pull in
//! every unit test).

const std = @import("std");
const testing = std.testing;
const script = @import("shale").script;

var out_buf: [64 * 1024]u8 = undefined;

const Fixture = struct {
    tmp: std.testing.TmpDir,
    threaded: std.Io.Threaded,
    root: []const u8,

    fn init(self: *Fixture) !void {
        self.tmp = testing.tmpDir(.{});
        self.threaded = std.Io.Threaded.init(testing.allocator, .{});
        self.root = try std.fs.path.join(testing.allocator, &.{ ".zig-cache", "tmp", &self.tmp.sub_path, "data" });
        try self.tmp.dir.createDir(self.threaded.io(), "data", .default_dir);
    }

    fn deinit(self: *Fixture) void {
        testing.allocator.free(self.root);
        self.threaded.deinit();
        self.tmp.cleanup();
    }

    fn run(self: *Fixture, src: []const u8, budgets: script.Budgets) script.Result {
        return script.run(self.threaded.io(), testing.allocator, self.root, src, budgets, 0, &out_buf);
    }
};

test "expensive sweep: builtins: a change on the disk is in the effects log, however little arena is left (every limit swept)" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    const io = f.threaded.io();
    var finished: usize = 0;
    var stopped_by_arena: usize = 0;
    var limit: usize = 0;
    while (limit <= 32 * 1024) : (limit += 8) {
        var r = f.run("replace(\"x.txt\", \"v\")", .{ .arena_bytes = limit });
        defer r.deinit();
        if (r.finished()) finished += 1;
        if (r.stop) |s| if (s.budget == .arena) {
            stopped_by_arena += 1;
        };
        const on_disk = (f.tmp.dir.statFile(io, "data/x.txt", .{}) catch null) != null;
        if (on_disk) {
            try testing.expectEqual(@as(usize, 1), r.effects.len);
            try testing.expectEqualStrings("x.txt", r.effects[0].path);
            try f.tmp.dir.deleteFile(io, "data/x.txt");
        }
    }
    // Not vacuous: the sweep crossed from too little arena to enough.
    try testing.expect(finished > 0 and stopped_by_arena > 0);
}
