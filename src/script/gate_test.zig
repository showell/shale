//! The gate: in the tests, every `script.run` is the VM's, on the store it was
//! given, and the tree-walker's, on a copy of that store made before. The two
//! must agree on everything a run shows: the output, the effects, the stop
//! (its class, budget, place, function and message), the steps, the work, the
//! arena held, and the store each leaves. A difference panics with both runs,
//! so the test it happened in is the reproduction, and every interpreter,
//! builtin, example, cookbook and fuzz test holds the VM to the walker.

const std = @import("std");
const script = @import("script.zig");
const Io = @import("host_io").Io;

/// Runs `source` on the VM whole, on the VM in slices, and on the walker, and
/// panics if they differ. Returns the whole VM run's result.
pub fn run(io: Io, backing: std.mem.Allocator, root: []const u8, source: []const u8, budgets: script.Budgets, now: i64, out: []u8, options: script.Options) script.Result {
    var paths: std.heap.ArenaAllocator = .init(std.heap.page_allocator);
    defer paths.deinit();
    const a = paths.allocator();
    const d = Io.Dir.cwd();
    // The copy's path is exactly as long as the root's: a store call's full
    // path is made in the arena, so a longer one would stop the walker on
    // the arena budget sooner.
    const copy = sibling(io, a, root, "~_#@");
    const cut_copy = sibling(io, a, root, "=+%^");
    const has_root = if (d.openDir(io, root, .{})) |dir| blk: {
        dir.close(io);
        break :blk true;
    } else |_| false;
    if (has_root) {
        copyTree(io, a, root, copy) catch |e| std.debug.panic("the gate can't copy {s}: {t}", .{ root, e });
        copyTree(io, a, root, cut_copy) catch |e| std.debug.panic("the gate can't copy {s}: {t}", .{ root, e });
    }
    defer if (has_root) {
        d.deleteTree(io, copy) catch {};
        d.deleteTree(io, cut_copy) catch {};
    };

    const walker_out = std.testing.allocator.alloc(u8, out.len) catch @panic("the gate's output");
    defer std.testing.allocator.free(walker_out);
    // The walker's memory is its own, so a test that measures the backing
    // allocator measures the VM's run alone.
    var w = script.runOn(.walker, io, std.testing.allocator, if (has_root) copy else root, source, budgets, now, walker_out, options);
    defer w.deinit();
    const v = script.runOn(.vm, io, backing, root, source, budgets, now, out, options);

    // A pause between any two ops changes nothing, so the VM in slices of
    // 1 to 29 steps (chosen by the source) must be the uncut VM in every
    // respect. A run that tried no change is cut as a background run, which
    // only reads and for such a script is the atomic run, slice for slice.
    const cut_out = std.testing.allocator.alloc(u8, out.len) catch @panic("the gate's output");
    defer std.testing.allocator.free(cut_out);
    const slice = 1 + std.hash.Wyhash.hash(0, source) % 29;
    // A run already a background one is cut as itself.
    const engine: script.Engine = if (!v.tried_change and options.mode == .atomic) .{ .vm_background = slice } else .{ .vm_sliced = slice };
    var cut = script.runOn(engine, io, std.testing.allocator, if (has_root) cut_copy else root, source, budgets, now, cut_out, options);
    defer cut.deinit();
    if (!std.mem.eql(u8, v.output, cut.output) or v.steps != cut.steps or v.work != cut.work or v.arena_bytes != cut.arena_bytes or !sameStop(v.stop, cut.stop) or !sameEffects(v.effects, cut.effects) or
        (has_root and !(sameTree(io, a, root, cut_copy) catch false)))
    {
        std.debug.print("\n── THE GATE: the VM in slices of {d} differs from the VM in one piece ──\n{s}\n", .{ slice, source });
        for ([_]struct { name: []const u8, r: *const script.Result }{ .{ .name = "whole", .r = &v }, .{ .name = "cut", .r = &cut } }) |x| {
            std.debug.print("── {s}: {d} steps, {d} work, {d} arena, {d} effects\n", .{ x.name, x.r.steps, x.r.work, x.r.arena_bytes, x.r.effects.len });
            if (x.r.stop) |st| std.debug.print("   stopped {d}:{d} ({s}) {t} {?t}: {s}\n", .{ st.line, st.column, st.function, st.class, st.budget, st.message });
        }
        @panic("the VM in slices differs from the VM whole (metal-vmm QUEUE 211's gate)");
    }

    var why = verdict(&v, &w);
    // Runs that made different changes, as an arena-shaped stop may, leave different
    // stores; their changes were compared above.
    if (why == null and has_root and sameEffects(v.effects, w.effects)) {
        if (!(sameTree(io, a, root, copy) catch false)) why = "the store left";
    }
    if (why) |what| {
        std.debug.print("\n── THE GATE: the VM and the walker differ in {s} ──\n{s}\n", .{ what, source });
        for ([_]struct { name: []const u8, r: *const script.Result }{ .{ .name = "vm", .r = &v }, .{ .name = "walker", .r = &w } }) |x| {
            std.debug.print("── {s}: {d} steps, {d} work, {d} arena, {d} effects\n", .{ x.name, x.r.steps, x.r.work, x.r.arena_bytes, x.r.effects.len });
            if (x.r.stop) |s| std.debug.print("   stopped {d}:{d} ({s}) {t} {?t}: {s}\n", .{ s.line, s.column, s.function, s.class, s.budget, s.message });
            std.debug.print("   output: {s}\n", .{x.r.output[0..@min(x.r.output.len, 400)]});
        }
        @panic("the VM and the tree-walker differ (metal-vmm QUEUE 211's gate)");
    }
    return v;
}

