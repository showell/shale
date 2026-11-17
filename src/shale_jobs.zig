//! Long jobs: programs that outlive the `shale` that started them.
//!
//! `launch` double-forks a reaper out of the caller's process tree, so stopping
//! the `shale` that waits on a job never kills the job. The first child makes
//! its own session, forks the reaper and exits; the reaper, re-parented to
//! init, execs `shale --reap`. The reaper (`reapMain`) starts the job in its
//! own session with stdin `/dev/null` and stdout and stderr to its log, writes
//! the job's record, waits for it, ends what it left running in its group
//! (TERM, a grace, KILL, as `run` does), and writes how it ended. The record,
//! `<log>.job`, is `key=value` lines (`shale_job_record.zig`), each write a new
//! file renamed over the old.

const std = @import("std");
const linux = std.os.linux;
const sleepMs = @import("shale_os_linux.zig").sleepMs;
const job_record = @import("shale_job_record.zig");
const Record = job_record.Record;

/// A process's raw start time: field 22 of `/proc/<pid>/stat`, counted after
/// the last `)`, since a program's name may contain one. Returns `null` if
/// there is no such process.
pub fn startOf(buf: []u8, pid: i32) ?[]const u8 {
    var path: [64]u8 = undefined;
    const p = std.mem.printSentinel(&path, "/proc/{d}/stat", .{pid}, 0) catch return null;
    const fd = linux.open(p, .{ .ACCMODE = .RDONLY }, 0);
    if (linux.errno(fd) != .SUCCESS) return null;
    defer _ = linux.close(@intCast(fd));
    const n = linux.read(@intCast(fd), buf.ptr, buf.len);
    if (linux.errno(n) != .SUCCESS) return null;
    const text = buf[0..n];
    const close = std.mem.lastIndexOfScalar(u8, text, ')') orelse return null;
    var it = std.mem.tokenizeScalar(u8, text[close + 1 ..], ' ');
    // Field 3 (the state) is the first after `)`; the start time is 22.
    var i: usize = 3;
    while (it.next()) |f| : (i += 1) if (i == 22) return f;
    return null;
}

/// Whether the reaper the record names is alive. While it is, a job that is
/// gone still has its end to be written.
pub fn reaperAlive(r: Record) bool {
    if (r.reaper <= 0) return false;
    var buf: [1024]u8 = undefined;
    const s = startOf(&buf, r.reaper) orelse return false;
    return std.mem.eql(u8, s, r.reaper_start);
}

/// Signals the group `pgid`; whether the kernel took it. A pgid below 2 is
/// refused before any `kill`.
fn signalGroup(pgid: i32, sig: u32) bool {
    // DRAGON: the process boundary. A pgid comes from a record's text, and
    // `kill(-1, sig)` signals every process the user may signal, `kill(0,
    // sig)` this process's own group.
    // REMEDY: the reaper's fork gives a leader's pid, never 0 or 1, so a pgid
    // below 2 is a forged or broken record's, and is refused here.
    if (pgid < 2) return false;
    const rc = linux.syscall2(.kill, @as(usize, @bitCast(@as(isize, -pgid))), sig);
    return linux.errno(rc) == .SUCCESS;
}

/// Whether a job is alive: its leader, while it is there, by the record's
/// start time; once it is gone, its group, if a process of it lives on. The
/// leader is asked first because it makes its group (`setsid`) only once it
/// runs, and the reaper writes the record as soon as it has forked it: a job
/// asked about just after `job_start` may have no group yet.
pub fn alive(r: Record) bool {
    var buf: [1024]u8 = undefined;
    if (startOf(&buf, r.pid)) |s| return std.mem.eql(u8, s, r.start);
    return signalGroup(r.pgid, 0);
}

/// Sends TERM to a job's group, waits up to `grace_ms`, then sends KILL.
/// Returns whether the job was alive. The start-time check narrows the race
/// with a reused pid; it does not close it.
pub fn stop(r: Record, grace_ms: u64) bool {
    if (!alive(r)) return false;
    _ = signalGroup(r.pgid, @backingInt(linux.SIG.TERM));
    // A leader that has not made its group yet is not reached by it.
    signalLeader(r, .TERM);
    // Gone is `alive`'s word, the leader's and the group's alike: the
    // group alone may not be made yet.
    var waited: u64 = 0;
    while (waited < grace_ms) : (waited += 50) {
        if (!alive(r)) return true;
        sleepMs(50);
    }
    _ = signalGroup(r.pgid, @backingInt(linux.SIG.KILL));
    signalLeader(r, .KILL);
    // The reaper reaps the leader; wait a little for it and the group to go.
    waited = 0;
    while (waited < 2000 and alive(r)) : (waited += 20) sleepMs(20);
    return true;
}

