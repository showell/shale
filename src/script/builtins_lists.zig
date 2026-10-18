//! **LISTS' AND MAPS' BUILTINS**: growing a list, sorting it (by value, or
//! a list of maps by a key), reversing it, and a map's keys and size. Each
//! charges a step an element it touches; a sort is charged its bound
//! before it runs.

const std = @import("std");
const ast = @import("ast.zig");
const interp = @import("interp.zig");
const value = @import("value.zig");
const bi = @import("builtins.zig");

const Run = interp.Run;
const Value = value.Value;
const Error = interp.Error;
const spec = bi.spec;

pub const specs = [_]bi.Spec{
    spec(.push, 2, 2, push),
    spec(.sort, 1, 3, sort),
    spec(.reverse, 1, 1, reverse),
    spec(.take, 2, 2, take),
    spec(.last, 2, 2, last),
    spec(.keys, 1, 1, keys),
    spec(.has_key, 2, 2, hasKey),
    spec(.get, 2, 3, get),
    spec(.count, 1, 1, count),
};

fn push(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const t = try bi.listArg(run, pos, .push, args[0]);
    if (args[1] == .nil) return run.fail(pos, .type, "a list cannot hold nil", .{});
    t.list.append(run.alloc, args[1]) catch return run.oom(pos);
    t.kind = .list;
    return .nil;
}

fn reverse(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const t = try bi.listArg(run, pos, .reverse, args[0]);
    try run.charge(pos, t.list.items.len);
    run.tally(t.list.items.len);
    std.mem.reverse(Value, t.list.items);
    return .nil;
}

/// **`take(l, n)`**: a new list of `l`'s first `n` items, or all of a
/// shorter one (QUEUE 220: the top N of a sorted list).
fn take(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const t = try bi.listArg(run, pos, .take, args[0]);
    const n = try bi.intArg(run, pos, .take, args[1], "its count");
    if (n < 0) return run.fail(pos, .argument, "`take`: a count of {d}", .{n});
    const k: usize = @intCast(@min(@as(u64, @intCast(n)), t.list.items.len));
    try run.charge(pos, k);
    const out = try run.newTable(pos);
    out.list.appendSlice(run.alloc, t.list.items[0..k]) catch return run.oom(pos);
    out.kind = .list;
    run.tally(k);
    return .{ .table = out };
}

/// **`last(l, n)`** (the design, final; metal-vmm QUEUE 232): the last
/// `n` items of a list, in order, all of them when it is shorter. A count
/// below 0 is an error, as `take`'s is: a count gone negative never quietly
/// becomes a tail.
fn last(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const t = try bi.listArg(run, pos, .last, args[0]);
    const n = try bi.intArg(run, pos, .last, args[1], "its count");
    if (n < 0) return run.fail(pos, .argument, "`last`: a count of {d}", .{n});
    const k: usize = @intCast(@min(@as(u64, @intCast(n)), t.list.items.len));
    try run.charge(pos, k);
    const out = try run.newTable(pos);
    out.list.appendSlice(run.alloc, t.list.items[t.list.items.len - k ..]) catch return run.oom(pos);
    out.kind = .list;
    run.tally(k);
    return .{ .table = out };
}

/// **`keys(m)`**: a map's keys, in the order they were first set.
fn keys(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const t = try bi.mapArg(run, pos, .keys, args[0]);
    const out = try run.newTable(pos);
    try run.charge(pos, t.map.count());
    out.list.ensureTotalCapacity(run.alloc, t.map.count()) catch return run.oom(pos);
    for (t.map.keys()) |k| out.list.appendAssumeCapacity(.{ .string = k });
    run.tally(t.map.count());
    out.kind = .list;
    return .{ .table = out };
}

fn hasKey(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const t = try bi.mapArg(run, pos, .has_key, args[0]);
    const k = try bi.stringArg(run, pos, .has_key, args[1], "its key");
    try run.charge(pos, k.len);
    run.tally(k.len);
    return .{ .boolean = t.map.contains(k) };
}

/// **`get(t, k[, default])`** (QUEUE 229, chunk 3): a map's value at the
/// string `k`, or a list's item at the integer `k`, or `default` (nil
/// unset) where there is none: `t[k]` that never stops the run for a
/// missing key or an index out of range. A key of the wrong kind for the
/// table is still the error indexing gives.
fn get(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const fallback: Value = if (args.len > 2) args[2] else .nil;
    const t = switch (args[0]) {
        .table => |t| t,
        else => return run.fail(pos, .type, "`get` takes a map or a list, not {s}", .{args[0].typeName()}),
    };
    switch (args[1]) {
        .int => |k| {
            if (t.kind == .map) return run.fail(pos, .type, "`get`: a map's keys are strings: this one is a map, given an integer", .{});
            if (k < 1 or k > t.list.items.len) return fallback;
            return t.list.items[@intCast(k - 1)];
        },
        .string => |k| {
            if (t.kind == .list) return run.fail(pos, .type, "`get`: a list's keys are integers: this one is a list, given a string", .{});
            try run.charge(pos, k.len);
            run.tally(k.len);
            return t.map.get(k) orelse fallback;
        },
        else => return run.fail(pos, .type, "`get`: a key must be an integer (a list) or a string (a map), not {s}", .{args[1].typeName()}),
    }
}

