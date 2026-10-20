//! Hunts over generated scripts (src/script/gen.zig), each run through `script.run`, so
//! the gate holds the VM to the walker on every one, sliced and as a background run.
//! Each hunt is an invariant of its own, and the coverage SDK says what the scripts
//! reached: `always` for the invariant, `sometimes` for each case a hunt must reach. A
//! hunt fails on a broken invariant where it breaks, prints the SDK's report at its end,
//! and fails when a case on its floor was never reached.

const std = @import("std");
const script = @import("shale").script;
const options = @import("script_test_options");
const coverage = @import("coverage");
const gen = @import("gen.zig");
const testing = std.testing;

const Class = script.diag.Class;
const Budget = script.diag.Budget;

var out_a: [256 * 1024]u8 = undefined;
var out_b: [256 * 1024]u8 = undefined;

const Rig = struct {
    tmp: std.testing.TmpDir,
    threaded: std.Io.Threaded,
    root: []const u8,

    fn init(self: *Rig) !void {
        self.tmp = testing.tmpDir(.{});
        self.threaded = std.Io.Threaded.init(testing.allocator, .{});
        self.root = try std.fs.path.join(testing.allocator, &.{ ".zig-cache", "tmp", &self.tmp.sub_path, "data" });
        try self.tmp.dir.createDirPath(self.threaded.io(), "data/gen");
    }

    fn deinit(self: *Rig) void {
        testing.allocator.free(self.root);
        self.threaded.deinit();
        self.tmp.cleanup();
    }

    /// A fresh store for the next script: what one wrote, none after it reads.
    fn clear(self: *Rig) !void {
        const io = self.threaded.io();
        try self.tmp.dir.deleteTree(io, "data/gen");
        try self.tmp.dir.createDirPath(io, "data/gen");
    }

    fn run(self: *Rig, src: []const u8, budgets: script.Budgets, out: []u8) script.Result {
        return script.run(self.threaded.io(), testing.allocator, self.root, src, budgets, 1_700_000_000, out);
    }

    fn runOn(self: *Rig, engine: script.Engine, src: []const u8, budgets: script.Budgets, out: []u8) script.Result {
        return script.runOn(engine, self.threaded.io(), testing.allocator, self.root, src, budgets, 1_700_000_000, out, .{});
    }
};

/// The source, for a failure's message.
fn show(src: []const u8, comptime what: []const u8, r: *const script.Result) void {
    std.debug.print("── " ++ what ++ "\n{s}\n", .{src});
    if (r.stop) |s| std.debug.print("── stopped {d}:{d} {s} {?s}: {s}\n", .{ s.line, s.column, @tagName(s.class), if (s.budget) |b| @tagName(b) else null, s.message }) else std.debug.print("── finished, {d} steps\n", .{r.steps});
}

fn sameEnd(a: *const script.Result, b: *const script.Result) bool {
    if (!std.mem.eql(u8, a.output, b.output) or a.steps != b.steps) return false;
    if ((a.stop == null) != (b.stop == null)) return false;
    const x = a.stop orelse return true;
    const y = b.stop.?;
    return x.class == y.class and x.budget == y.budget and x.line == y.line and x.column == y.column and std.mem.eql(u8, x.message, y.message);
}

/// The SDK's verdict at a hunt's end: each case on `floor` with what the hunt saw of it,
/// and the hunt failed when one was never reached. (A broken `always` failed it already,
/// where it broke.)
fn verdict(floor: []const u8) !void {
    var buf: [16 * 1024]u8 = undefined;
    var w: std.Io.Writer = .fixed(&buf);
    var lines = std.mem.splitScalar(u8, floor, '\n');
    while (lines.next()) |line| {
        var it = coverage.catalog();
        while (it.next()) |site| {
            if (!std.mem.eql(u8, std.mem.span(site.message), line)) continue;
            try w.print("{s} {s}  ({d} true, {d} false)\n", .{ if (site.holds()) "ok  " else "MISS", line, site.passes, site.fails });
        }
    }
    // And what the hunt touched beyond its floor, reached or not.
    var it = coverage.catalog();
    while (it.next()) |site| {
        if (site.passes + site.fails == 0 or std.mem.indexOf(u8, floor, std.mem.span(site.message)) != null) continue;
        try w.print("{s} {s}  (off the floor; {d} true, {d} false)\n", .{ if (site.holds()) "ok  " else "MISS", site.message, site.passes, site.fails });
    }
    const under = try coverage.checkFloor(floor, &w);
    std.debug.print("{s}", .{w.buffered()});
    if (under > 0) return error.UnderTheFloor;
}

