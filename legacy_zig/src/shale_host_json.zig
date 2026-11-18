//! JSON for the Linux host (metal-vmm QUEUE 304): `json_read` turns a JSON
//! text into Shale's values, and `json_write` turns them back. The prelude's
//! `json_read(s, opts?)` and `json_write(v)` call these.
//!
//! **SHALE'S VALUES ARE NOT JSON'S**, and where they differ the reader says
//! so rather than guess:
//! - an object is a map and an array a list; `{}` and `[]` are both Shale's
//!   empty table;
//! - an integer is an integer, and one past 64 bits is refused; a number
//!   with a fraction or an exponent is kept as its text, a string, since
//!   Shale has no reals;
//! - `null` cannot be in a list or a map (Shale's lists and maps never hold
//!   nil): a member that is null is left out of its object, and a null in an
//!   array is refused, unless `{null = v}` names a value to stand for it;
//! - a key twice in one object is refused: which one would be meant?
//!
//! A malformed text is refused with what was wrong, its byte offset and its
//! line and column. The depth is bounded (`depth`, 64 unless given) so a
//! text cannot nest past the Zig stack, and the size by `size` (the call
//! bytes cap unless given smaller). A step is charged for every 16 bytes
//! read, and one for each value made.

const std = @import("std");
const shale = @import("shale");
const script = shale.script;

const Run = script.interp.Run;
const Value = script.value.Value;
const Error = script.interp.Error;
const Pos = script.ast.Pos;

/// The most nesting a text may have unless `depth` says less; `depth` may
/// not say more.
const depth_default = 64;
const depth_most = 512;