/// Signals the job's leader, only while `r.pid` is still it by its start
/// time: a pid the kernel gave to another process since is never signalled.
fn signalLeader(r: Record, sig: linux.SIG) void {
    var buf: [1024]u8 = undefined;
    const s = startOf(&buf, r.pid) orelse return;
    if (std.mem.eql(u8, s, r.start)) _ = linux.kill(r.pid, sig);
}

/// Finds argv[0] as `execve` would: a path (relative to `cwd`), or along PATH.
/// Returns `null` if nothing runnable is there.
pub fn find(a: std.mem.Allocator, argv0: []const u8, env: []const []const u8, cwd: []const u8) !?[]const u8 {
    if (std.mem.indexOfScalar(u8, argv0, '/') != null) {
        const p = if (std.fs.path.isAbsolute(argv0)) argv0 else try std.fmt.allocPrint(a, "{s}/{s}", .{ cwd, argv0 });
        return if (runnable(a, p)) p else null;
    }
    var path: []const u8 = "/usr/local/bin:/usr/bin:/bin";
    for (env) |e| if (std.mem.startsWith(u8, e, "PATH=")) {
        path = e["PATH=".len..];
    };
    var it = std.mem.splitScalar(u8, path, ':');
    while (it.next()) |dir| {
        if (dir.len == 0) continue;
        const p = try std.fmt.allocPrint(a, "{s}/{s}", .{ dir, argv0 });
        if (runnable(a, p)) return p;
    }
    return null;
}

fn runnable(a: std.mem.Allocator, p: []const u8) bool {
    const z = a.dupeSentinel(u8, p, 0) catch return false;
    return linux.errno(linux.access(z, 1)) == .SUCCESS;
}

fn zList(a: std.mem.Allocator, items: []const []const u8) ![*:null]const ?[*:0]const u8 {
    const out = try a.allocSentinel(?[*:0]const u8, items.len, null);
    for (items, out) |s, *o| o.* = (try a.dupeSentinel(u8, s, 0)).ptr;
    return out.ptr;
}

pub const LaunchError = error{ OutOfMemory, Spawn, NoRecord };

/// Double-forks `reaper --reap <record> <log> <cwd> <program> -- argv...` out
/// of this process's tree with `env`, and returns the job's record once the
/// reaper signals it is written by a byte on its fd 3, a pipe. End of file
/// with no byte means the reaper died first (`NoRecord`). A slow reaper is
/// only slow, so there is no deadline.
pub fn launch(a: std.mem.Allocator, io: std.Io, reaper: []const u8, record: []const u8, log: []const u8, cwd: []const u8, program: []const u8, argv: []const []const u8, env: []const []const u8) LaunchError!Record {
    var args: std.ArrayList([]const u8) = .empty;
    try args.appendSlice(a, &.{ reaper, "--reap", record, log, cwd, program, "--" });
    try args.appendSlice(a, argv);
    const zargv = try zList(a, args.items);
    const zenv = try zList(a, env);
    const zexe = try a.dupeSentinel(u8, reaper, 0);
    // An old record would be read as the new one's.
    std.Io.Dir.cwd().deleteFile(io, record) catch {};
    var started: [2]i32 = undefined;
    if (linux.errno(linux.pipe2(&started, .{ .CLOEXEC = true })) != .SUCCESS) return error.Spawn;
    defer _ = linux.close(started[0]);
    const first = linux.fork();
    if (linux.errno(first) != .SUCCESS) {
        _ = linux.close(started[1]);
        return error.Spawn;
    }
    if (first == 0) {
        _ = linux.setsid();
        const second = linux.fork();
        if (second == 0) {
            // The started pipe becomes fd 3 and survives the exec: dup2's copy
            // has no CLOEXEC, and an end already at 3 has the flag cleared.
            if (started[1] == reaper_started_fd) {
                _ = linux.fcntl(reaper_started_fd, linux.F.SETFD, 0);
            } else _ = linux.dup2(started[1], reaper_started_fd);
            _ = linux.close_range(reaper_started_fd + 1, std.math.maxInt(i32), .{ .UNSHARE = false, .CLOEXEC = false });
            // CLOEXEC, so the copy beside 0 to 2 never reaches the reaper.
            const devnull: i32 = @intCast(linux.open("/dev/null", .{ .ACCMODE = .RDWR, .CLOEXEC = true }, 0));
            _ = linux.dup2(devnull, 0);
            _ = linux.dup2(devnull, 1);
            _ = linux.dup2(devnull, 2);
            var none = linux.sigemptyset();
            _ = linux.sigprocmask(linux.SIG.SETMASK, &none, null);
            _ = linux.execve(zexe, zargv, zenv);
            linux.exit_group(127);
        }
        linux.exit_group(0);
    }
    _ = linux.close(started[1]);
    var status: i32 = 0;
    while (true) {
        const rc = linux.waitpid(@intCast(first), &status, 0);
        if (linux.errno(rc) != .INTR) break;
    }
    var byte: [1]u8 = undefined;
    while (true) {
        const rc = linux.read(started[0], &byte, 1);
        if (linux.errno(rc) != .INTR) break;
    }
    // The reaper wrote its byte or exited; either way the record is final.
    const text = std.Io.Dir.cwd().readFileAlloc(io, record, a, .limited(64 * 1024)) catch return error.NoRecord;
    return job_record.parseRecord(text) orelse error.NoRecord;
}

