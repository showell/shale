//! The text builtins: splitting and joining, searching, slicing and replacing,
//! `like` and `match_path` globs, and parsing integers. Every search is for
//! plain text, not Lua's patterns. Each builtin charges a step per byte it
//! examines.

const std = @import("std");
const ast = @import("ast.zig");
const interp = @import("interp.zig");
const value = @import("value.zig");
const bi = @import("builtins.zig");

const Run = interp.Run;
const Value = value.Value;
const Error = interp.Error;
const B = ast.Builtin;
const spec = bi.spec;

pub const specs = [_]bi.Spec{
    spec(.split, 2, 2, split),
    spec(.join, 2, 2, join),
    spec(.lines, 1, 1, lines),
    spec(.words, 1, 1, words),
    spec(.starts_with, 2, 2, looks(.starts_with)),
    spec(.ends_with, 2, 2, looks(.ends_with)),
    spec(.contains, 2, 2, looks(.contains)),
    spec(.find, 2, 3, find),
    spec(.sub, 2, 3, sub),
    spec(.bad_utf8, 1, 1, badUtf8),
    spec(.trim, 1, 1, trimmed(.both)),
    spec(.trim_start, 1, 1, trimmed(.start)),
    spec(.trim_end, 1, 1, trimmed(.end)),
    spec(.upper, 1, 1, cased(.upper)),
    spec(.lower, 1, 1, cased(.lower)),
    spec(.like, 2, 2, like),
    spec(.match_path, 2, 2, matchPath),
    spec(.between, 3, 3, between),
    spec(.tostring, 1, 1, tostring),
    spec(.tonumber, 1, 1, tonumber),
    spec(.parse_int, 1, 1, parseInt),
    spec(.rep, 2, 2, rep),
    spec(.substitute, 3, 3, substitute),
    spec(.path, 1, bi.most_args, path),
    spec(.basename, 1, 1, basename),
    spec(.strip_suffix, 2, 2, stripSuffix),
};

/// `path(a, b, ...)` joins the parts with `/`, leaving out each "", so ""
/// denotes the root and needs no special case. A step per byte produced.
fn path(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    var total: u64 = 0;
    for (args, 0..) |a, i| {
        const part = switch (a) {
            .string => |x| x,
            else => return run.fail(pos, .type, "`path`'s part {d} is {s}, not a string", .{ i + 1, a.typeName() }),
        };
        if (part.len > 0) total += part.len + 1;
    }
    if (total > 0) total -= 1;
    try run.charge(pos, total);
    const out = run.alloc.alloc(u8, @intCast(total)) catch return run.oom(pos);
    var at: usize = 0;
    for (args) |a| {
        if (a.string.len == 0) continue;
        if (at > 0) {
            out[at] = '/';
            at += 1;
        }
        @memcpy(out[at..][0..a.string.len], a.string);
        at += a.string.len;
    }
    run.tally(total);
    return .{ .string = out };
}

/// `basename(p)` returns what follows the last `/`, or all of `p`.
fn basename(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const p = try bi.stringArg(run, pos, .basename, args[0], "its path");
    try run.charge(pos, p.len);
    run.tally(p.len);
    return .{ .string = if (std.mem.lastIndexOfScalar(u8, p, '/')) |i| p[i + 1 ..] else p };
}

/// `strip_suffix(s, t)` returns `s` without the suffix `t`, or `s` unchanged.
fn stripSuffix(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .strip_suffix, args[0], "its first argument");
    const t = try bi.stringArg(run, pos, .strip_suffix, args[1], "its suffix");
    try run.charge(pos, t.len);
    run.tally(t.len);
    return .{ .string = if (std.mem.endsWith(u8, str, t)) str[0 .. str.len - t.len] else str };
}

/// `rep(s, n)` returns `s` repeated `n` times. Charges a step per byte before
/// building the string.
fn rep(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .rep, args[0], "its first argument");
    const n = try bi.intArg(run, pos, .rep, args[1], "how many times");
    if (n < 0) return run.fail(pos, .argument, "`rep`: {d} times", .{n});
    const total = std.math.mul(u64, str.len, @intCast(n)) catch return run.budgetStop(pos, .arena);
    try run.charge(pos, total);
    const out = run.alloc.alloc(u8, @intCast(total)) catch return run.oom(pos);
    var at: usize = 0;
    while (at < out.len) : (at += str.len) @memcpy(out[at..][0..str.len], str);
    run.tally(total);
    return .{ .string = out };
}

