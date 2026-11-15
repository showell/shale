//! The Linux system calls the host makes outside a program's spawn
//! (`shale_spawn.zig`) and a job's processes (`shale_jobs.zig`): the clock,
//! this process's own folder and path, /proc's process table, a pidfd's
//! signal and wait, and the file `run`'s `stdout_secret` writes. Its callers
//! name no `std.os.linux`, so these three files are what a port to another
//! kernel supplies (docs/PORTING.md).

const std = @import("std");
const linux = std.os.linux;

/// The kernel's error numbers, which a caller names in its message (`{t}`).
pub const Errno = linux.E;

/// A signal, as `proc_stop` sends it and names it.
pub const Signal = linux.SIG;

// ── The clock

/// Milliseconds on the monotonic clock, which a deadline is measured on
/// because the wall clock can jump.
pub fn monoMs() u64 {
    var ts: linux.timespec = undefined;
    _ = linux.clock_gettime(.MONOTONIC, &ts);
    return @as(u64, @intCast(ts.sec)) * 1000 + @as(u64, @intCast(ts.nsec)) / 1_000_000;
}

/// Sleeps for `ms`, or less if a signal comes.
pub fn sleepMs(ms: u64) void {
    const ts: linux.timespec = .{ .sec = @intCast(ms / 1000), .nsec = @intCast((ms % 1000) * 1_000_000) };
    _ = linux.nanosleep(&ts, null);
}

// ── This process

/// This process's pid.
pub fn selfPid() i32 {
    return linux.getpid();
}

/// The working folder, absolute, in `buf`.
pub fn cwd(buf: []u8) []const u8 {
    const n = linux.getcwd(buf.ptr, buf.len);
    return std.mem.sliceTo(buf[0..n], 0);
}

/// This program's own path, `/proc/self/exe`, in `buf`; null if the kernel
/// will not say.
pub fn selfExe(buf: []u8) ?[]const u8 {
    const n = linux.readlink("/proc/self/exe", buf.ptr, buf.len);
    if (linux.errno(n) != .SUCCESS) return null;
    return buf[0..n];
}

// ── The process table, /proc

/// One process as /proc/<pid>/stat has it.
pub const ProcStat = struct { ppid: i32, start: []const u8, name: []const u8 };

/// Every pid /proc lists, in its order. `NotReadable` when /proc cannot be
/// opened, `NotListable` when its listing fails part way.
pub fn procPids(io: std.Io, a: std.mem.Allocator) error{ NotReadable, NotListable, OutOfMemory }![]i32 {
    var pids: std.ArrayList(i32) = .empty;
    var dir = std.Io.Dir.cwd().openDir(io, "/proc", .{ .iterate = true }) catch return error.NotReadable;
    defer dir.close(io);
    var it = dir.iterate();
    while (it.next(io) catch return error.NotListable) |e| {
        const pid = std.fmt.parseInt(i32, e.name, 10) catch continue;
        try pids.append(a, pid);
    }
    return pids.items;
}

/// The process `pid` as its stat has it, in `buf`; null for one not there.
/// The fields are counted after the last `)`, since a program's name may
/// hold one.
pub fn procStat(buf: []u8, pid: i32) ?ProcStat {
    const text = readProc(buf, pid, "stat") orelse return null;
    const open = std.mem.indexOfScalar(u8, text, '(') orelse return null;
    const close_at = std.mem.lastIndexOfScalar(u8, text, ')') orelse return null;
    if (close_at < open) return null;
    var it = std.mem.tokenizeScalar(u8, text[close_at + 1 ..], ' ');
    var ppid: ?i32 = null;
    // Field 3 (the state) is the first after `)`; the parent is 4, the start 22.
    var i: usize = 3;
    while (it.next()) |f| : (i += 1) {
        if (i == 4) ppid = std.fmt.parseInt(i32, f, 10) catch return null;
        if (i == 22) return .{ .ppid = ppid orelse return null, .start = f, .name = text[open + 1 .. close_at] };
    }
    return null;
}

