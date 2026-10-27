//! The regular-expression builtins, `re_find`, `re_match` and `re_replace`, over regex.zig's
//! linear-time subset. A pattern is compiled once per run (cached in the
//! machine's memory) at a step per byte; a search is charged a step per byte
//! of the string, plus one for its end, times the pattern's instructions, so a
//! budget bounds a search whatever the pattern.

const std = @import("std");
const ast = @import("ast.zig");
const interp = @import("interp.zig");
const value = @import("value.zig");
const bi = @import("builtins.zig");
const regex = @import("regex.zig");

const Run = interp.Run;
const Value = value.Value;
const Error = interp.Error;
const B = ast.Builtin;
const spec = bi.spec;

pub const specs = [_]bi.Spec{
    spec(.re_find, 2, 3, reFind),
    spec(.re_match, 2, 3, reMatch),
    spec(.re_replace, 3, 4, reReplace),
};

/// Returns the pattern's compiled program from the run's cache, compiling and
/// caching it if absent.
fn program(run: *Run, pos: ast.Pos, b: B, pattern: []const u8, options: regex.Options) Error!*regex.Cached {
    if (run.patterns.get(pattern, options)) |got| return got;
    if (pattern.len > regex.pattern_most) return run.fail(pos, .argument, "`{s}`'s pattern is {d} bytes, and one may be at most {d}", .{ @tagName(b), pattern.len, regex.pattern_most });
    // A step per byte to compile, and a step per byte more to fold case.
    const cost = pattern.len + 1 + if (options.nocase) pattern.len else 0;
    try run.charge(pos, cost);
    run.tally(cost);
    var refused: regex.Refusal = undefined;
    return run.patterns.put(run.machine, pattern, options, &refused) catch |e| switch (e) {
        error.OutOfMemory => run.machineOom(pos),
        error.Refused => run.fail(pos, .argument, "`{s}`'s pattern, at its byte {d}: {s}", .{ @tagName(b), refused.at + 1, refused.message }),
    };
}

/// Parses the options map a call may end with: `{nocase = true}`.
fn optionsArg(run: *Run, pos: ast.Pos, b: B, args: []const Value) Error!regex.Options {
    var o: regex.Options = .{};
    // The options follow the string and the pattern, and `re_replace`'s
    // replacement too.
    const at: usize = if (b == .re_replace) 3 else 2;
    if (args.len <= at) return o;
    const m = try bi.mapArg(run, pos, b, args[at]);
    for (m.map.keys(), m.map.values()) |k, x| {
        if (!std.mem.eql(u8, k, "nocase")) return run.fail(pos, .argument, "`{s}` takes `nocase`, not `{s}`", .{ @tagName(b), k });
        o.nocase = switch (x) {
            .boolean => |v| v,
            else => return run.fail(pos, .type, "`{s}`: its `nocase` must be true or false, not {s}", .{ @tagName(b), x.typeName() }),
        };
    }
    return o;
}

/// Runs the search, charged as it goes: when the steps left would not cover
/// all of it, it stops where they run out.
fn searched(run: *Run, pos: ast.Pos, b: B, s: []const u8, pattern: []const u8, args: []const Value) Error!struct { ?regex.Found, *regex.Cached } {
    const c = try program(run, pos, b, pattern, try optionsArg(run, pos, b, args));
    return .{ try searchedFrom(run, pos, c, s, 0), c };
}

/// One search of `s` from byte `from`, charged as `searched` charges.
fn searchedFrom(run: *Run, pos: ast.Pos, c: *regex.Cached, s: []const u8, from: usize) Error!?regex.Found {
    const per: u64 = c.program.insts.len;
    const most = (run.budgets.steps -| run.steps) / per + 1;
    const r = regex.searchFrom(&c.program, &c.scratch, s, from, most);
    run.tally(r.work);
    try run.charge(pos, r.positions * per);
    std.debug.assert(r.finished);
    return r.found;
}

/// `re_find(s, pattern[, {nocase}])` returns the leftmost match of `pattern` in
/// `s`, the one a backtracking matcher would find first, as `{at, text}` with
/// `at` 1-based, or nil. When the pattern has capture groups, also `groups`,
/// each one's text in order of its `(` ("" where the match did not pass
/// through it), and `group`, the first's: a one-group pattern reads as it did.
fn reFind(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const s = try bi.stringArg(run, pos, .re_find, args[0], "its first argument");
    const pattern = try bi.stringArg(run, pos, .re_find, args[1], "its pattern");
    const found, const c = try searched(run, pos, .re_find, s, pattern, args);
    const f = found orelse return .nil;
    const a = run.alloc;
    const m = try run.newTable(pos);
    m.kind = .map;
    m.map.put(a, "at", .{ .int = @intCast(f.start + 1) }) catch return run.oom(pos);
    m.map.put(a, "text", .{ .string = s[f.start..f.end] }) catch return run.oom(pos);
    const n = c.program.groups;
    if (n > 0) {
        const gs = try run.newTable(pos);
        for (f.groups[0..n]) |g| gs.list.append(a, .{ .string = if (g) |x| s[x[0]..x[1]] else "" }) catch return run.oom(pos);
        m.map.put(a, "groups", .{ .table = gs }) catch return run.oom(pos);
        m.map.put(a, "group", gs.list.items[0]) catch return run.oom(pos);
    }
    return .{ .table = m };
}