/// **`count(t)`**: a map's entries, or a list's length (`#l` too).
fn count(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const t = switch (args[0]) {
        .table => |t| t,
        else => return run.fail(pos, .type, "`count` takes a map or a list, not {s}", .{args[0].typeName()}),
    };
    return .{ .int = @intCast(if (t.kind == .list) t.list.items.len else t.map.count()) };
}

/// `"asc"` or `"desc"`, as `sort` takes an order.
fn orderArg(run: *Run, pos: ast.Pos, v: Value) Error!bool {
    const order = try bi.stringArg(run, pos, .sort, v, "its order");
    if (std.mem.eql(u8, order, "desc")) return true;
    if (std.mem.eql(u8, order, "asc")) return false;
    return run.fail(pos, .argument, "`sort`'s order is \"asc\" or \"desc\", not \"{s}\"", .{order});
}

/// **`sort(l[, order])`**: in place, all integers or all strings, and `l`
/// given back, so a sort is an expression (`join(sort(keys(m)), ",")`).
/// **`sort(rows, keys[, order])`**: a list of maps, by one key or several
/// (`sortByKeys`). `order` is `"asc"` (the default) or `"desc"`.
fn sort(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const t = try bi.listArg(run, pos, .sort, args[0]);
    const items = t.list.items;
    // An empty list sorts to itself, whether its second argument is a key
    // or an order: neither can be told from no items.
    if (items.len == 0) return args[0];
    const rows = items[0] == .table;
    if (args.len > 1 and args[1] != .nil and (rows or args[1] == .table)) {
        const desc = if (args.len > 2) try orderArg(run, pos, args[2]) else false;
        try sortByKeys(run, pos, t, args[1], desc);
        return args[0];
    }
    // A list of scalars: its second argument is its order (a key is a map's).
    if (args.len > 2) return run.fail(pos, .argument, "`sort` of integers or strings takes one order, `sort(l, \"desc\")`; a key is a list of maps'", .{});
    const desc = if (args.len > 1 and args[1] != .nil) blk: {
        if (args[1] == .string and !std.mem.eql(u8, args[1].string, "asc") and !std.mem.eql(u8, args[1].string, "desc"))
            return run.fail(pos, .argument, "`sort` of integers or strings takes an order, \"asc\" or \"desc\"; \"{s}\" would be a key, which sorts a list of maps", .{args[1].string});
        break :blk try orderArg(run, pos, args[1]);
    } else false;
    const kind = std.meta.activeTag(items[0]);
    for (items, 0..) |item, i| {
        if (std.meta.activeTag(item) != kind or (kind != .int and kind != .string))
            return run.fail(pos, .type, "`sort` takes all integers or all strings, and item {d} is {s}", .{ i + 1, item.typeName() });
    }
    // **CHARGED BEFORE IT RUNS**, its bound: a step a comparison and a
    // step a byte, every byte compared at most once a pass.
    var bytes: u64 = 0;
    if (kind == .string) for (items) |item| {
        bytes += item.string.len;
    };
    try run.charge(pos, bi.sortBound(items.len, bytes));
    var ctx: bi.SortContext = .{};
    if (desc) std.sort.pdq(Value, items, &ctx, struct {
        fn gt(c: *bi.SortContext, x: Value, y: Value) bool {
            return c.lessThan(y, x);
        }
    }.gt) else std.sort.pdq(Value, items, &ctx, bi.SortContext.lessThan);
    run.tally(ctx.comparisons + ctx.bytes);
    return args[0];
}

/// One key of a sort by keys, and its own order.
const SortKey = struct { key: []const u8, desc: bool };

/// The most keys one sort takes.
const sort_keys_most = 8;