/// What the VM's run `v` and the walker's `w` differ in, by the gate's rules, or null
/// when they agree. The store each left is compared apart, in `run`.
pub fn verdict(v: *const script.Result, w: *const script.Result) ?[]const u8 {
    var why: ?[]const u8 = null;
    // At each `yield` the VM copies what it keeps into a fresh arena,
    // charged as steps, while the walker keeps everything. Their steps, work
    // and arena differ by that, and so may a stop on the steps, the arena or
    // what is kept; all else is the same. Yields are counted on the run, not
    // searched for in the source, so a comment saying "yield" exempts nothing.
    const yields = v.collections > 0;
    // Only a machine stop both engines make is exempt, and only from what
    // their machinery spends differently on the way there (the steps, the
    // work, the arena); the stop, its place included, and the output are
    // held. A broader exemption would hide the machine budget refusing a
    // valid source.
    const machine_shaped = isBudget(v.stop, .machine) and isBudget(w.stop, .machine);
    const arena_shaped = yields and (isArenaShaped(v.stop) or isArenaShaped(w.stop));
    if (arena_shaped) {
        // The engine that stopped first did what the other did, up to its stop: its
        // output and its changes are the other's first ones.
        if (!prefixes(v.output, w.output)) why = "the output";
        if (!firstChanges(v, w)) why = "the effects";
    } else {
        if (!std.mem.eql(u8, v.output, w.output)) why = "the output";
        if (!sameEffects(v.effects, w.effects)) why = "the effects";
        // A machine stop both make is at the same place, but may be at a
        // different depth of the same recursion, so its trail is not compared.
        if (!sameStop(v.stop, w.stop) and !(machine_shaped and samePlace(v.stop.?, w.stop.?))) why = "the stop";
    }
    if (!yields and !machine_shaped) {
        if (v.steps != w.steps) why = "the steps";
        if (v.work != w.work) why = "the work";
        if (v.arena_bytes != w.arena_bytes) why = "the arena";
    }
    return why;
}

/// A sibling of `root` with a name as long as its own, its last byte the first
/// of `bytes` that names no existing sibling.
fn sibling(io: Io, a: std.mem.Allocator, root: []const u8, bytes: []const u8) []u8 {
    const p = a.dupe(u8, std.mem.trimEnd(u8, root, "/")) catch @panic("the gate's path");
    for (bytes) |c| {
        p[p.len - 1] = c;
        if (Io.Dir.cwd().access(io, p, .{})) |_| continue else |_| break;
    }
    return p;
}

fn isBudget(s: ?script.Stop, b: script.diag.Budget) bool {
    const got = (s orelse return false).budget orelse return false;
    return got == b;
}

fn isArenaShaped(s: ?script.Stop) bool {
    const b = (s orelse return false).budget orelse return false;
    return b == .steps or b == .arena or b == .kept;
}

/// Whether the changes of the run that made fewer are the other's first ones. Its last
/// may have been refused where the other's was made, when that refusal is its stop: an
/// arena run out inside the change.
fn firstChanges(v: *const script.Result, w: *const script.Result) bool {
    const n = @min(v.effects.len, w.effects.len);
    if (n == 0) return true;
    if (!sameEffects(v.effects[0 .. n - 1], w.effects[0 .. n - 1])) return false;
    const x = v.effects[n - 1];
    const y = w.effects[n - 1];
    if (!std.mem.eql(u8, x.op, y.op) or !std.mem.eql(u8, x.path, y.path)) return false;
    if (std.mem.eql(u8, x.outcome, y.outcome)) return true;
    return refusedAtItsStop(v, n) or refusedAtItsStop(w, n);
}