/// `re_match(s, pattern[, {nocase}])` returns whether `s` contains a match of
/// `pattern` anywhere, as `grep` would of a line; `^` and `$` anchor it to the
/// whole.
fn reMatch(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const s = try bi.stringArg(run, pos, .re_match, args[0], "its first argument");
    const pattern = try bi.stringArg(run, pos, .re_match, args[1], "its pattern");
    const found, _ = try searched(run, pos, .re_match, s, pattern, args);
    return .{ .boolean = found != null };
}

/// `re_replace(s, pattern, replacement[, {nocase}])` returns `s` with every
/// non-overlapping match of `pattern` replaced, left to right, each search
/// over the whole of `s` so `^`, `$` and `\b` see its real edges. The
/// replacement is literal but for `\1` to `\9`, that capture group's text (""
/// where the match did not pass through it); a `\N` past the pattern's groups
/// is an error. After an empty match the next search starts a byte on, that byte
/// kept, so an empty match never repeats.
fn reReplace(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const s = try bi.stringArg(run, pos, .re_replace, args[0], "its first argument");
    const pattern = try bi.stringArg(run, pos, .re_replace, args[1], "its pattern");
    const with = try bi.stringArg(run, pos, .re_replace, args[2], "its replacement");
    const c = try program(run, pos, .re_replace, pattern, try optionsArg(run, pos, .re_replace, args));
    const groups = c.program.groups;
    // How often each group is named, and the bytes the names take.
    var uses: [regex.groups_most]usize = @splat(0);
    var named: usize = 0;
    var k: usize = 0;
    while (k + 1 < with.len) : (k += 1) {
        if (with[k] != '\\' or with[k + 1] < '1' or with[k + 1] > '9') continue;
        const g = with[k + 1] - '1';
        if (g >= groups) {
            if (groups == 0) return run.fail(pos, .argument, "`re_replace`'s replacement has `\\{c}`, and its pattern has no capture group", .{with[k + 1]});
            return run.fail(pos, .argument, "`re_replace`'s replacement has `\\{c}`, and its pattern has only {d} capture group{s}", .{ with[k + 1], groups, if (groups == 1) "" else "s" });
        }
        uses[g] += 1;
        named += 2;
        k += 1;
    }
    try bi.chargeBytes(run, pos, s.len + with.len);
    var out: std.ArrayList(u8) = .empty;
    const a = run.alloc;
    var at: usize = 0;
    var replaced = false;
    while (at <= s.len) {
        const f = try searchedFrom(run, pos, c, s, at) orelse break;
        replaced = true;
        // A step a byte this match adds, charged before it is written.
        var grown: usize = 0;
        for (uses[0..groups], f.groups[0..groups]) |u, g| {
            const len = if (g) |x| x[1] - x[0] else 0;
            grown = std.math.add(usize, grown, std.math.mul(usize, u, len) catch return run.budgetStop(pos, .arena)) catch return run.budgetStop(pos, .arena);
        }
        try run.charge(pos, (f.start - at) + with.len - named + grown + 1);
        out.appendSlice(a, s[at..f.start]) catch return run.oom(pos);
        var i: usize = 0;
        while (i < with.len) {
            if (with[i] == '\\' and i + 1 < with.len and with[i + 1] >= '1' and with[i + 1] <= '9') {
                if (f.groups[with[i + 1] - '1']) |x| out.appendSlice(a, s[x[0]..x[1]]) catch return run.oom(pos);
                i += 2;
            } else {
                out.append(a, with[i]) catch return run.oom(pos);
                i += 1;
            }
        }
        if (f.end > f.start) {
            at = f.end;
        } else {
            if (f.end < s.len) out.append(a, s[f.end]) catch return run.oom(pos);
            at = f.end + 1;
        }
    }
    if (!replaced) return .{ .string = s };
    if (at < s.len) {
        try run.charge(pos, s.len - at);
        out.appendSlice(a, s[at..]) catch return run.oom(pos);
    }
    run.tally(out.items.len);
    return .{ .string = out.items };
}
