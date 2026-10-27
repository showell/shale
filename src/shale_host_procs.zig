//! The host's process calls, `procs` and `proc_stop`: a script finds a
//! process it means by what `procs` gives (its `args`, its `name`) and stops
//! that one, never one a pattern also matches.
//!
//! **NEVER THE CALLER, BY CONSTRUCTION.** `pkill -f pattern` matches its own
//! shell when the shell's command line holds the pattern, and kills the
//! command that ran it. `procs` leaves out this process and every one above
//! it, so no filter of its list can name them, and `proc_stop` refuses them
//! whatever it is handed. A process is named by its pid and its start time
//! together, and signalled through a pidfd opened before its start time is
//! checked, so a pid the kernel has given to another since is never
//! signalled. A signal refused is a stop, never a success.

const std = @import("std");
const linux = std.os.linux;
const shale = @import("shale");
const script = shale.script;
const interp = script.interp;
const ast = script.ast;
const Value = script.value.Value;
const Run = interp.Run;
const Error = interp.Error;
const hl = @import("shale_host_linux.zig");
const Host = hl.Host;

/// The most entries `procs` gives; past it the list ends with `{more = true}`.
pub const most = 4096;

/// The most of a command line `procs` reads; past it the entry says
/// `args_cut = true`, so a filter knows it saw only the start.
pub const args_most = 1 << 20;

pub fn call(h: *Host, run: *Run, pos: ast.Pos, name: []const u8, args: []const Value) Error!Value {
    if (std.mem.eql(u8, name, "procs")) {
        if (args.len != 0) return run.fail(pos, .argument, "`procs` takes nothing, given {d} arguments: filter its list by `args` or `name`", .{args.len});
        return list(run, pos);
    }
    if (std.mem.eql(u8, name, "proc_stop")) {
        if (args.len != 1) return run.fail(pos, .argument, "`proc_stop` takes one entry of `procs()`, given {d} arguments", .{args.len});
        return stop(h, run, pos, args[0]);
    }
    return run.fail(pos, .host, "`{s}`: not on this host", .{name});
}

/// One process as /proc/<pid>/stat has it.
const Stat = struct { ppid: i32, start: []const u8, name: []const u8 };

/// The real uid in /proc/<pid>/status, or null.
fn uidOf(pid: i32) ?i64 {
    var buf: [4096]u8 = undefined;
    const text = readProc(&buf, pid, "status") orelse return null;
    var lines = std.mem.splitScalar(u8, text, '\n');
    while (lines.next()) |line| if (std.mem.startsWith(u8, line, "Uid:")) {
        var it = std.mem.tokenizeAny(u8, line[4..], " \t");
        return std.fmt.parseInt(i64, it.next() orelse return null, 10) catch null;
    };
    return null;
}

/// The whole of /proc/<pid>/cmdline up to `args_most`, its NULs spaces;
/// null for a process gone. `cut` when there was more.
fn cmdline(a: std.mem.Allocator, pid: i32, cut: *bool) error{OutOfMemory}!?[]u8 {
    var path: [64]u8 = undefined;
    const p = std.fmt.bufPrintSentinel(&path, "/proc/{d}/cmdline", .{pid}, 0) catch return null;
    const fd = linux.open(p, .{ .ACCMODE = .RDONLY }, 0);
    if (linux.errno(fd) != .SUCCESS) return null;
    defer _ = linux.close(@intCast(fd));
    var out: std.ArrayList(u8) = .empty;
    var chunk: [4096]u8 = undefined;
    cut.* = false;
    while (true) {
        const n = linux.read(@intCast(fd), &chunk, chunk.len);
        if (linux.errno(n) != .SUCCESS) return null;
        if (n == 0) break;
        if (out.items.len + n > args_most) {
            try out.appendSlice(a, chunk[0 .. args_most - out.items.len]);
            cut.* = true;
            break;
        }
        try out.appendSlice(a, chunk[0..n]);
    }
    var n = out.items.len;
    while (n > 0 and out.items[n - 1] == 0) n -= 1;
    const args = out.items[0..n];
    std.mem.replaceScalar(u8, args, 0, ' ');
    return args;
}