const Reader = struct {
    run: *Run,
    pos: Pos,
    s: []const u8,
    i: usize = 0,
    depth_left: usize,
    null_as: ?Value,

    fn failAt(r: *Reader, at: usize, comptime what: []const u8, args: anytype) Error {
        var line: usize = 1;
        var col: usize = 1;
        for (r.s[0..@min(at, r.s.len)]) |c| {
            if (c == '\n') {
                line += 1;
                col = 1;
            } else col += 1;
        }
        const said = std.fmt.allocPrint(r.run.alloc, what, args) catch return r.run.oom(r.pos);
        return r.run.fail(r.pos, .argument, "`json_read`: {s} at byte {d} (line {d}, column {d})", .{ said, at, line, col });
    }

    fn skipSpace(r: *Reader) void {
        while (r.i < r.s.len and (r.s[r.i] == ' ' or r.s[r.i] == '\t' or r.s[r.i] == '\n' or r.s[r.i] == '\r')) r.i += 1;
    }

    fn shown(c: u8) []const u8 {
        return switch (c) {
            '{' => "`{`",
            '}' => "`}`",
            '[' => "`[`",
            ']' => "`]`",
            ',' => "a comma",
            ':' => "a colon",
            '"' => "a quote",
            else => "a byte",
        };
    }

    /// One value, `null` given back as nil for the caller to place.
    fn value(r: *Reader) Error!Value {
        r.skipSpace();
        if (r.i >= r.s.len) return r.failAt(r.i, "a value was expected and the text ended", .{});
        try r.run.charge(r.pos, 1);
        const c = r.s[r.i];
        return switch (c) {
            '{' => r.object(),
            '[' => r.array(),
            '"' => .{ .string = try r.string() },
            't' => r.word("true", .{ .boolean = true }),
            'f' => r.word("false", .{ .boolean = false }),
            'n' => r.word("null", .nil),
            '-', '0'...'9' => r.number(),
            else => r.failAt(r.i, "a value was expected, not {s} (0x{x:0>2})", .{ shown(c), c }),
        };
    }

    fn word(r: *Reader, w: []const u8, v: Value) Error!Value {
        if (!std.mem.startsWith(u8, r.s[r.i..], w)) return r.failAt(r.i, "`{s}` was expected", .{w});
        r.i += w.len;
        return v;
    }

    fn enter(r: *Reader) Error!void {
        if (r.depth_left == 0) return r.failAt(r.i, "the text nests past its depth budget", .{});
        r.depth_left -= 1;
    }

    fn object(r: *Reader) Error!Value {
        try r.enter();
        defer r.depth_left += 1;
        r.i += 1;
        const t = try r.run.newTable(r.pos);
        r.skipSpace();
        if (r.i < r.s.len and r.s[r.i] == '}') {
            r.i += 1;
            return .{ .table = t };
        }
        while (true) {
            r.skipSpace();
            if (r.i >= r.s.len or r.s[r.i] != '"') return r.failAt(r.i, "a member's name, a string, was expected", .{});
            const key_at = r.i;
            const key = try r.string();
            r.skipSpace();
            if (r.i >= r.s.len or r.s[r.i] != ':') return r.failAt(r.i, "a colon after a member's name was expected", .{});
            r.i += 1;
            var v = try r.value();
            if (t.map.contains(key)) return r.failAt(key_at, "the key \"{s}\" is in this object twice", .{key});
            if (v == .nil) if (r.null_as) |n| {
                v = n;
            };
            if (v != .nil) {
                t.map.put(r.run.alloc, key, v) catch return r.run.oom(r.pos);
                t.kind = .map;
            }
            r.skipSpace();
            if (r.i >= r.s.len) return r.failAt(r.i, "the text ended inside an object", .{});
            if (r.s[r.i] == ',') {
                r.i += 1;
                continue;
            }
            if (r.s[r.i] == '}') {
                r.i += 1;
                return .{ .table = t };
            }
            return r.failAt(r.i, "a comma or `}}` was expected, not {s}", .{shown(r.s[r.i])});
        }
    }

    fn array(r: *Reader) Error!Value {
        try r.enter();
        defer r.depth_left += 1;
        r.i += 1;
        const t = try r.run.newTable(r.pos);
        r.skipSpace();
        if (r.i < r.s.len and r.s[r.i] == ']') {
            r.i += 1;
            return .{ .table = t };
        }
        while (true) {
            r.skipSpace();
            const at = r.i;
            var v = try r.value();
            if (v == .nil) v = r.null_as orelse return r.failAt(at, "a null in an array, which a Shale list cannot hold: `{{null = v}}` names a value to stand for it", .{});
            t.list.append(r.run.alloc, v) catch return r.run.oom(r.pos);
            t.kind = .list;
            r.skipSpace();
            if (r.i >= r.s.len) return r.failAt(r.i, "the text ended inside an array", .{});
            if (r.s[r.i] == ',') {
                r.i += 1;
                continue;
            }
            if (r.s[r.i] == ']') {
                r.i += 1;
                return .{ .table = t };
            }
            return r.failAt(r.i, "a comma or `]` was expected, not {s}", .{shown(r.s[r.i])});
        }
    }

    /// A string at `r.i` (its opening quote), its escapes decoded to UTF-8.
    fn string(r: *Reader) Error![]const u8 {
        const open = r.i;
        r.i += 1;
        var out: std.ArrayList(u8) = .empty;
        const a = r.run.alloc;
        while (true) {
            if (r.i >= r.s.len) return r.failAt(open, "a string that never closes", .{});
            const c = r.s[r.i];
            if (c == '"') {
                r.i += 1;
                if (!std.unicode.utf8ValidateSlice(out.items)) return r.failAt(open, "a string that is not UTF-8", .{});
                return out.items;
            }
            if (c < 0x20) return r.failAt(r.i, "a control byte (0x{x:0>2}) inside a string, which JSON writes as an escape", .{c});
            if (c != '\\') {
                out.append(a, c) catch return r.run.oom(r.pos);
                r.i += 1;
                continue;
            }
            if (r.i + 1 >= r.s.len) return r.failAt(r.i, "a string that ends in a backslash", .{});
            const e = r.s[r.i + 1];
            r.i += 2;
            const plain: ?u8 = switch (e) {
                '"' => '"',
                '\\' => '\\',
                '/' => '/',
                'b' => 8,
                'f' => 12,
                'n' => '\n',
                'r' => '\r',
                't' => '\t',
                'u' => null,
                else => return r.failAt(r.i - 2, "the escape `\\{c}`, which JSON has not", .{e}),
            };
            if (plain) |p| {
                out.append(a, p) catch return r.run.oom(r.pos);
                continue;
            }
            var cp: u21 = try r.hex4(r.i - 2);
            if (cp >= 0xD800 and cp <= 0xDBFF) {
                // A high surrogate takes the low one after it.
                if (!std.mem.startsWith(u8, r.s[r.i..], "\\u")) return r.failAt(r.i - 6, "a high surrogate with no low one after it", .{});
                r.i += 2;
                const lo = try r.hex4(r.i - 2);
                if (lo < 0xDC00 or lo > 0xDFFF) return r.failAt(r.i - 6, "a high surrogate followed by no low one", .{});
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 and cp <= 0xDFFF) return r.failAt(r.i - 6, "a low surrogate with no high one before it", .{});
            var buf: [4]u8 = undefined;
            const n = std.unicode.utf8Encode(cp, &buf) catch unreachable;
            out.appendSlice(a, buf[0..n]) catch return r.run.oom(r.pos);
        }
    }

    /// The four hex digits at `r.i`, after `\u` at `at`.
    fn hex4(r: *Reader, at: usize) Error!u21 {
        if (r.i + 4 > r.s.len) return r.failAt(at, "`\\u` needs four hex digits", .{});
        const v = std.fmt.parseInt(u16, r.s[r.i .. r.i + 4], 16) catch return r.failAt(at, "`\\u` needs four hex digits", .{});
        r.i += 4;
        return v;
    }

    fn number(r: *Reader) Error!Value {
        const start = r.i;
        if (r.s[r.i] == '-') r.i += 1;
        if (r.i >= r.s.len or !std.ascii.isDigit(r.s[r.i])) return r.failAt(start, "a number needs a digit after its sign", .{});
        if (r.s[r.i] == '0' and r.i + 1 < r.s.len and std.ascii.isDigit(r.s[r.i + 1])) return r.failAt(start, "a number with a leading zero", .{});
        while (r.i < r.s.len and std.ascii.isDigit(r.s[r.i])) r.i += 1;
        var whole = true;
        if (r.i < r.s.len and r.s[r.i] == '.') {
            whole = false;
            r.i += 1;
            if (r.i >= r.s.len or !std.ascii.isDigit(r.s[r.i])) return r.failAt(start, "a number needs a digit after its point", .{});
            while (r.i < r.s.len and std.ascii.isDigit(r.s[r.i])) r.i += 1;
        }
        if (r.i < r.s.len and (r.s[r.i] == 'e' or r.s[r.i] == 'E')) {
            whole = false;
            r.i += 1;
            if (r.i < r.s.len and (r.s[r.i] == '+' or r.s[r.i] == '-')) r.i += 1;
            if (r.i >= r.s.len or !std.ascii.isDigit(r.s[r.i])) return r.failAt(start, "a number needs a digit in its exponent", .{});
            while (r.i < r.s.len and std.ascii.isDigit(r.s[r.i])) r.i += 1;
        }
        const text = r.s[start..r.i];
        if (!whole) return .{ .string = r.run.alloc.dupe(u8, text) catch return r.run.oom(r.pos) };
        const n = std.fmt.parseInt(i64, text, 10) catch return r.failAt(start, "the integer {s} is past 64 bits", .{text});
        return .{ .int = n };
    }
};

