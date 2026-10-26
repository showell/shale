//! Builtins for time and the host: `now()`, the one time a script sees, handed
//! in by the host; `date` and `seconds`, converting to and from UTC text;
//! `arg`, `fail`, `host`, `yield`, `budget()`, what a run has left, and
//! `call_bytes()`, what one call may bring.

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
    spec(.now, 0, 0, now),
    spec(.arg, 1, 2, givenArg),
    spec(.fail, 1, 1, failWith),
    spec(.host, 1, bi.most_args, hostCall),
    spec(.budget, 0, 0, budget),
    spec(.call_bytes, 0, 0, callBytes),
    spec(.yield, 0, 0, yield),
    spec(.date, 1, 1, date),
    spec(.seconds, 1, 1, seconds),
};

/// `now()` returns the run's one time, in seconds since the epoch, as the host
/// handed it in.
fn now(run: *Run, _: ast.Pos, _: []const Value) Error!Value {
    return .{ .int = run.now };
}

/// `fail(msg)` stops the run here with class `fail` and `msg` as its message,
/// reported with its trail like any stop; `attempt` catches it. A script's own
/// assertion.
fn failWith(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const msg = try bi.stringArg(run, pos, .fail, args[0], "its message");
    try run.charge(pos, msg.len);
    return run.fail(pos, .fail, "{s}", .{msg});
}

/// `host(name, ...)` invokes the host's call `name` with the remaining
/// arguments; the Linux CLI's preludes wrap these as `run`, `real` and the
/// jobs. Where there is no host (metal), stops with class `host`, naming the
/// call. The host checks and charges its own arguments and work.
fn hostCall(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const name = try bi.stringArg(run, pos, .host, args[0], "its call's name");
    try run.charge(pos, name.len);
    const h = run.host orelse return run.fail(pos, .host, "`{s}`: not on this host", .{name});
    return h.call(h.ctx, run, pos, name, args[1..]);
}

/// `arg(name[, default])` returns the value the run was given as `name` (the
/// Linux tool's `name=value`), or `default` (nil if unset) when there is none.
/// An integer or boolean default parses the value as one (`true`, `false`,
/// `1`, `0`), and a value that does not parse is an `argument` error; any other
/// default returns the string as given.
fn givenArg(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const name = try bi.stringArg(run, pos, .arg, args[0], "its name");
    try run.charge(pos, name.len);
    const fallback: Value = if (args.len > 1) args[1] else .nil;
    for (run.args) |a| {
        if (!std.mem.eql(u8, a.name, name)) continue;
        try run.charge(pos, a.value.len);
        run.tally(name.len + a.value.len);
        switch (fallback) {
            .int => return .{ .int = bi.parseInteger(a.value) orelse return run.fail(pos, .argument, "`arg`: {s}={s} is not an integer, and its default is one", .{ name, a.value[0..@min(a.value.len, 40)] }) },
            .boolean => {
                if (std.mem.eql(u8, a.value, "true") or std.mem.eql(u8, a.value, "1")) return .{ .boolean = true };
                if (std.mem.eql(u8, a.value, "false") or std.mem.eql(u8, a.value, "0")) return .{ .boolean = false };
                return run.fail(pos, .argument, "`arg`: {s}={s} is not true or false, and its default is one", .{ name, a.value[0..@min(a.value.len, 40)] });
            },
            else => return .{ .string = run.alloc.dupe(u8, a.value) catch return run.oom(pos) },
        }
    }
    return fallback;
}

/// `yield()` is a collection point only. The VM compiles it to its own op,
/// `collect`, which copies the values the run still reaches into a fresh arena.
/// Where a slice ends is the host's choice, never the script's. The tree walker
/// keeps everything, so here it does nothing.
fn yield(_: *Run, _: ast.Pos, _: []const Value) Error!Value {
    return .nil;
}

