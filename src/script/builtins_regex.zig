//! The regular-expression builtins, `re_find` and `re_match`, over regex.zig's
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
    if (args.len < 3) return o;
    const m = try bi.mapArg(run, pos, b, args[2]);
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
    const per: u64 = c.program.insts.len;
    const most = (run.budgets.steps -| run.steps) / per + 1;
    const r = regex.search(&c.program, &c.scratch, s, most);
    run.tally(r.work);
    try run.charge(pos, r.positions * per);
    std.debug.assert(r.finished);
    return .{ r.found, c };
}

/// `re_find(s, pattern[, {nocase}])` returns the leftmost match of `pattern` in
/// `s`, the one a backtracking matcher would find first, as `{at, text}` with
/// `at` 1-based, or nil. When the pattern has a capture group, also `group`,
/// its text ("" where the match did not pass through it).
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
    if (c.program.group) m.map.put(a, "group", .{ .string = if (f.group) |g| s[g[0]..g[1]] else "" }) catch return run.oom(pos);
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