/// `host("json_read", s, opts?)`.
pub fn read(run: *Run, pos: Pos, args: []const Value) Error!Value {
    if (args.len < 1 or args.len > 2) return run.fail(pos, .argument, "`json_read` takes a text and options, given {d} arguments", .{args.len});
    const s = switch (args[0]) {
        .string => |x| x,
        else => |v| return run.fail(pos, .type, "`json_read` takes a text, not {s}", .{v.typeName()}),
    };
    var depth: usize = depth_default;
    var size: usize = run.budgets.call_bytes;
    var null_as: ?Value = null;
    if (args.len == 2) switch (args[1]) {
        .table => |t| if (t.kind != .list) {
            for (t.map.keys(), t.map.values()) |k, x| {
                if (std.mem.eql(u8, k, "depth") or std.mem.eql(u8, k, "size")) {
                    const n = switch (x) {
                        .int => |n| n,
                        else => return run.fail(pos, .type, "`json_read`'s `{s}` is an integer, not {s}", .{ k, x.typeName() }),
                    };
                    if (std.mem.eql(u8, k, "depth")) {
                        if (n < 1 or n > depth_most) return run.fail(pos, .argument, "`json_read`'s `depth` is 1 to {d}, not {d}", .{ depth_most, n });
                        depth = @intCast(n);
                    } else {
                        if (n < 0 or n > run.budgets.call_bytes) return run.fail(pos, .argument, "`json_read`'s `size` is 0 to the call bytes cap, {d}, not {d}", .{ run.budgets.call_bytes, n });
                        size = @intCast(n);
                    }
                } else if (std.mem.eql(u8, k, "null")) {
                    null_as = x;
                } else return run.fail(pos, .argument, "`json_read` takes `depth`, `size` and `null`, not `{s}`", .{k});
            }
        } else return run.fail(pos, .type, "`json_read`'s options are a map, not a list", .{}),
        else => |v| return run.fail(pos, .type, "`json_read`'s options are a map, not {s}", .{v.typeName()}),
    };
    if (s.len > size) return run.fail(pos, .argument, "`json_read`: the text is {d} bytes, past its size budget of {d}", .{ s.len, size });
    try script.builtins.chargeBytes(run, pos, s.len);
    var r: Reader = .{ .run = run, .pos = pos, .s = s, .depth_left = depth, .null_as = null_as };
    const v = try r.value();
    r.skipSpace();
    if (r.i < s.len) return r.failAt(r.i, "more after the value: one text is one value", .{});
    return v;
}