/// Whether `r`'s change `n - 1` is its last, refused for want of arena, and its stop.
fn refusedAtItsStop(r: *const script.Result, n: usize) bool {
    return r.effects.len == n and std.mem.eql(u8, r.effects[n - 1].outcome, "OutOfMemory") and isBudget(r.stop, .arena);
}

/// Whether the shorter of `x` and `y` begins the longer.
fn prefixes(x: []const u8, y: []const u8) bool {
    const n = @min(x.len, y.len);
    return std.mem.eql(u8, x[0..n], y[0..n]);
}

/// Whether each effect is the same op on the same path with the same outcome,
/// in order.
fn sameEffects(x: []const script.Effect, y: []const script.Effect) bool {
    if (x.len != y.len) return false;
    for (x, y) |a, b| {
        if (!std.mem.eql(u8, a.op, b.op) or !std.mem.eql(u8, a.path, b.path) or !std.mem.eql(u8, a.outcome, b.outcome)) return false;
    }
    return true;
}

fn sameStop(x: ?script.Stop, y: ?script.Stop) bool {
    if (x == null or y == null) return x == null and y == null;
    const a = x.?;
    const b = y.?;
    return a.line == b.line and a.column == b.column and a.class == b.class and a.budget == b.budget and
        std.mem.eql(u8, a.function, b.function) and std.mem.eql(u8, a.message, b.message) and sameTrail(a.trail, b.trail);
}

/// Whether the stops are the same but for their trails.
fn samePlace(a: script.Stop, b: script.Stop) bool {
    var x = a;
    var y = b;
    x.trail = &.{};
    y.trail = &.{};
    return sameStop(x, y);
}

/// Whether each call is made at the same place in the same function, in order.
fn sameTrail(x: []const script.diag.Call, y: []const script.diag.Call) bool {
    if (x.len != y.len) return false;
    for (x, y) |a, b| {
        if (a.line != b.line or a.column != b.column or !std.mem.eql(u8, a.function, b.function)) return false;
    }
    return true;
}

/// Copies the tree `src` to `dst`, links as links (their targets unread).
fn copyTree(io: Io, a: std.mem.Allocator, src: []const u8, dst: []const u8) !void {
    const d = Io.Dir.cwd();
    try d.createDirPath(io, dst);
    var dir = try d.openDir(io, src, .{ .iterate = true });
    defer dir.close(io);
    var it = dir.iterate();
    while (try it.next(io)) |e| {
        const from = try std.fs.path.join(a, &.{ src, e.name });
        const to = try std.fs.path.join(a, &.{ dst, e.name });
        switch (e.kind) {
            .directory => {
                try copyTree(io, a, from, to);
                try keepTime(io, from, to);
            },
            .sym_link => {
                var buf: [4096]u8 = undefined;
                const n = try d.readLink(io, from, &buf);
                try d.symLink(io, buf[0..n], to, .{});
            },
            else => {
                const data = try d.readFileAlloc(io, from, a, .unlimited);
                try d.writeFile(io, .{ .sub_path = to, .data = data });
                try keepTime(io, from, to);
            },
        }
    }
}

/// Sets `to`'s modification time to `from`'s. A script reads the times
/// (`walk`, `stat`), so the copy keeps them.
fn keepTime(io: Io, from: []const u8, to: []const u8) !void {
    const d = Io.Dir.cwd();
    const st = try d.statFile(io, from, .{});
    try d.setTimestamps(io, to, .{ .modify_timestamp = .{ .new = st.mtime } });
}

/// Whether two trees hold the same names, kinds, bytes and links.
fn sameTree(io: Io, a: std.mem.Allocator, x: []const u8, y: []const u8) !bool {
    const d = Io.Dir.cwd();
    const xs = try listing(io, a, x);
    const ys = try listing(io, a, y);
    if (xs.len != ys.len) return false;
    for (xs, ys) |p, q| {
        if (!std.mem.eql(u8, p.name, q.name) or p.kind != q.kind) return false;
        const from = try std.fs.path.join(a, &.{ x, p.name });
        const to = try std.fs.path.join(a, &.{ y, q.name });
        switch (p.kind) {
            .directory => if (!try sameTree(io, a, from, to)) return false,
            .sym_link => {
                var b1: [4096]u8 = undefined;
                var b2: [4096]u8 = undefined;
                if (!std.mem.eql(u8, b1[0..try d.readLink(io, from, &b1)], b2[0..try d.readLink(io, to, &b2)])) return false;
            },
            else => if (!std.mem.eql(u8, try d.readFileAlloc(io, from, a, .unlimited), try d.readFileAlloc(io, to, a, .unlimited))) return false,
        }
    }
    return true;
}