/// `substitute(s, old, new)` replaces every non-overlapping `old` in `s`, left
/// to right, with `new`; plain text. A step per byte of `s` and of the result.
fn substitute(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .substitute, args[0], "its first argument");
    const old = try bi.stringArg(run, pos, .substitute, args[1], "what it replaces");
    const new = try bi.stringArg(run, pos, .substitute, args[2], "what it puts instead");
    if (old.len == 0) return run.fail(pos, .argument, "`substitute` of \"\" is an error: there is nothing to replace", .{});
    try bi.chargeBytes(run, pos, str.len);
    const finder = try Finder.init(run, pos, old);
    var n: usize = 0;
    var at: usize = 0;
    while (try finder.find(str, at)) |i| : (at = i + old.len) n += 1;
    if (n == 0) return .{ .string = str };
    const added = std.math.mul(usize, n, new.len) catch return run.budgetStop(pos, .arena);
    const total = str.len - n * old.len + added;
    try run.charge(pos, total);
    const out = run.alloc.alloc(u8, total) catch return run.oom(pos);
    var w: usize = 0;
    at = 0;
    while (try finder.find(str, at)) |i| : (at = i + old.len) {
        @memcpy(out[w..][0 .. i - at], str[at..i]);
        w += i - at;
        @memcpy(out[w..][0..new.len], new);
        w += new.len;
    }
    @memcpy(out[w..], str[at..]);
    run.tally(total);
    return .{ .string = out };
}

/// Searches for a needle, charging the work as it is done. The caller charges
/// the haystack, a step per 16 bytes, for the fast pass over the needle's
/// first byte. Each place that pass stops costs a step more, and the bytes
/// compared there a step per 16. So a dense haystack pays a step per match,
/// and a long near-match (`rep("a", 30000) .. "b"` in a run of `a`) pays for
/// every compare.
pub const Finder = struct {
    run: *Run,
    pos: ast.Pos,
    needle: []const u8,

    pub fn init(run: *Run, pos: ast.Pos, needle: []const u8) Error!Finder {
        return .{ .run = run, .pos = pos, .needle = needle };
    }

    /// The byte compares a search does between two charges.
    const batch_compares = 64 * 1024;

    /// Returns the first index of the needle in `hay` at or after `from`.
    pub fn find(f: *const Finder, hay: []const u8, from: usize) Error!?usize {
        const n = f.needle;
        // "" is found where the search starts, as Lua's `find` answers 1.
        if (n.len == 0) return if (from <= hay.len) from else null;
        var at = from;
        var stops: u64 = 0;
        var compared: u64 = 0;
        // A near-match search does haystack x needle compares, so charging
        // only at the end would let it finish before a budget could stop it.
        // We charge each batch of compares as whole steps when it is done, so
        // a budget stops the search within a batch; the total is unchanged.
        var paid_stops: u64 = 0;
        var paid_compared: u64 = 0;
        const found: ?usize = while (std.mem.indexOfScalarPos(u8, hay, at, n[0])) |i| {
            stops += 1;
            if (hay.len - i < n.len) break null;
            compared += n.len - 1;
            if (std.mem.eql(u8, hay[i + 1 ..][0 .. n.len - 1], n[1..])) break i;
            at = i + 1;
            if (compared - paid_compared >= batch_compares) {
                const whole = (compared - paid_compared) / bi.bytes_per_step * bi.bytes_per_step;
                f.run.tally(stops - paid_stops + whole);
                try f.run.charge(f.pos, stops - paid_stops + whole / bi.bytes_per_step);
                paid_stops = stops;
                paid_compared += whole;
            }
        } else null;
        f.run.tally((if (found) |i| i + n.len else hay.len) -| from + (stops - paid_stops) + (compared - paid_compared));
        try f.run.charge(f.pos, stops - paid_stops + (std.math.divCeil(u64, compared - paid_compared, bi.bytes_per_step) catch unreachable));
        return found;
    }
};

fn split(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .split, args[0], "its first argument");
    const sep = try bi.stringArg(run, pos, .split, args[1], "its separator");
    if (sep.len == 0) return run.fail(pos, .argument, "`split` by \"\" is an error: there is no empty separator", .{});
    try bi.chargeBytes(run, pos, str.len);
    const finder = try Finder.init(run, pos, sep);
    const out = try run.newTable(pos);
    var at: usize = 0;
    while (try finder.find(str, at)) |i| : (at = i + sep.len) {
        // Each piece is an item made: a step each.
        try run.charge(pos, 1);
        run.tally(1);
        out.list.append(run.alloc, .{ .string = str[at..i] }) catch return run.oom(pos);
    }
    out.list.append(run.alloc, .{ .string = str[at..] }) catch return run.oom(pos);
    out.kind = .list;
    return .{ .table = out };
}