/// The reaper's end of `launch`'s started pipe.
const reaper_started_fd: i32 = 3;

fn nowSeconds() i64 {
    var ts: linux.timespec = undefined;
    _ = linux.clock_gettime(.REALTIME, &ts);
    return @intCast(ts.sec);
}

/// Writes the record to `tmp` and renames it over `record`, so a reader never
/// sees a partial one. Returns whether it succeeded.
fn writeRecord(record: [*:0]const u8, tmp: [*:0]const u8, r: Record) bool {
    var buf: [8192]u8 = undefined;
    const text = job_record.formatRecord(&buf, r) orelse return false;
    // DRAGON: the temp file is a name in the log's folder, which another
    // process may write: a link planted there would be followed, and the
    // file it names truncated and written.
    // REMEDY: what is there is unlinked (a link itself, never what it
    // names), and the file is made new, `EXCL` and `NOFOLLOW`, so one planted
    // between the two is refused, never followed.
    _ = linux.unlink(tmp);
    const fd = linux.open(tmp, .{ .ACCMODE = .WRONLY, .CREAT = true, .EXCL = true, .NOFOLLOW = true, .CLOEXEC = true }, 0o644);
    if (linux.errno(fd) != .SUCCESS) return false;
    const w = linux.write(@intCast(fd), text.ptr, text.len);
    _ = linux.close(@intCast(fd));
    if (linux.errno(w) != .SUCCESS or w != text.len or linux.errno(linux.rename(tmp, record)) != .SUCCESS) {
        _ = linux.unlink(tmp);
        return false;
    }
    return true;
}