/// `host("json_write", v)`: `v` as JSON, compact. A map's members in its
/// keys' order, an empty table as `[]`, nil as `null`. A string must be
/// UTF-8, and nesting past the depth `json_read` allows is refused, which is
/// also what stops a table that holds itself.
pub fn write(run: *Run, pos: Pos, args: []const Value) Error!Value {
    if (args.len != 1) return run.fail(pos, .argument, "`json_write` takes one value, given {d}", .{args.len});
    var out: std.ArrayList(u8) = .empty;
    try emit(run, pos, &out, args[0], depth_most);
    try script.builtins.chargeBytes(run, pos, out.items.len);
    return .{ .string = out.items };
}

fn emit(run: *Run, pos: Pos, out: *std.ArrayList(u8), v: Value, depth_left: usize) Error!void {
    const a = run.alloc;
    try run.charge(pos, 1);
    switch (v) {
        .nil => out.appendSlice(a, "null") catch return run.oom(pos),
        .boolean => |b| out.appendSlice(a, if (b) "true" else "false") catch return run.oom(pos),
        .int => |n| out.print(a, "{d}", .{n}) catch return run.oom(pos),
        .string => |s| try emitString(run, pos, out, s),
        .table => |t| {
            if (depth_left == 0) return run.fail(pos, .argument, "`json_write`: the value nests past {d} deep (or holds itself)", .{depth_most});
            switch (t.kind) {
                .empty => out.appendSlice(a, "[]") catch return run.oom(pos),
                .list => {
                    out.append(a, '[') catch return run.oom(pos);
                    for (t.list.items, 0..) |x, k| {
                        if (k > 0) out.append(a, ',') catch return run.oom(pos);
                        try emit(run, pos, out, x, depth_left - 1);
                    }
                    out.append(a, ']') catch return run.oom(pos);
                },
                .map => {
                    out.append(a, '{') catch return run.oom(pos);
                    for (t.map.keys(), t.map.values(), 0..) |k, x, n| {
                        if (n > 0) out.append(a, ',') catch return run.oom(pos);
                        try emitString(run, pos, out, k);
                        out.append(a, ':') catch return run.oom(pos);
                        try emit(run, pos, out, x, depth_left - 1);
                    }
                    out.append(a, '}') catch return run.oom(pos);
                },
            }
        },
    }
}

fn emitString(run: *Run, pos: Pos, out: *std.ArrayList(u8), s: []const u8) Error!void {
    const a = run.alloc;
    if (!std.unicode.utf8ValidateSlice(s)) return run.fail(pos, .argument, "`json_write`: a string that is not UTF-8, which JSON cannot hold", .{});
    out.append(a, '"') catch return run.oom(pos);
    for (s) |c| switch (c) {
        '"' => out.appendSlice(a, "\\\"") catch return run.oom(pos),
        '\\' => out.appendSlice(a, "\\\\") catch return run.oom(pos),
        '\n' => out.appendSlice(a, "\\n") catch return run.oom(pos),
        '\r' => out.appendSlice(a, "\\r") catch return run.oom(pos),
        '\t' => out.appendSlice(a, "\\t") catch return run.oom(pos),
        0...8, 11, 12, 14...0x1f => out.print(a, "\\u{x:0>4}", .{c}) catch return run.oom(pos),
        else => out.append(a, c) catch return run.oom(pos),
    };
    out.append(a, '"') catch return run.oom(pos);
}