/// `budget()` returns what is left of each budget a script spends as it runs,
/// so it can stop at a point of its own choosing.
fn budget(run: *Run, pos: ast.Pos, _: []const Value) Error!Value {
    const a = run.alloc;
    const out = try run.newTable(pos);
    const left = [_]struct { []const u8, u64 }{
        .{ "steps", run.budgets.steps -| run.steps },
        .{ "reads", run.budgets.reads -| run.reads },
        .{ "read_bytes", run.budgets.read_bytes -| run.read_bytes },
        .{ "changes", run.budgets.changes -| run.changes },
        .{ "change_bytes", run.budgets.change_bytes -| run.change_bytes },
        .{ "output", run.out_cap -| run.out_len },
        // What the values' arena may still take: a string read stays in it,
        // so a scan that reads file after file guards on this too.
        .{ "arena_bytes", run.budgeted.limit -| run.budgeted.held },
    };
    for (left) |kv| out.map.put(a, kv[0], .{ .int = @intCast(@min(kv[1], std.math.maxInt(i64))) }) catch return run.oom(pos);
    out.kind = .map;
    return .{ .table = out };
}

/// `call_bytes()` is what one store call may bring, a cap never spent, so a scan
/// sizes its parts to it. An integer, not a map: `room_for` and a scan's loop
/// call it often, and a map would be built each time.
fn callBytes(run: *Run, _: ast.Pos, _: []const Value) Error!Value {
    return .{ .int = @intCast(@min(run.budgets.call_bytes, std.math.maxInt(i64))) };
}

/// The latest second `date` writes: 9999-12-31T23:59:59Z.
const last_second: i64 = 253402300799;

/// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
fn daysFromCivil(y0: i64, m: i64, d: i64) i64 {
    const y = if (m <= 2) y0 - 1 else y0;
    const era = @divFloor(y, 400);
    const yoe = y - era * 400;
    const mp = @mod(m + 9, 12);
    const doy = @divFloor(153 * mp + 2, 5) + d - 1;
    const doe = yoe * 365 + @divFloor(yoe, 4) - @divFloor(yoe, 100) + doy;
    return era * 146097 + doe - 719468;
}

const Civil = struct { y: i64, m: i64, d: i64 };

fn civilFromDays(z0: i64) Civil {
    const z = z0 + 719468;
    const era = @divFloor(z, 146097);
    const doe = z - era * 146097;
    const yoe = @divFloor(doe - @divFloor(doe, 1460) + @divFloor(doe, 36524) - @divFloor(doe, 146096), 365);
    const doy = doe - (365 * yoe + @divFloor(yoe, 4) - @divFloor(yoe, 100));
    const mp = @divFloor(5 * doy + 2, 153);
    const d = doy - @divFloor(153 * mp + 2, 5) + 1;
    const m = if (mp < 10) mp + 3 else mp - 9;
    return .{ .y = yoe + era * 400 + @intFromBool(m <= 2), .m = m, .d = d };
}

fn daysInMonth(y: i64, m: i64) i64 {
    return switch (m) {
        2 => if (@mod(y, 4) == 0 and (@mod(y, 100) != 0 or @mod(y, 400) == 0)) 29 else 28,
        4, 6, 9, 11 => 30,
        else => 31,
    };
}

/// `date(seconds)` formats the UTC time `seconds` after the epoch as
/// `YYYY-MM-DDTHH:MM:SSZ`, from 1970 to the end of 9999.
fn date(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const arg = args[0];
    const t = try bi.intArg(run, pos, .date, arg, "its seconds");
    if (t < 0 or t > last_second) return run.fail(pos, .argument, "`date`: {d} is outside 1970-01-01 to 9999-12-31", .{t});
    const c = civilFromDays(@divFloor(t, 86400));
    const r = @mod(t, 86400);
    try run.charge(pos, 20);
    const text = std.fmt.allocPrint(run.alloc, "{d:0>4}-{d:0>2}-{d:0>2}T{d:0>2}:{d:0>2}:{d:0>2}Z", .{
        @as(u64, @intCast(c.y)),                @as(u64, @intCast(c.m)),                          @as(u64, @intCast(c.d)),
        @as(u64, @intCast(@divFloor(r, 3600))), @as(u64, @intCast(@divFloor(@mod(r, 3600), 60))), @as(u64, @intCast(@mod(r, 60))),
    }) catch return run.oom(pos);
    return .{ .string = text };
}