fn join(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const t = try bi.listArg(run, pos, .join, args[0]);
    const sep = try bi.stringArg(run, pos, .join, args[1], "its separator");
    var buf: std.ArrayList(u8) = .empty;
    for (t.list.items, 0..) |item, i| {
        if (i > 0) buf.appendSlice(run.alloc, sep) catch return run.oom(pos);
        var nb: [24]u8 = undefined;
        const piece = switch (item) {
            .string => |x| x,
            .int => |n| std.fmt.bufPrint(&nb, "{d}", .{n}) catch unreachable,
            else => return run.fail(pos, .type, "`join` takes strings and integers, and item {d} is {s}", .{ i + 1, item.typeName() }),
        };
        try run.charge(pos, piece.len + sep.len);
        run.tally(piece.len + sep.len);
        buf.appendSlice(run.alloc, piece) catch return run.oom(pos);
    }
    return .{ .string = buf.items };
}

/// `lines(s)` splits `s` at each `\n`, with no empty last piece after a final
/// newline (`#lines("a\nb\n")` is 2).
fn lines(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .lines, args[0], "its first argument");
    try run.charge(pos, str.len);
    const out = try run.newTable(pos);
    out.kind = .list;
    var rest = str;
    while (rest.len > 0) {
        const nl = std.mem.indexOfScalar(u8, rest, '\n');
        const line = if (nl) |i| rest[0..i] else rest;
        out.list.append(run.alloc, .{ .string = line }) catch return run.oom(pos);
        run.tally(line.len + 1);
        rest = if (nl) |i| rest[i + 1 ..] else "";
    }
    return .{ .table = out };
}

/// `words(s)` returns the nonempty runs between ASCII whitespace.
fn words(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .words, args[0], "its first argument");
    try run.charge(pos, str.len);
    const out = try run.newTable(pos);
    out.kind = .list;
    var it = std.mem.tokenizeAny(u8, str, " \t\r\n\x0b\x0c");
    while (it.next()) |w| out.list.append(run.alloc, .{ .string = w }) catch return run.oom(pos);
    run.tally(str.len);
    return .{ .table = out };
}

const Look = enum { starts_with, ends_with, contains };

fn looks(comptime l: Look) bi.Impl {
    return struct {
        fn f(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
            const b: B = switch (l) {
                inline else => |t| @field(B, @tagName(t)),
            };
            const str = try bi.stringArg(run, pos, b, args[0], "its first argument");
            const sub_s = try bi.stringArg(run, pos, b, args[1], "what it looks for");
            try bi.chargeBytes(run, pos, str.len);
            return .{ .boolean = switch (l) {
                .starts_with => blk: {
                    run.tally(sub_s.len);
                    break :blk std.mem.startsWith(u8, str, sub_s);
                },
                .ends_with => blk: {
                    run.tally(sub_s.len);
                    break :blk std.mem.endsWith(u8, str, sub_s);
                },
                .contains => (try (try Finder.init(run, pos, sub_s)).find(str, 0)) != null,
            } };
        }
    }.f;
}

/// `find(s, t[, from])` returns the 1-based index of the first `t` in `s`, or
/// nil. Searches from `from` (1-based, `#s + 1` is the end), so the next one
/// is `find(s, t, at + 1)`; reads only what is past `from`.
fn find(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .find, args[0], "its first argument");
    const sub_s = try bi.stringArg(run, pos, .find, args[1], "what it looks for");
    const from: usize = if (args.len > 2) blk: {
        const f = try bi.intArg(run, pos, .find, args[2], "where it starts");
        if (f < 1 or f > @as(i64, @intCast(str.len)) + 1) return run.fail(pos, .argument, "`find` starts at 1 to {d} (`#s + 1`), not {d}", .{ str.len + 1, f });
        break :blk @intCast(f - 1);
    } else 0;
    try bi.chargeBytes(run, pos, str.len - from);
    const at = (try (try Finder.init(run, pos, sub_s)).find(str, from)) orelse return .nil;
    return .{ .int = @intCast(at + 1) };
}

/// `bad_utf8(s)` is nil when `s` is valid UTF-8, else the position (from 1,
/// as `find` counts) of the first byte that starts no valid sequence: a
/// stray continuation byte, a lead byte with too few continuations after it
/// (the text cut short counts), an overlong form, a surrogate half, or past
/// U+10FFFF. A step per 16 bytes, as a search.
fn badUtf8(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .bad_utf8, args[0], "its text");
    try bi.chargeBytes(run, pos, str.len);
    var i: usize = 0;
    while (i < str.len) {
        const n = std.unicode.utf8ByteSequenceLength(str[i]) catch return .{ .int = @intCast(i + 1) };
        if (n > str.len - i) return .{ .int = @intCast(i + 1) };
        _ = std.unicode.utf8Decode(str[i..][0..n]) catch return .{ .int = @intCast(i + 1) };
        i += n;
    }
    return .nil;
}