/// The real uid in /proc/<pid>/status, or null.
pub fn procUid(pid: i32) ?i64 {
    var buf: [4096]u8 = undefined;
    const text = readProc(&buf, pid, "status") orelse return null;
    var lines = std.mem.splitScalar(u8, text, '\n');
    while (lines.next()) |line| if (std.mem.startsWith(u8, line, "Uid:")) {
        var it = std.mem.tokenizeAny(u8, line[4..], " \t");
        return std.fmt.parseInt(i64, it.next() orelse return null, 10) catch null;
    };
    return null;
}

/// The whole of /proc/<pid>/cmdline up to `most` bytes, its NULs spaces;
/// null for a process gone. `cut` when there was more.
pub fn procCmdline(a: std.mem.Allocator, pid: i32, most: usize, cut: *bool) error{OutOfMemory}!?[]u8 {
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
        if (out.items.len + n > most) {
            try out.appendSlice(a, chunk[0 .. most - out.items.len]);
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

// ── A process by its pidfd

/// Opens a pidfd for `pid` into `fd`. It names the process that holds the
/// pid now, whatever the pid comes to name later, so a signal through it
/// never reaches a process the kernel gave the pid to since. `.SRCH` when
/// no process holds it.
pub fn pidfdOpen(pid: i32, fd: *i32) Errno {
    const rc = linux.pidfd_open(pid, 0);
    const e = linux.errno(rc);
    if (e == .SUCCESS) fd.* = @intCast(rc);
    return e;
}

/// Sends `sig` to the process `fd` names. `.SRCH` when it has gone.
pub fn pidfdSignal(fd: i32, sig: Signal) Errno {
    return linux.errno(linux.pidfd_send_signal(fd, sig, null, 0));
}

/// Whether the process `fd` names ends within `ms`: a pidfd turns readable
/// when its process exits, a zombie too.
pub fn pidfdEnds(fd: i32, ms: i32) bool {
    var p = [1]linux.pollfd{.{ .fd = fd, .events = linux.POLL.IN, .revents = 0 }};
    const rc = linux.poll(&p, 1, ms);
    return linux.errno(rc) == .SUCCESS and rc == 1;
}

/// Closes a file descriptor.
pub fn close(fd: i32) void {
    _ = linux.close(fd);
}

// ── `stdout_secret`'s file

/// Makes a new file at `path` for writing into `fd`, mode 0600 whatever the
/// umask. One already there, a link among them, is refused (`.EXIST`), so
/// a secret is never written through a file someone else made.
pub fn createPrivate(path: [*:0]const u8, fd: *i32) Errno {
    const rc = linux.open(path, .{ .ACCMODE = .WRONLY, .CREAT = true, .EXCL = true, .CLOEXEC = true, .NOFOLLOW = true }, 0o600);
    const e = linux.errno(rc);
    if (e != .SUCCESS) return e;
    fd.* = @intCast(rc);
    _ = linux.fchmod(fd.*, 0o600);
    return .SUCCESS;
}

/// The size of the file `fd` names into `size`, by seeking to its end.
pub fn fileSize(fd: i32, size: *u64) Errno {
    const rc = linux.lseek(fd, 0, linux.SEEK.END);
    const e = linux.errno(rc);
    if (e == .SUCCESS) size.* = rc;
    return e;
}

/// Writes what `fd` holds to the disk.
pub fn sync(fd: i32) Errno {
    return linux.errno(linux.fsync(fd));
}

/// Renames `from` to `to` in one step. Unless `replace`, a `to` already
/// there is refused (`.EXIST`) rather than replaced, a file made while
/// the program ran among them.
pub fn renameOver(from: [*:0]const u8, to: [*:0]const u8, replace: bool) Errno {
    return linux.errno(linux.renameat2(linux.AT.FDCWD, from, linux.AT.FDCWD, to, .{ .NOREPLACE = !replace }));
}

/// Removes a file, saying nothing if it cannot.
pub fn unlink(path: [*:0]const u8) void {
    _ = linux.unlink(path);
}