/// Parses `s[at..][0..n]` as decimal; null unless all of it is digits.
fn digitsAt(s: []const u8, at: usize, n: usize) ?i64 {
    if (at + n > s.len) return null;
    var v: i64 = 0;
    for (s[at..][0..n]) |c| {
        if (!std.ascii.isDigit(c)) return null;
        v = v * 10 + (c - '0');
    }
    return v;
}

/// Returns the seconds since the epoch of an RFC 3339 time, or of a date alone
/// (its midnight UTC); null when `s` is neither, or names no real day or time.
pub fn parseTime(s: []const u8) ?i64 {
    const y = digitsAt(s, 0, 4) orelse return null;
    if (s.len < 10 or s[4] != '-' or s[7] != '-') return null;
    const m = digitsAt(s, 5, 2) orelse return null;
    const d = digitsAt(s, 8, 2) orelse return null;
    if (m < 1 or m > 12 or d < 1 or d > daysInMonth(y, m)) return null;
    const day = daysFromCivil(y, m, d) * 86400;
    if (s.len == 10) return day;
    if (s.len < 20 or s[10] != 'T' or s[13] != ':' or s[16] != ':') return null;
    const hh = digitsAt(s, 11, 2) orelse return null;
    const mm = digitsAt(s, 14, 2) orelse return null;
    const ss = digitsAt(s, 17, 2) orelse return null;
    if (hh > 23 or mm > 59 or ss > 59) return null;
    var at: usize = 19;
    // Fractions of a second are dropped: the result is whole seconds.
    if (s[at] == '.') {
        at += 1;
        const from = at;
        while (at < s.len and std.ascii.isDigit(s[at])) at += 1;
        if (at == from) return null;
    }
    if (at >= s.len) return null;
    var offset: i64 = 0;
    if (s[at] == 'Z') {
        at += 1;
    } else if (s[at] == '+' or s[at] == '-') {
        const oh = digitsAt(s, at + 1, 2) orelse return null;
        if (at + 3 >= s.len or s[at + 3] != ':') return null;
        const om = digitsAt(s, at + 4, 2) orelse return null;
        if (oh > 23 or om > 59) return null;
        offset = (oh * 60 + om) * 60;
        if (s[at] == '-') offset = -offset;
        at += 6;
    } else return null;
    if (at != s.len) return null;
    return day + hh * 3600 + mm * 60 + ss - offset;
}

/// `seconds(text)` is the inverse of `date`, over `date`'s range. Accepts an
/// RFC 3339 time (`T` between date and time, then `Z` or `±HH:MM`; a fraction
/// of a second is dropped) or a date alone, meaning its midnight UTC. Anything
/// else, including a day not in the calendar, is an error.
fn seconds(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const arg = args[0];
    const s = try bi.stringArg(run, pos, .seconds, arg, "its time");
    try run.charge(pos, s.len);
    const t = parseTime(s) orelse return run.fail(pos, .argument, "`seconds`: \"{s}\" is not an RFC 3339 time (2026-10-14T03:00:00Z, or +02:00 for Z) nor a date (2026-10-14)", .{s[0..@min(s.len, 40)]});
    // Every time `seconds` returns, `date` can format.
    if (t < 0 or t > last_second) return run.fail(pos, .argument, "`seconds`: \"{s}\" is outside 1970-01-01 to 9999-12-31, `date`'s range", .{s[0..@min(s.len, 40)]});
    return .{ .int = t };
}