fn sub(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .sub, args[0], "its first argument");
    const i = try bi.intArg(run, pos, .sub, args[1], "its start");
    const j = if (args.len > 2) try bi.intArg(run, pos, .sub, args[2], "its end") else -1;
    const r = subRange(str.len, i, j);
    try run.charge(pos, r.len);
    run.tally(r.len);
    return .{ .string = str[r.start..][0..r.len] };
}

/// Lua's `string.sub` range: 1-based, inclusive, negatives from the end,
/// clamped to the string.
pub fn subRange(len: usize, i: i64, j: i64) struct { start: usize, len: usize } {
    const n: i64 = @intCast(len);
    var a: i64 = if (i < 0) @max(n + i + 1, 1) else if (i == 0) 1 else i;
    var z: i64 = if (j < 0) n + j + 1 else @min(j, n);
    if (a < 1) a = 1;
    if (z > n) z = n;
    if (a > z) return .{ .start = 0, .len = 0 };
    return .{ .start = @intCast(a - 1), .len = @intCast(z - a + 1) };
}

const Side = enum { both, start, end };

/// `trim`, `trim_start`, `trim_end` remove spaces, tabs, `\r` and `\n` from
/// both ends, the start, or the end. Returns a slice of `s`; nothing is copied.
fn trimmed(comptime side: Side) bi.Impl {
    return struct {
        fn f(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
            const b: B = switch (side) {
                .both => .trim,
                .start => .trim_start,
                .end => .trim_end,
            };
            const str = try bi.stringArg(run, pos, b, args[0], "its first argument");
            try run.charge(pos, str.len);
            run.tally(str.len);
            const blank = " \t\r\n";
            return .{ .string = switch (side) {
                .both => std.mem.trim(u8, str, blank),
                .start => std.mem.trimStart(u8, str, blank),
                .end => std.mem.trimEnd(u8, str, blank),
            } };
        }
    }.f;
}

const Case = enum { upper, lower };

fn cased(comptime c: Case) bi.Impl {
    return struct {
        fn f(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
            const b: B = if (c == .upper) .upper else .lower;
            const str = try bi.stringArg(run, pos, b, args[0], "its first argument");
            try run.charge(pos, str.len);
            const out = run.alloc.alloc(u8, str.len) catch return run.oom(pos);
            for (str, out) |ch, *o| o.* = if (c == .upper) std.ascii.toUpper(ch) else std.ascii.toLower(ch);
            run.tally(str.len);
            return .{ .string = out };
        }
    }.f;
}

fn tostring(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    var buf: [24]u8 = undefined;
    const t = try bi.scalarText(run, pos, args[0], &buf, "tostring");
    try run.charge(pos, t.len);
    run.tally(t.len);
    return .{ .string = run.alloc.dupe(u8, t) catch return run.oom(pos) };
}

fn tonumber(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = try bi.stringArg(run, pos, .tonumber, args[0], "its first argument");
    try run.charge(pos, str.len);
    run.tally(str.len);
    return .{ .int = bi.parseInteger(str) orelse return run.fail(pos, .argument, "`tonumber`: \"{s}\" is not an integer (an optional `-`, then decimal digits)", .{str[0..@min(str.len, 40)]}) };
}

/// `parse_int(s)` returns the integer `s` denotes, ignoring surrounding spaces,
/// tabs, `\r` and `\n`, or nil for anything else: a torn line, a word, a number
/// past 64 bits. For data a script did not write, where `tonumber` stopping
/// would end the whole run. nil maps to nil and an integer to itself, so a
/// missing field reads as no number.
fn parseInt(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const str = switch (args[0]) {
        .nil => return .nil,
        .int => return args[0],
        .string => |s| s,
        else => return run.fail(pos, .type, "`parse_int` takes a string (or nil, or an integer), not {s}", .{args[0].typeName()}),
    };
    try run.charge(pos, str.len);
    run.tally(str.len);
    const n = bi.parseInteger(std.mem.trim(u8, str, " \t\r\n")) orelse return .nil;
    return .{ .int = n };
}