/// Whether a stop is one line with a class, placed inside the source.
fn oneLine(src: []const u8, s: script.Stop) bool {
    if (std.mem.indexOfScalar(u8, s.message, '\n') != null) return false;
    if (s.line < 1 or s.column < 1) return false;
    var it = std.mem.splitScalar(u8, src, '\n');
    var n: u32 = 0;
    while (it.next()) |l| {
        n += 1;
        if (n == s.line) return s.column <= l.len + 1;
    }
    // A stop at the end of a source that ends with a newline is on the line after it.
    return s.line == n + 1 and s.column == 1;
}

fn reach(r: *const script.Result) void {
    coverage.sometimes(@src(), r.stop == null, "a generated script finishes", null);
    const s = r.stop orelse return;
    inline for (comptime std.enums.values(Class)) |c| {
        if (c == .syntax or c == .refused or c == .name) continue;
        coverage.sometimes(@src(), s.class == c, "a generated script stops on " ++ @tagName(c), null);
    }
    inline for (comptime std.enums.values(Budget)) |b| {
        coverage.sometimes(@src(), s.budget == b, "a generated script stops on the " ++ @tagName(b) ++ " budget", null);
    }
}

test "expensive fuzz: generated scripts parse, and each ends inside its budgets with a stop of one line and a class" {
    coverage.reset();
    var rig: Rig = undefined;
    try rig.init();
    defer rig.deinit();
    var prng = std.Random.DefaultPrng.init(0x6e6e);
    for (0..options.fuzz_runs) |_| {
        var arena = std.heap.ArenaAllocator.init(testing.allocator);
        defer arena.deinit();
        var g = gen.Gen.init(prng.random(), arena.allocator(), .{});
        const src = try g.script();
        try rig.clear();
        var r = rig.run(src, .{}, &out_a);
        defer r.deinit();
        reach(&r);
        if (r.stop) |s| {
            const parsed = s.class != .syntax and s.class != .refused and s.class != .name;
            if (!parsed) show(src, "a generated script that does not parse", &r);
            coverage.always(@src(), parsed, "a generated script parses", null);
            const one = oneLine(src, s);
            if (!one) show(src, "a stop of more than one line, or outside the source", &r);
            coverage.always(@src(), one, "a stop is one line with a class, inside the source", null);
        }
    }
    try verdict(
        \\a generated script parses
        \\a stop is one line with a class, inside the source
        \\a generated script finishes
        \\a generated script stops on type
        \\a generated script stops on index
        \\a generated script stops on arithmetic
        \\a generated script stops on argument
        \\a generated script stops on budget
        \\a generated script stops on the steps budget
        \\a generated script stops on the changes budget
    );
}

/// Each store budget's use, exact: under exactly what the run used it ends as it did,
/// and under one less it stops on that budget.
const Exact = struct { budget: Budget, used: u64 };

fn exactOnes(r: *const script.Result) [5]Exact {
    return .{
        .{ .budget = .steps, .used = r.steps },
        .{ .budget = .reads, .used = r.reads },
        .{ .budget = .read_bytes, .used = r.read_bytes },
        .{ .budget = .output, .used = r.output.len },
        .{ .budget = .changes, .used = r.changes },
    };
}

fn withBudget(b: Budget, n: u64) script.Budgets {
    var budgets: script.Budgets = .{};
    switch (b) {
        .steps => budgets.steps = n,
        .reads => budgets.reads = n,
        .read_bytes => budgets.read_bytes = n,
        .output => budgets.output_bytes = @intCast(n),
        .changes => budgets.changes = n,
        .arena => budgets.arena_bytes = @intCast(n),
        else => unreachable,
    }
    return budgets;
}