const Named = struct { name: []const u8, kind: Io.File.Kind };

fn listing(io: Io, a: std.mem.Allocator, path: []const u8) ![]Named {
    var dir = try Io.Dir.cwd().openDir(io, path, .{ .iterate = true });
    defer dir.close(io);
    var out: std.ArrayList(Named) = .empty;
    var it = dir.iterate();
    while (try it.next(io)) |e| try out.append(a, .{ .name = try a.dupe(u8, e.name), .kind = e.kind });
    std.mem.sort(Named, out.items, {}, struct {
        fn lt(_: void, p: Named, q: Named) bool {
            return std.mem.lessThan(u8, p.name, q.name);
        }
    }.lt);
    return out.items;
}

test "gate: when only the walker outgrows the arena, keeping what the VM's yields let go, the changes it made before its stop are the VM's first ones, and agree" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var threaded = std.Io.Threaded.init(std.testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try tmp.dir.createDirPath(io, "data");
    const root = try std.fs.path.join(std.testing.allocator, &.{ ".zig-cache", "tmp", &tmp.sub_path, "data" });
    defer std.testing.allocator.free(root);
    // Each turn makes 4000 bytes and lets them go at its yield: the VM holds one turn's,
    // the walker every turn's.
    const source = "for i = 1, 40 do local s = rep(\"x\", 4000) yield() replace(\"f\" .. (i % 3), \"v\") print(i) end";
    const budgets: script.Budgets = .{ .arena_bytes = 64 * 1024 };
    var out_v: [4096]u8 = undefined;
    var out_w: [4096]u8 = undefined;
    var v = script.runOn(.vm, io, std.testing.allocator, root, source, budgets, 0, &out_v, .{});
    defer v.deinit();
    var w = script.runOn(.walker, io, std.testing.allocator, root, source, budgets, 0, &out_w, .{});
    defer w.deinit();
    try std.testing.expectEqual(null, v.stop);
    try std.testing.expectEqual(script.diag.Budget.arena, w.stop.?.budget.?);
    try std.testing.expect(w.effects.len < v.effects.len);
    try std.testing.expectEqual(@as(?[]const u8, null), verdict(&v, &w));
}

test "gate: when the walker's arena runs out in a change, that change is refused where the VM's is made, and the two agree up to it" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    var threaded = std.Io.Threaded.init(std.testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const root = try std.fs.path.join(std.testing.allocator, &.{ ".zig-cache", "tmp", &tmp.sub_path, "data" });
    defer std.testing.allocator.free(root);
    const source = "for i = 1, 40 do local s = rep(\"x\", 300) yield() replace(\"f\" .. (i % 3), s .. i) end";
    var out_v: [4096]u8 = undefined;
    var out_w: [4096]u8 = undefined;
    // Every arena from 4 KiB to 32 KiB, so some budget runs out inside a change.
    var refused_in_a_change = false;
    var n: usize = 4096;
    while (n <= 32 * 1024) : (n += 64) {
        try tmp.dir.deleteTree(io, "data");
        try tmp.dir.createDirPath(io, "data");
        var v = script.runOn(.vm, io, std.testing.allocator, root, source, .{ .arena_bytes = n }, 0, &out_v, .{});
        defer v.deinit();
        try tmp.dir.deleteTree(io, "data");
        try tmp.dir.createDirPath(io, "data");
        var w = script.runOn(.walker, io, std.testing.allocator, root, source, .{ .arena_bytes = n }, 0, &out_w, .{});
        defer w.deinit();
        if (w.effects.len > 0 and std.mem.eql(u8, w.effects[w.effects.len - 1].outcome, "OutOfMemory")) refused_in_a_change = true;
        if (verdict(&v, &w)) |why| {
            std.debug.print("an arena of {d}: the gate says they differ in {s}\n", .{ n, why });
            return error.TestUnexpectedResult;
        }
    }
    try std.testing.expect(refused_in_a_change);
}