fn stat(buf: []u8, pid: i32) ?Stat {
    const text = readProc(buf, pid, "stat") orelse return null;
    const open = std.mem.indexOfScalar(u8, text, '(') orelse return null;
    const close = std.mem.lastIndexOfScalar(u8, text, ')') orelse return null;
    if (close < open) return null;
    var it = std.mem.tokenizeScalar(u8, text[close + 1 ..], ' ');
    var ppid: ?i32 = null;
    // Field 3 (the state) is the first after `)`; the parent is 4, the start 22.
    var i: usize = 3;
    while (it.next()) |f| : (i += 1) {
        if (i == 4) ppid = std.fmt.parseInt(i32, f, 10) catch return null;
        if (i == 22) return .{ .ppid = ppid orelse return null, .start = f, .name = text[open + 1 .. close] };
    }
    return null;
}

fn readProc(buf: []u8, pid: i32, what: []const u8) ?[]u8 {
    var path: [64]u8 = undefined;
    const p = std.fmt.bufPrintSentinel(&path, "/proc/{d}/{s}", .{ pid, what }, 0) catch return null;
    const fd = linux.open(p, .{ .ACCMODE = .RDONLY }, 0);
    if (linux.errno(fd) != .SUCCESS) return null;
    defer _ = linux.close(@intCast(fd));
    const n = linux.read(@intCast(fd), buf.ptr, buf.len);
    if (linux.errno(n) != .SUCCESS) return null;
    return buf[0..n];
}

/// This process and every one above it, to pid 1's child: the ones no
/// script may stop.
fn lineage(out: *[64]i32) []const i32 {
    var n: usize = 0;
    var pid: i32 = linux.getpid();
    var buf: [1024]u8 = undefined;
    while (pid > 1 and n < out.len) {
        out[n] = pid;
        n += 1;
        pid = (stat(&buf, pid) orelse break).ppid;
    }
    return out[0..n];
}

fn inLineage(l: []const i32, pid: i32) bool {
    return std.mem.indexOfScalar(i32, l, pid) != null;
}

/// `procs()`: every process but this one and those above it, in pid order,
/// as `{pid, ppid, uid, name, args, start}`: `args` its command line joined
/// by spaces (with `args_cut = true` past `args_most`), `start` its start
/// time in the kernel's ticks (what `proc_stop` checks it by).
fn list(run: *Run, pos: ast.Pos) Error!Value {
    const a = run.alloc;
    var lbuf: [64]i32 = undefined;
    const mine = lineage(&lbuf);
    var pids: std.ArrayList(i32) = .empty;
    var dir = std.Io.Dir.cwd().openDir(run.io, "/proc", .{ .iterate = true }) catch return run.fail(pos, .host, "`procs`: /proc cannot be read", .{});
    defer dir.close(run.io);
    var it = dir.iterate();
    while (it.next(run.io) catch return run.fail(pos, .host, "`procs`: /proc cannot be listed", .{})) |e| {
        const pid = std.fmt.parseInt(i32, e.name, 10) catch continue;
        if (inLineage(mine, pid)) continue;
        pids.append(a, pid) catch return run.oom(pos);
    }
    std.mem.sort(i32, pids.items, {}, std.sort.asc(i32));
    const out = try run.newTable(pos);
    out.kind = .list;
    var buf: [1024]u8 = undefined;
    for (pids.items) |pid| {
        if (out.list.items.len == most) {
            const more = try run.newTable(pos);
            more.kind = .map;
            more.map.put(a, "more", .{ .boolean = true }) catch return run.oom(pos);
            out.list.append(a, .{ .table = more }) catch return run.oom(pos);
            break;
        }
        // A process gone between the listing and its read is passed by.
        const s = stat(&buf, pid) orelse continue;
        var cut = false;
        const args = (cmdline(a, pid, &cut) catch return run.oom(pos)) orelse continue;
        try run.charge(pos, 1 + args.len / 16);
        const m = try run.newTable(pos);
        m.kind = .map;
        m.map.put(a, "pid", .{ .int = pid }) catch return run.oom(pos);
        m.map.put(a, "ppid", .{ .int = s.ppid }) catch return run.oom(pos);
        m.map.put(a, "uid", if (uidOf(pid)) |u| .{ .int = u } else .nil) catch return run.oom(pos);
        m.map.put(a, "name", .{ .string = a.dupe(u8, s.name) catch return run.oom(pos) }) catch return run.oom(pos);
        m.map.put(a, "args", .{ .string = args }) catch return run.oom(pos);
        if (cut) m.map.put(a, "args_cut", .{ .boolean = true }) catch return run.oom(pos);
        m.map.put(a, "start", .{ .string = a.dupe(u8, s.start) catch return run.oom(pos) }) catch return run.oom(pos);
        out.list.append(a, .{ .table = m }) catch return run.oom(pos);
    }
    return .{ .table = out };
}

