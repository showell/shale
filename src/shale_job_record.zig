//! A job's record, `<log>.job`: `key=value` lines its reaper writes and every
//! later `shale` reads to find the job again. Plain text, no system call, so
//! it is the part of jobs a port keeps; the processes it names are
//! `shale_jobs.zig`'s.

const std = @import("std");

/// A job's record, as `<log>.job` holds it.
pub const Record = struct {
    /// The job's leader.
    pid: i32 = 0,
    /// The job's process group, which is its session's.
    pgid: i32 = 0,
    /// The leader's start time, raw from `/proc/<pid>/stat`.
    start: []const u8 = "",
    /// When the job started, in Unix seconds.
    started: i64 = 0,
    /// The job's argv joined by spaces, for display.
    argv: []const u8 = "",
    /// The reaper's pid.
    reaper: i32 = 0,
    /// The reaper's start time, raw from `/proc/<pid>/stat`.
    reaper_start: []const u8 = "",
    /// The exit code, once the job exited.
    exit: ?u8 = null,
    /// The signal that ended the job, once it did.
    signal: ?u32 = null,
    /// When the job ended, in Unix seconds.
    ended: ?i64 = null,

    pub fn done(r: Record) bool {
        return r.exit != null or r.signal != null;
    }
};

/// Parses a record's text. Returns `null` if it is not a record: a key
/// missing (`formatRecord` writes each of the first seven), one it does not
/// know or twice, a line that is no `key=value`, or an `argv` escaped
/// wrongly. A missing `start` read as "" would make `alive` say false of a
/// job that runs. `argv` is unescaped in place, so `text` is changed.
pub fn parseRecord(text: []u8) ?Record {
    var r: Record = .{};
    const keys = [_][]const u8{ "pid", "pgid", "start", "started", "argv", "reaper", "reaper_start", "exit", "signal", "ended" };
    const required = 7;
    var seen: [keys.len]bool = @splat(false);
    var it = std.mem.splitScalar(u8, text, '\n');
    while (it.next()) |line_c| {
        if (line_c.len == 0) continue;
        // The line, as the part of `text` it is, so `argv` can be unescaped there.
        const at = @intFromPtr(line_c.ptr) - @intFromPtr(text.ptr);
        const line = text[at .. at + line_c.len];
        const eq = std.mem.indexOfScalar(u8, line, '=') orelse return null;
        const k = line[0..eq];
        const v = line[eq + 1 ..];
        const i = for (keys, 0..) |name, i| {
            if (std.mem.eql(u8, k, name)) break i;
        } else return null;
        if (seen[i]) return null;
        seen[i] = true;
        switch (i) {
            0 => r.pid = std.fmt.parseInt(i32, v, 10) catch return null,
            1 => r.pgid = std.fmt.parseInt(i32, v, 10) catch return null,
            2 => r.start = v,
            3 => r.started = std.fmt.parseInt(i64, v, 10) catch return null,
            4 => r.argv = unescape(v) orelse return null,
            5 => r.reaper = std.fmt.parseInt(i32, v, 10) catch return null,
            6 => r.reaper_start = v,
            7 => r.exit = std.fmt.parseInt(u8, v, 10) catch return null,
            8 => r.signal = std.fmt.parseInt(u32, v, 10) catch return null,
            else => r.ended = std.fmt.parseInt(i64, v, 10) catch return null,
        }
    }
    for (seen[0..required]) |s| if (!s) return null;
    if (r.pid <= 0 or r.pgid <= 0) return null;
    return r;
}

/// `argv` as `formatRecord` escapes it: `\n` a newline, `\\` a backslash,
/// so no argument starts a line of its own.
fn unescape(v: []u8) ?[]u8 {
    var o: usize = 0;
    var i: usize = 0;
    while (i < v.len) : (i += 1) {
        if (v[i] == '\\') {
            i += 1;
            if (i == v.len) return null;
            v[o] = switch (v[i]) {
                'n' => '\n',
                '\\' => '\\',
                else => return null,
            };
        } else v[o] = v[i];
        o += 1;
    }
    return v[0..o];
}