/// `between(s, a, b)` returns the text after the first `a` in `s` and before
/// the first `b` after it, or nil when either is absent. `a` of "" is the start
/// of `s`, `b` of "" its end. Extracts a field without regular expressions.
fn between(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const s = try bi.stringArg(run, pos, .between, args[0], "its first argument");
    const a = try bi.stringArg(run, pos, .between, args[1], "what it starts after");
    const b = try bi.stringArg(run, pos, .between, args[2], "what it ends before");
    const fa = try Finder.init(run, pos, a);
    const start = (try fa.find(s, 0) orelse return .nil) + a.len;
    if (b.len == 0) {
        try run.charge(pos, s.len);
        return .{ .string = s[start..] };
    }
    const fb = try Finder.init(run, pos, b);
    const end = try fb.find(s, start) orelse return .nil;
    try bi.chargeBytes(run, pos, end + b.len);
    return .{ .string = s[start..end] };
}

/// `match_path(path, pattern)` returns whether a path matches a glob, part by
/// part on `/`: `**` matches any number of parts, including none, and in any
/// other part `*` and `?` are as in `like`, never crossing a `/`. So
/// `src/**/*.zig` is every `.zig` file under `src`, and `src/tcp*.zig` only
/// those directly in it.
fn matchPath(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const subject = try bi.stringArg(run, pos, .match_path, args[0], "its path");
    const pattern = try bi.stringArg(run, pos, .match_path, args[1], "its pattern");
    var ps: [path_parts_most][]const u8 = undefined;
    var qs: [path_parts_most][]const u8 = undefined;
    const p = splitParts(subject, &ps) orelse return run.fail(pos, .argument, "`match_path`: a path of more than {d} parts", .{path_parts_most});
    const q = splitParts(pattern, &qs) orelse return run.fail(pos, .argument, "`match_path`: a pattern of more than {d} parts", .{path_parts_most});
    try run.charge(pos, subject.len + pattern.len);
    // `can[j]`: whether the pattern's first i parts match the path's first
    // j, row by row over i.
    var can: [path_parts_most + 1]bool = @splat(false);
    can[0] = true;
    for (q) |part| {
        var next: [path_parts_most + 1]bool = @splat(false);
        if (std.mem.eql(u8, part, "**")) {
            var any = false;
            for (0..p.len + 1) |j| {
                any = any or can[j];
                next[j] = any;
            }
        } else for (1..p.len + 1) |j| {
            if (can[j - 1]) next[j] = try likeCharged(run, pos, p[j - 1], part);
        }
        can = next;
    }
    return .{ .boolean = can[p.len] };
}

/// The most `/`-separated parts `match_path` accepts in a path or pattern.
const path_parts_most = 64;

fn splitParts(s: []const u8, out: *[path_parts_most][]const u8) ?[]const []const u8 {
    var n: usize = 0;
    var it = std.mem.splitScalar(u8, s, '/');
    while (it.next()) |part| {
        if (n == out.len) return null;
        out[n] = part;
        n += 1;
    }
    return out[0..n];
}

/// `like(s, pattern)` returns whether all of `s` matches `pattern`, where `*`
/// matches any run of bytes, including none, `?` one byte, and every other
/// byte itself. No classes, no escapes: not Lua's patterns.
fn like(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const s = try bi.stringArg(run, pos, .like, args[0], "its first argument");
    const p = try bi.stringArg(run, pos, .like, args[1], "its pattern");
    return .{ .boolean = try likeCharged(run, pos, s, p) };
}

/// The match behind `like`: the classic walk that backtracks only to the last
/// `*`. Every byte examined is a step, so a pattern that backtracks over a
/// long string runs out of steps.
pub fn likeCharged(run: *Run, pos: ast.Pos, s: []const u8, p: []const u8) Error!bool {
    var si: usize = 0;
    var pi: usize = 0;
    var star: ?usize = null;
    var mark: usize = 0;
    var looked: u64 = 0;
    while (si < s.len) {
        looked += 1;
        if (looked == 1024) {
            run.tally(looked);
            try run.charge(pos, looked);
            looked = 0;
        }
        if (pi < p.len and (p[pi] == '?' or (p[pi] != '*' and p[pi] == s[si]))) {
            si += 1;
            pi += 1;
        } else if (pi < p.len and p[pi] == '*') {
            star = pi;
            mark = si;
            pi += 1;
        } else if (star) |st| {
            pi = st + 1;
            mark += 1;
            si = mark;
        } else {
            run.tally(looked);
            try run.charge(pos, looked);
            return false;
        }
    }
    while (pi < p.len and p[pi] == '*') pi += 1;
    run.tally(looked + p.len);
    try run.charge(pos, looked + p.len);
    return pi == p.len;
}