/// `proc_stop(p)`: TERM to the process `p` (an entry of `procs()`), 2
/// seconds, then KILL; whether it was running. Refused for this process
/// and those above it; a process whose start time is not `p.start` is not
/// `p`, and is left alone (false). **A SIGNAL REFUSED IS A STOP** (the
/// box's review): a process the run may not signal stops the run, named,
/// never a success with the process still there; nor is one still there
/// 2 seconds after KILL.
fn stop(h: *Host, run: *Run, pos: ast.Pos, v: Value) Error!Value {
    const m = switch (v) {
        .table => |t| if (t.kind == .map) t else return run.fail(pos, .type, "`proc_stop` takes an entry of `procs()`, a map, not a list", .{}),
        else => return run.fail(pos, .type, "`proc_stop` takes an entry of `procs()`, not {s}", .{v.typeName()}),
    };
    const pid: i32 = switch (m.map.get("pid") orelse return run.fail(pos, .argument, "`proc_stop`: the entry has no `pid`: hand it one of `procs()`", .{})) {
        .int => |n| std.math.cast(i32, n) orelse return run.fail(pos, .argument, "`proc_stop`: pid {d} is no process's", .{n}),
        else => return run.fail(pos, .type, "`proc_stop`: the entry's `pid` is not an integer", .{}),
    };
    const start = switch (m.map.get("start") orelse return run.fail(pos, .argument, "`proc_stop`: the entry has no `start`: hand it one of `procs()`, which names a process by its pid and its start", .{})) {
        .string => |s| s,
        else => return run.fail(pos, .type, "`proc_stop`: the entry's `start` is not a string", .{}),
    };
    var lbuf: [64]i32 = undefined;
    if (pid <= 1 or inLineage(lineage(&lbuf), pid)) return run.fail(pos, .argument, "`proc_stop`: pid {d} is this run or one above it, which a script never stops", .{pid});
    try run.charge(pos, 1);
    // **ONE PROCESS, WHATEVER ITS NUMBER BECOMES**: the pidfd names the
    // process that held the pid when it was opened, and its start time,
    // checked after, says that is `p`.
    const fd_rc = linux.pidfd_open(pid, 0);
    switch (linux.errno(fd_rc)) {
        .SUCCESS => {},
        .SRCH => return .{ .boolean = false },
        else => |e| return run.fail(pos, .host, "`proc_stop`: pid {d}: pidfd_open: {t}", .{ pid, e }),
    }
    const fd: i32 = @intCast(fd_rc);
    defer _ = linux.close(fd);
    if (!same(pid, start)) return .{ .boolean = false };
    if (!try signal(run, pos, fd, pid, .TERM)) return .{ .boolean = false };
    if (!gone(fd, 2000)) {
        _ = try signal(run, pos, fd, pid, .KILL);
        if (!gone(fd, 2000)) return run.fail(pos, .host, "`proc_stop`: pid {d} is still there 2 seconds after KILL", .{pid});
    }
    h.record("process {d} stopped", .{pid});
    return .{ .boolean = true };
}

/// `sig` to the process `fd` names: false when it has gone already; any
/// refusal (EPERM, for one the run may not signal) stops the run.
fn signal(run: *Run, pos: ast.Pos, fd: i32, pid: i32, sig: linux.SIG) Error!bool {
    switch (linux.errno(linux.pidfd_send_signal(fd, sig, null, 0))) {
        .SUCCESS => return true,
        .SRCH => return false,
        else => |e| return run.fail(pos, .host, "`proc_stop`: pid {d} refused {t}: {t}", .{ pid, sig, e }),
    }
}

/// Whether the process `fd` names ends within `ms`: its pidfd turns
/// readable when it exits, a zombie too.
fn gone(fd: i32, ms: i32) bool {
    var p = [1]linux.pollfd{.{ .fd = fd, .events = linux.POLL.IN, .revents = 0 }};
    const rc = linux.poll(&p, 1, ms);
    return linux.errno(rc) == .SUCCESS and rc == 1;
}

/// Whether `pid` is still the process that started at `start`.
fn same(pid: i32, start: []const u8) bool {
    var buf: [1024]u8 = undefined;
    const s = stat(&buf, pid) orelse return false;
    return std.mem.eql(u8, s.start, start);
}