/// The reaper, `shale --reap <record> <log> <cwd> <program> -- argv...`, whose
/// environment is the job's: starts the job, writes its record, waits for it,
/// ends its group and writes how it ended, so a job's end is written only once
/// its group is empty. The exit code says only whether it could.
pub fn reapMain(a: std.mem.Allocator, args: []const []const u8, env: []const []const u8) u8 {
    if (args.len < 6 or !std.mem.eql(u8, args[4], "--")) return 2;
    const record = a.dupeSentinel(u8, args[0], 0) catch return 2;
    // A temp file of its own, so a reaper ending as another starts never shares one.
    const tmp = std.fmt.allocPrintSentinel(a, "{s}.{d}.tmp", .{ args[0], linux.getpid() }, 0) catch return 2;
    const log = a.dupeSentinel(u8, args[1], 0) catch return 2;
    const cwd = a.dupeSentinel(u8, args[2], 0) catch return 2;
    const program = a.dupeSentinel(u8, args[3], 0) catch return 2;
    const argv = args[5..];
    const zargv = zList(a, argv) catch return 2;
    const zenv = zList(a, env) catch return 2;
    const shown = std.mem.join(a, " ", argv) catch return 2;
    // The started pipe must not leak into the job.
    _ = linux.fcntl(reaper_started_fd, linux.F.SETFD, linux.FD_CLOEXEC);
    var own_buf: [1024]u8 = undefined;
    const reaper_start = startOf(&own_buf, linux.getpid()) orelse return 2;
    // CLOEXEC, so the job has its log as 1 and 2 alone.
    const log_fd = linux.open(log, .{ .ACCMODE = .WRONLY, .CREAT = true, .TRUNC = true, .CLOEXEC = true }, 0o644);
    if (linux.errno(log_fd) != .SUCCESS) return 2;
    const pid = linux.fork();
    if (linux.errno(pid) != .SUCCESS) return 2;
    if (pid == 0) {
        _ = linux.setsid();
        const devnull: i32 = @intCast(linux.open("/dev/null", .{ .ACCMODE = .RDONLY, .CLOEXEC = true }, 0));
        _ = linux.dup2(devnull, 0);
        _ = linux.dup2(@intCast(log_fd), 1);
        _ = linux.dup2(@intCast(log_fd), 2);
        if (linux.errno(linux.chdir(cwd)) != .SUCCESS) linux.exit_group(126);
        _ = linux.execve(program, zargv, zenv);
        linux.exit_group(127);
    }
    _ = linux.close(@intCast(log_fd));
    var r: Record = .{ .pid = @intCast(pid), .pgid = @intCast(pid), .started = nowSeconds(), .argv = shown, .reaper = linux.getpid(), .reaper_start = reaper_start };
    var stat_buf: [1024]u8 = undefined;
    // The child's stat may take a moment to appear.
    var tries: usize = 0;
    while (tries < 100) : (tries += 1) {
        if (startOf(&stat_buf, r.pid)) |s| {
            r.start = s;
            break;
        }
        sleepMs(1);
    }
    // The record is a job's only handle, so a job whose first record cannot
    // be written is killed, and `launch` reports `NoRecord`.
    if (!writeRecord(record, tmp, r)) {
        // Kill the child too, in case it has not made its group yet.
        _ = linux.kill(-r.pgid, .KILL);
        _ = linux.kill(@intCast(pid), .KILL);
        var st: i32 = 0;
        _ = linux.waitpid(@intCast(pid), &st, 0);
        return 2;
    }
    _ = linux.write(reaper_started_fd, "s", 1);
    _ = linux.close(reaper_started_fd);
    // The leader's exit is seen first and left unreaped (`NOWAIT`): its
    // zombie keeps its pid the group's id, so the TERM to what it left
    // running in its group can reach no stranger.
    var info: linux.siginfo_t = undefined;
    while (true) {
        const rc = linux.waitid(.PID, @intCast(pid), &info, linux.W.EXITED | linux.W.NOWAIT, null);
        if (linux.errno(rc) != .INTR) break;
    }
    _ = signalGroup(r.pgid, @backingInt(linux.SIG.TERM));
    // waitpid fills an i32; `W` reads a u32.
    var raw: i32 = 0;
    while (true) {
        const rc = linux.waitpid(@intCast(pid), &raw, 0);
        if (linux.errno(rc) != .INTR) break;
    }
    const status: u32 = @bitCast(raw);
    if (linux.W.IFEXITED(status)) r.exit = linux.W.EXITSTATUS(status) else r.signal = @backingInt(linux.W.TERMSIG(status));
    endGroup(r.pgid, group_grace_ms);
    r.ended = nowSeconds();
    return if (writeRecord(record, tmp, r)) 0 else 2;
}

/// How long what a job's leader left in its group has, after the TERM at the
/// leader's exit, before it is KILLed: `job_stop`'s and `run`'s grace.
const group_grace_ms: u64 = 2000;

/// Whether no process is left in the group `pgid`. Asked only once its
/// leader is reaped: until then the leader's zombie is a member.
fn groupGone(pgid: i32) bool {
    const rc = linux.syscall2(.kill, @as(usize, @bitCast(@as(isize, -pgid))), 0);
    return linux.errno(rc) == .SRCH;
}

