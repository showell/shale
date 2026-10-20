//! The formatting builtins: human-readable sizes, padding, and a table of rows
//! as aligned text. Pure work on values; each charges a step per byte it
//! writes, and `table` a step per cell before it builds the grid.

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
    spec(.human, 1, 1, human),
    spec(.pad, 2, 2, pad),
    spec(.table, 2, 2, table),
};

/// `human(bytes)` formats a size as `512 B`, `1.5 KiB`, `3.2 MiB`, up to EiB,
/// with one decimal rounded half up, computed in integers. A negative size
/// keeps its sign.
fn human(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const arg = args[0];
    const n = try bi.intArg(run, pos, .human, arg, "its size");
    try run.charge(pos, 16);
    const neg = n < 0;
    const mag: u128 = @abs(@as(i128, n));
    const units = [_][]const u8{ "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
    const sign: []const u8 = if (neg) "-" else "";
    if (mag < 1024) return .{ .string = std.fmt.allocPrint(run.alloc, "{s}{d} B", .{ sign, mag }) catch return run.oom(pos) };
    var u: usize = 1;
    while (u + 1 < units.len and mag >= (@as(u128, 1) << @intCast(10 * (u + 1)))) u += 1;
    var unit: u128 = @as(u128, 1) << @intCast(10 * u);
    var tenths = (mag * 10 + unit / 2) / unit;
    // 1023.95 KiB rounds to 1024.0; carry to 1.0 MiB.
    if (tenths >= 10240 and u + 1 < units.len) {
        u += 1;
        unit <<= 10;
        tenths = (mag * 10 + unit / 2) / unit;
    }
    return .{ .string = std.fmt.allocPrint(run.alloc, "{s}{d}.{d} {s}", .{ sign, tenths / 10, tenths % 10, units[u] }) catch return run.oom(pos) };
}

/// Returns a scalar's text, as `print` writes it.
fn cellText(run: *Run, pos: ast.Pos, v: Value, what: []const u8) Error![]const u8 {
    var buf: [24]u8 = undefined;
    const t = try bi.scalarText(run, pos, v, &buf, what);
    return run.alloc.dupe(u8, t) catch return run.oom(pos);
}

/// Pads `text` with spaces to `width`: on the right (left-aligned), or, for a
/// negative width, on the left (right-aligned). Longer text is kept whole.
fn padded(run: *Run, pos: ast.Pos, text: []const u8, width: i64) Error![]const u8 {
    const w: usize = @intCast(@min(@abs(width), std.math.maxInt(u32)));
    if (text.len >= w) return text;
    try run.charge(pos, w);
    run.tally(w);
    const out = run.alloc.alloc(u8, w) catch return run.oom(pos);
    const gap = w - text.len;
    if (width < 0) {
        @memset(out[0..gap], ' ');
        @memcpy(out[gap..], text);
    } else {
        @memcpy(out[0..text.len], text);
        @memset(out[text.len..], ' ');
    }
    return out;
}

/// `pad(v, width)` returns a string or integer as text, padded with spaces to
/// `width` on the right, or to `-width` on the left (for numbers).
fn pad(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const v = args[0];
    const w_arg = args[1];
    const width = try bi.intArg(run, pos, .pad, w_arg, "its width");
    const text = switch (v) {
        .string => |s| s,
        .int => try cellText(run, pos, v, "pad"),
        else => return run.fail(pos, .type, "`pad` takes a string or an integer, not {s}", .{v.typeName()}),
    };
    return .{ .string = try padded(run, pos, text, width) };
}

/// `table(rows, columns)` formats rows (maps) as aligned text: a line of column
/// names, then one per row, columns two spaces apart, with no trailing spaces
/// and no final newline (`print` adds it). A column of integers, and its name,
/// is right-aligned. Every row must have every column, as reading a missing
/// key is an error; each cell is a scalar.
fn table(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const rows_arg = args[0];
    const cols_arg = args[1];
    const rows = try bi.listArg(run, pos, .table, rows_arg);
    const cols = try bi.listArg(run, pos, .table, cols_arg);
    const a = run.alloc;
    const nc = cols.list.items.len;
    const names = a.alloc([]const u8, nc) catch return run.oom(pos);
    for (cols.list.items, names, 0..) |c, *n, i| n.* = switch (c) {
        .string => |s| s,
        else => return run.fail(pos, .type, "`table`: column {d} must be a string, not {s}", .{ i + 1, c.typeName() }),
    };
    const nr = rows.list.items.len;
    // Every cell costs a step, charged before the grid is made: an empty
    // cell's text is 0 bytes, so charging only text would let a few thousand
    // rows and columns buy millions of uncharged lookups and the grid's
    // allocation.
    try run.charge(pos, nr * nc);
    const cells = a.alloc([]const u8, nr * nc) catch return run.oom(pos);
    const widths = a.alloc(usize, nc) catch return run.oom(pos);
    const numeric = a.alloc(bool, nc) catch return run.oom(pos);
    for (names, widths, numeric) |n, *w, *num| {
        w.* = n.len;
        num.* = nr > 0;
    }
    for (rows.list.items, 0..) |row, r| {
        const m = switch (row) {
            .table => |t| if (t.kind == .map) t else return run.fail(pos, .type, "`table`: row {d} must be a map, not {s}", .{ r + 1, row.typeName() }),
            else => return run.fail(pos, .type, "`table`: row {d} must be a map, not {s}", .{ r + 1, row.typeName() }),
        };
        for (names, 0..) |n, c| {
            try run.charge(pos, n.len);
            const v = m.map.get(n) orelse return run.fail(pos, .index, "`table`: row {d} has no \"{s}\"", .{ r + 1, n });
            if (v != .int) numeric[c] = false;
            const t = try cellText(run, pos, v, "table");
            try run.charge(pos, t.len);
            run.tally(1 + n.len + t.len);
            cells[r * nc + c] = t;
            widths[c] = @max(widths[c], t.len);
        }
    }
    var out: std.ArrayList(u8) = .empty;
    for (0..nr + 1) |r| {
        if (r > 0) out.append(a, '\n') catch return run.oom(pos);
        const start = out.items.len;
        for (0..nc) |c| {
            if (c > 0) out.appendSlice(a, "  ") catch return run.oom(pos);
            const t = if (r == 0) names[c] else cells[(r - 1) * nc + c];
            const w: i64 = @intCast(widths[c]);
            out.appendSlice(a, try padded(run, pos, t, if (numeric[c]) -w else w)) catch return run.oom(pos);
        }
        while (out.items.len > start and out.items[out.items.len - 1] == ' ') out.items.len -= 1;
    }
    return .{ .string = out.items };
}