test "expensive fuzz: generated scripts: a budget is exact, ending as it did under what the run used and stopping on it under one less" {
    coverage.reset();
    var rig: Rig = undefined;
    try rig.init();
    defer rig.deinit();
    var prng = std.Random.DefaultPrng.init(0xe4ac7);
    for (0..options.fuzz_runs) |run_index| {
        var arena = std.heap.ArenaAllocator.init(testing.allocator);
        defer arena.deinit();
        var g = gen.Gen.init(prng.random(), arena.allocator(), .{ .mischief = 0 });
        const src = try g.script();
        try rig.clear();
        var r = rig.run(src, .{}, &out_a);
        defer r.deinit();
        coverage.sometimes(@src(), r.stop == null, "a clean generated script finishes", null);
        if (r.stop != null) continue;
        for (exactOnes(&r)) |e| {
            {
                try rig.clear();
                var at = rig.run(src, withBudget(e.budget, e.used), &out_b);
                defer at.deinit();
                const same = sameEnd(&r, &at);
                if (!same) {
                    std.debug.print("── under exactly the {s} it used, {d}:\n", .{ @tagName(e.budget), e.used });
                    show(src, "the run", &at);
                }
                coverage.always(@src(), same, "a run ends as it did under exactly the budget it used", null);
            }
            if (e.used == 0) continue;
            coverage.sometimes(@src(), e.budget == .reads, "a run's reads are held to one less", null);
            coverage.sometimes(@src(), e.budget == .read_bytes, "a run's read bytes are held to one less", null);
            coverage.sometimes(@src(), e.budget == .output, "a run's output is held to one less", null);
            coverage.sometimes(@src(), e.budget == .changes, "a run's changes are held to one less", null);
            try rig.clear();
            var under = rig.run(src, withBudget(e.budget, e.used - 1), &out_b);
            defer under.deinit();
            const stopped = if (under.stop) |s| s.class == .budget and s.budget == e.budget else false;
            if (!stopped) {
                std.debug.print("── under one less {s} than it used, {d}:\n", .{ @tagName(e.budget), e.used - 1 });
                show(src, "the run", &under);
            }
            coverage.always(@src(), stopped, "a run stops on a budget one less than it used", null);
        }
        // The arena, by its least: what a run holds at its end is not the most it held,
        // so the least budget it finishes under is found, then held to.
        if (run_index % 4 != 0) continue;
        var lo: u64 = 0;
        var hi: u64 = (script.Budgets{}).arena_bytes;
        while (lo + 1 < hi) {
            const mid = lo + (hi - lo) / 2;
            try rig.clear();
            var t = rig.run(src, withBudget(.arena, mid), &out_b);
            defer t.deinit();
            if (t.stop == null) hi = mid else lo = mid;
        }
        {
            try rig.clear();
            var at = rig.run(src, withBudget(.arena, hi), &out_b);
            defer at.deinit();
            const same = sameEnd(&r, &at);
            if (!same) show(src, "under the least arena it finishes in, it ends otherwise", &at);
            coverage.always(@src(), same, "a run ends as it did under the least arena it finishes in", null);
        }
        if (hi == 0) continue;
        try rig.clear();
        var under = rig.run(src, withBudget(.arena, hi - 1), &out_b);
        defer under.deinit();
        const stopped = if (under.stop) |s| s.class == .budget and s.budget == .arena else false;
        if (!stopped) show(src, "under one byte less arena, it does not stop on the arena", &under);
        coverage.always(@src(), stopped, "a run stops on the arena a byte under the least it finishes in", null);
    }
    try verdict(
        \\a clean generated script finishes
        \\a run ends as it did under exactly the budget it used
        \\a run stops on a budget one less than it used
        \\a run's reads are held to one less
        \\a run's read bytes are held to one less
        \\a run's output is held to one less
        \\a run's changes are held to one less
        \\a run ends as it did under the least arena it finishes in
        \\a run stops on the arena a byte under the least it finishes in
    );
}