/// The record's text in `buf`, `argv` escaped; null if it does not fit,
/// never a record cut short.
pub fn formatRecord(buf: []u8, r: Record) ?[]u8 {
    var w: std.Io.Writer = .fixed(buf);
    w.print("pid={d}\npgid={d}\nstart={s}\nstarted={d}\nargv=", .{ r.pid, r.pgid, r.start, r.started }) catch return null;
    for (r.argv) |c| switch (c) {
        '\\' => w.writeAll("\\\\") catch return null,
        '\n' => w.writeAll("\\n") catch return null,
        else => w.writeByte(c) catch return null,
    };
    w.print("\nreaper={d}\nreaper_start={s}\n", .{ r.reaper, r.reaper_start }) catch return null;
    if (r.exit) |c| w.print("exit={d}\n", .{c}) catch return null;
    if (r.signal) |s| w.print("signal={d}\n", .{s}) catch return null;
    if (r.ended) |e| w.print("ended={d}\n", .{e}) catch return null;
    return w.buffered();
}

/// The most bytes a job's argv may take in its record, escaped
/// (`formatRecord`): the rest of `shale_jobs.zig`'s `writeRecord`'s 8 KiB holds
/// the other keys.
pub const argv_most = 7168;

test "jobs: a record reads back as it was written, and a broken one is none" {
    var buf: [512]u8 = undefined;
    const text = formatRecord(&buf, .{ .pid = 42, .pgid = 42, .start = "123", .started = 9, .argv = "zig build", .exit = 3, .ended = 11 }).?;
    const r = parseRecord(text).?;
    try std.testing.expectEqual(@as(i32, 42), r.pid);
    try std.testing.expectEqualStrings("123", r.start);
    try std.testing.expectEqual(@as(?u8, 3), r.exit);
    try std.testing.expect(r.done());
    try std.testing.expect(parseCopy("pid=x\n") == null);
    try std.testing.expect(parseCopy("") == null);
    // A record missing a key, holding one it does not know, or a line that
    // is no `key=value`, is none: never a record with a key read as empty.
    try std.testing.expect(parseCopy("pid=42\npgid=42\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n") == null);
    try std.testing.expect(parseCopy("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\nexitt=3\n") == null);
    try std.testing.expect(parseCopy("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\ngarbage\n") == null);
}

test "jobs: an argv with a newline or a backslash reads back as it was, and none overrides a key; a key twice is none" {
    var buf: [512]u8 = undefined;
    for ([_][]const u8{ "git commit -m one\ntwo", "x\npid=7\nexit=3", "a\\nb \\ c" }) |argv| {
        const r = parseRecord(formatRecord(&buf, .{ .pid = 42, .pgid = 42, .start = "123", .started = 9, .argv = argv, .reaper = 1, .reaper_start = "5" }).?) orelse return error.TestUnexpectedResult;
        try std.testing.expectEqualStrings(argv, r.argv);
        try std.testing.expectEqual(@as(i32, 42), r.pid);
        try std.testing.expect(!r.done());
    }
    try std.testing.expect(parseCopy("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\npid=7\n") == null);
    try std.testing.expect(parseCopy("pid=42\npgid=42\nstart=1\nstarted=9\nargv=a\\qb\nreaper=1\nreaper_start=5\n") == null);
    // A record too big for its buffer is none, never one cut short.
    var small: [64]u8 = undefined;
    const long: [64]u8 = @splat(120);
    try std.testing.expect(formatRecord(&small, .{ .pid = 42, .pgid = 42, .start = "123", .started = 9, .argv = &long, .reaper = 1, .reaper_start = "5" }) == null);
}

/// `parseRecord` of a literal, through a copy it may unescape in place.
fn parseCopy(comptime s: []const u8) ?Record {
    const S = struct {
        var b: [1024]u8 = undefined;
    };
    @memcpy(S.b[0..s.len], s);
    return parseRecord(S.b[0..s.len]);
}