/// Waits, once a job's leader is reaped and its group has had its TERM, for
/// the group to empty: KILL at `grace_ms`, then up to `grace_ms` more for a
/// process stuck in the kernel, which is then left, so the job's end is
/// still written. A process that left the group (`setsid`) is not waited for.
fn endGroup(pgid: i32, grace_ms: u64) void {
    // Checked every 10 ms, as `run` checks a group outliving its leader.
    const tick: u64 = 10;
    var waited: u64 = 0;
    var killed = false;
    while (!groupGone(pgid)) : (waited += tick) {
        if (waited >= 2 * grace_ms) return;
        // DRAGON: once the leader is reaped, its pid stays the group's id
        // only while a member is left; a group that empties frees it, and a
        // new group given the id would be signalled.
        // REMEDY: the KILL goes only to a group `groupGone` found not empty
        // a moment before, whose members hold the id; a group that empties
        // in that moment is the one window left, as in `run`. A leader
        // KILLed before its `setsid` never made the group, so its id is free
        // from the reap, and the same check guards it.
        if (!killed and waited >= grace_ms) {
            _ = signalGroup(pgid, @backingInt(linux.SIG.KILL));
            killed = true;
        }
        sleepMs(tick);
    }
}

test "jobs: a reaper gone before its record is NoRecord at once, the pipe's end its word" {
    const io = std.testing.io;
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    const a = arena.allocator();
    const dir = try tmp.dir.realPathFileAlloc(io, ".", a);
    const record = try std.fmt.allocPrint(a, "{s}/x.log.job", .{dir});
    const log = try std.fmt.allocPrint(a, "{s}/x.log", .{dir});
    var t0: linux.timespec = undefined;
    _ = linux.clock_gettime(.MONOTONIC, &t0);
    // `true` as the reaper: it writes nothing and ends.
    try std.testing.expectError(error.NoRecord, launch(a, io, "/bin/true", record, log, dir, "/bin/true", &.{"true"}, &.{}));
    var t1: linux.timespec = undefined;
    _ = linux.clock_gettime(.MONOTONIC, &t1);
    // No deadline was waited out.
    try std.testing.expect(t1.sec - t0.sec < 3);
}

test "jobs: a pgid below 2 is refused before any kill; a real group is signalled" {
    // Signal 0 only asks, so the test signals nobody. Unguarded, `kill(-1,
    // 0)` and `kill(0, 0)` both succeed.
    try std.testing.expect(!signalGroup(1, 0));
    try std.testing.expect(!signalGroup(0, 0));
    try std.testing.expect(!signalGroup(-7, 0));
    // The test's own group is there to be asked.
    try std.testing.expect(signalGroup(@intCast(linux.syscall1(.getpgid, 0)), 0));
}

test "jobs: a link planted at the record's temp file is replaced, never followed" {
    const io = std.testing.io;
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    const a = arena.allocator();
    const dir = try tmp.dir.realPathFileAlloc(io, ".", a);
    const record = try std.fmt.allocPrintSentinel(a, "{s}/x.log.job", .{dir}, 0);
    const temp = try std.fmt.allocPrintSentinel(a, "{s}/x.log.job.7.tmp", .{dir}, 0);
    const victim = try std.fmt.allocPrintSentinel(a, "{s}/victim", .{dir}, 0);
    try tmp.dir.writeFile(io, .{ .sub_path = "victim", .data = "keep\n" });
    try std.testing.expectEqual(linux.E.SUCCESS, linux.errno(linux.symlink(victim, temp)));
    try std.testing.expect(writeRecord(record, temp, .{ .pid = 42, .pgid = 42, .start = "1", .started = 9, .argv = "x", .reaper = 1, .reaper_start = "5" }));
    // The file the link named is as it was.
    try std.testing.expectEqualStrings("keep\n", try tmp.dir.readFileAlloc(io, "victim", a, .limited(64)));
    // The record is a file of its own, the record's text, and the temp gone.
    var st: linux.Statx = undefined;
    try std.testing.expectEqual(linux.E.SUCCESS, linux.errno(linux.statx(linux.AT.FDCWD, record, linux.AT.SYMLINK_NOFOLLOW, .{ .TYPE = true }, &st)));
    try std.testing.expect(st.mode & linux.S.IFMT == linux.S.IFREG);
    const text = try tmp.dir.readFileAlloc(io, "x.log.job", a, .limited(1024));
    try std.testing.expect(std.mem.startsWith(u8, text, "pid=42\npgid=42\n"));
    try std.testing.expectError(error.FileNotFound, tmp.dir.statFile(io, "x.log.job.7.tmp", .{}));
}

test "jobs: a process's start time is its stat's 22nd field; none for a process not there" {
    var buf: [1024]u8 = undefined;
    try std.testing.expect(startOf(&buf, linux.getpid()) != null);
    try std.testing.expect(startOf(&buf, 0x7ffffff0) == null);
}