test "expensive fuzz: generated scripts: the VM in slices of 1 to 64 steps ends as it does in one" {
    coverage.reset();
    var rig: Rig = undefined;
    try rig.init();
    defer rig.deinit();
    var prng = std.Random.DefaultPrng.init(0x511ce);
    for (0..@max(1, options.fuzz_runs / 8)) |_| {
        var arena = std.heap.ArenaAllocator.init(testing.allocator);
        defer arena.deinit();
        var g = gen.Gen.init(prng.random(), arena.allocator(), .{});
        const src = try g.script();
        try rig.clear();
        var whole = rig.runOn(.vm, src, .{}, &out_a);
        defer whole.deinit();
        coverage.sometimes(@src(), whole.stop == null, "a run in slices finishes", null);
        coverage.sometimes(@src(), whole.stop != null, "a run in slices stops", null);
        for (1..65) |n| {
            try rig.clear();
            var sliced = rig.runOn(.{ .vm_sliced = n }, src, .{}, &out_b);
            defer sliced.deinit();
            const same = sameEnd(&whole, &sliced);
            if (!same) {
                std.debug.print("── in slices of {d}:\n", .{n});
                show(src, "the sliced run", &sliced);
                show(src, "the whole run", &whole);
            }
            coverage.always(@src(), same, "a run in slices ends as it does in one", null);
        }
    }
    try verdict(
        \\a run in slices finishes
        \\a run in slices stops
        \\a run in slices ends as it does in one
    );
}

test "expensive sweep: numbers: tostring(tonumber(x)) is x at every 64-bit extreme and beside it, and arithmetic past one stops" {
    var rig: Rig = undefined;
    try rig.init();
    defer rig.deinit();
    const max = std.math.maxInt(i64);
    const min = std.math.minInt(i64);
    const values = [_]i64{ min, min + 1, min + 2, -2, -1, 0, 1, 2, max - 2, max - 1, max };
    for (values) |v| {
        const src = try std.fmt.allocPrint(testing.allocator, "print(tostring(tonumber(\"{d}\")))", .{v});
        defer testing.allocator.free(src);
        var r = rig.run(src, .{}, &out_a);
        defer r.deinit();
        const want = try std.fmt.allocPrint(testing.allocator, "{d}\n", .{v});
        defer testing.allocator.free(want);
        if (r.stop != null) show(src, "a number's round trip stopped", &r);
        try testing.expectEqualStrings(want, r.output);
    }
    // One past each extreme is not a number, read or written.
    for ([_][]const u8{ "9223372036854775808", "-9223372036854775809" }) |text| {
        const src = try std.fmt.allocPrint(testing.allocator, "print(tonumber(\"{s}\"))", .{text});
        defer testing.allocator.free(src);
        var r = rig.run(src, .{}, &out_a);
        defer r.deinit();
        try testing.expect(r.stop != null);
    }
    const past = [_][]const u8{
        "print(9223372036854775807 + 1)",
        "print((-9223372036854775807 - 1) - 1)",
        "print(9223372036854775807 * 2)",
        "print((-9223372036854775807 - 1) * -1)",
        "print(-(-9223372036854775807 - 1))",
        "print((-9223372036854775807 - 1) // -1)",
        "print(abs(-9223372036854775807 - 1))",
        "local x = 9223372036854775806 x = x + 1 x = x + 1",
        "local x = 1 for i = 1, 63 do x = x * 2 end",
    };
    for (past) |src| {
        var r = rig.run(src, .{}, &out_a);
        defer r.deinit();
        const s = r.stop orelse {
            show(src, "arithmetic past an extreme finished", &r);
            return error.TestUnexpectedResult;
        };
        if (s.class != .arithmetic) show(src, "arithmetic past an extreme stopped otherwise", &r);
        try testing.expectEqual(Class.arithmetic, s.class);
    }
    // And the neighbours of each extreme, reached by arithmetic, are themselves.
    var r = rig.run("print(9223372036854775806 + 1, (-9223372036854775807 - 1) + 1, -9223372036854775807 - 1, (-9223372036854775807 - 1) % -1, (-9223372036854775807 - 1) // 1)", .{}, &out_a);
    defer r.deinit();
    try testing.expectEqualStrings("9223372036854775807\t-9223372036854775807\t-9223372036854775808\t0\t-9223372036854775808\n", r.output);
}