/// **`sort(rows, keys)`**: a list of maps, in place, by `keys`: one key's
/// name, or a list of them, the first deciding first, each a name (in
/// `order`) or `{key = name, desc = true}` (its own). Each key's values are
/// all integers or all strings. **STABLE**: rows equal in every key keep
/// their order, descending too (where a sort and a `reverse` reversed them).
fn sortByKeys(run: *Run, pos: ast.Pos, t: *value.Table, keys_arg: Value, desc: bool) Error!void {
    var keys_buf: [sort_keys_most]SortKey = undefined;
    var n_keys: usize = 0;
    switch (keys_arg) {
        .string => |s| {
            keys_buf[0] = .{ .key = s, .desc = desc };
            n_keys = 1;
        },
        .table => |kt| {
            if (kt.kind != .list or kt.list.items.len == 0) return run.fail(pos, .argument, "`sort`'s keys are a key's name or a list of them, not an empty list or a map", .{});
            if (kt.list.items.len > sort_keys_most) return run.fail(pos, .argument, "`sort` takes at most {d} keys, given {d}", .{ sort_keys_most, kt.list.items.len });
            for (kt.list.items, 0..) |k, i| {
                keys_buf[i] = switch (k) {
                    .string => |s| .{ .key = s, .desc = desc },
                    .table => |km| blk: {
                        if (km.kind != .map) return run.fail(pos, .argument, "`sort`'s key {d} is a list: a key is a name or {{key = name, desc = true}}", .{i + 1});
                        const name = km.map.get("key") orelse return run.fail(pos, .argument, "`sort`'s key {d} has no `key`: {{key = name, desc = true}}", .{i + 1});
                        if (name != .string) return run.fail(pos, .type, "`sort`'s key {d}: its `key` is a name, not {s}", .{ i + 1, name.typeName() });
                        var own = desc;
                        var it = km.map.iterator();
                        while (it.next()) |e| {
                            if (std.mem.eql(u8, e.key_ptr.*, "key")) continue;
                            if (!std.mem.eql(u8, e.key_ptr.*, "desc")) return run.fail(pos, .argument, "`sort`'s key {d}: `{s}` is not one of its fields (`key`, `desc`)", .{ i + 1, e.key_ptr.* });
                            if (e.value_ptr.* != .boolean) return run.fail(pos, .type, "`sort`'s key {d}: `desc` is true or false, not {s}", .{ i + 1, e.value_ptr.typeName() });
                            own = e.value_ptr.boolean;
                        }
                        break :blk .{ .key = name.string, .desc = own };
                    },
                    else => return run.fail(pos, .type, "`sort`'s key {d} is {s}: a key is a name or {{key = name, desc = true}}", .{ i + 1, k.typeName() }),
                };
            }
            n_keys = kt.list.items.len;
        },
        else => return run.fail(pos, .type, "`sort`'s keys are a key's name or a list of them, not {s}", .{keys_arg.typeName()}),
    }
    const sort_keys = keys_buf[0..n_keys];
    const items = t.list.items;
    if (items.len == 0) return;
    // Each row's keys are hashed twice: once checked here, once taken below.
    var key_bytes: u64 = 0;
    for (sort_keys) |k| key_bytes += k.key.len;
    try run.charge(pos, 2 * @as(u64, items.len) * key_bytes);
    var kinds: [sort_keys_most]?std.meta.Tag(Value) = @splat(null);
    var bytes: u64 = 0;
    for (items, 0..) |item, i| {
        const m = switch (item) {
            .table => |m| if (m.kind == .map) m else return run.fail(pos, .type, "`sort` by a key takes a list of maps, and item {d} is {s}", .{ i + 1, item.typeName() }),
            else => return run.fail(pos, .type, "`sort` by a key takes a list of maps, and item {d} is {s}", .{ i + 1, item.typeName() }),
        };
        for (sort_keys, 0..) |k, j| {
            const v = m.map.get(k.key) orelse return run.fail(pos, .index, "`sort`: item {d} has no \"{s}\"", .{ i + 1, k.key });
            const kind = std.meta.activeTag(v);
            if ((kind != .int and kind != .string) or (kinds[j] != null and kinds[j].? != kind))
                return run.fail(pos, .type, "`sort` by \"{s}\" takes all integers or all strings, and item {d}'s is {s}", .{ k.key, i + 1, v.typeName() });
            kinds[j] = kind;
            if (kind == .string) bytes += v.string.len;
        }
    }
    // Two comparisons a key at most, for each of the bound's comparisons.
    try run.charge(pos, 2 * @as(u64, sort_keys.len) * bi.sortBound(items.len, bytes));
    // **EACH ROW'S KEYS LOOKED UP ONCE** (209's cold review), not twice a
    // comparison: the rows' keys are sorted beside them, then written back.
    const Row = struct { sort_keys: []Value, row: Value };
    const rows = run.alloc.alloc(Row, items.len) catch return run.oom(pos);
    for (items, rows) |item, *r| {
        r.sort_keys = run.alloc.alloc(Value, sort_keys.len) catch return run.oom(pos);
        for (sort_keys, r.sort_keys) |k, *v| v.* = item.table.map.get(k.key).?;
        r.row = item;
    }
    const Ctx = struct {
        c: bi.SortContext = .{},
        sort_keys: []const SortKey,
        fn lt(x: *@This(), a: Row, b: Row) bool {
            for (x.sort_keys, a.sort_keys, b.sort_keys) |k, av, bv| {
                if (x.c.lessThan(av, bv)) return !k.desc;
                if (x.c.lessThan(bv, av)) return k.desc;
            }
            return false;
        }
    };
    var ctx: Ctx = .{ .sort_keys = sort_keys };
    std.sort.block(Row, rows, &ctx, Ctx.lt);
    for (items, rows) |*item, r| item.* = r.row;
    run.tally(ctx.c.comparisons + ctx.c.bytes + 2 * items.len * key_bytes);
}
