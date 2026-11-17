//! The program `run`'s tests spawn, built by the build so the tests never
//! depend on /bin/sh. Each verb is one behaviour a test needs, and nothing
//! else:
//!
//!     shale-fixture lines N [TEXT]   N lines, "TEXT i" (TEXT "line")
//!     shale-fixture exit CODE        exits CODE, printing nothing
//!     shale-fixture env NAME         the variable NAME's value, or "(unset)"
//!     shale-fixture cat              stdin to stdout
//!     shale-fixture sleep MS         sleeps, then prints "woke"
//!     shale-fixture pwd              the working directory
//!     shale-fixture stderr TEXT      TEXT on stderr
//!     shale-fixture both TEXT CODE   TEXT on stdout as given, "on stderr"
//!                                    on stderr, then exits CODE
//!     shale-fixture kill-self        ends by SIGKILL, its own
//!     shale-fixture ignore-term MS   TERM ignored, then sleeps
//!     shale-fixture pid-sleep MS     "pid N", its own, then sleeps
//!     shale-fixture spill N          N bytes of 'x' with no newline
//!     shale-fixture chunks N SIZE    N bytes of 100-byte lines, written
//!                                    SIZE at a time, 1 ms apart
//!     shale-fixture burn MS          spins until it has used MS of CPU
//!     shale-fixture burn-child MS    a child burns MS; this waits for it
//!     shale-fixture grandchild-exit MS   "grandchild N", a child's pid,
//!                                    which sleeps MS holding the output;
//!                                    this exits 0 at once
//!     shale-fixture grandchild-late MS   the same, but the child ignores
//!                                    TERM and prints "late" once this has
//!                                    exited, then sleeps MS
//!     shale-fixture grandchild-escape MS the same as grandchild-exit, but
//!                                    the child leaves the group for a
//!                                    session of its own
//!     shale-fixture grandchild-signal    "grandchild N"; the child ignores
//!                                    TERM and, once this has exited, sends
//!                                    TERM to this one's parent, then exits
//!     shale-fixture --serve          a zig-ast gone wrong, as the host starts
//!                                    one: its header, then each request
//!                                    answered by its command: `decls` a
//!                                    line no host knows, `tokens` a `bad`,
//!                                    `imports` an end, with words on stderr

const std = @import("std");

/// Spins on the process's own CPU clock, so a busy box makes it take longer
/// but never makes it burn less.
fn burn(ms: u64) void {
    const linux = std.os.linux;
    var ts: linux.timespec = undefined;
    while (true) {
        _ = linux.clock_gettime(.PROCESS_CPUTIME_ID, &ts);
        if (@as(u64, @intCast(ts.sec)) * 1000 + @as(u64, @intCast(ts.nsec)) / 1_000_000 >= ms) return;
    }
}

pub fn main(init: std.process.Init) !u8 {
    const io = init.io;
    const argv = try init.minimal.args.toSlice(init.arena.allocator());
    var buf: [4096]u8 = undefined;
    var ow = std.Io.File.stdout().writer(io, &buf);
    const w = &ow.interface;
    defer w.flush() catch {};
    if (argv.len < 2) return 2;
    const verb = argv[1];
    const arg = if (argv.len > 2) argv[2] else "";
    if (std.mem.eql(u8, verb, "--serve")) {
        try w.writeAll("zig-ast serve 1 fixture\n");
        try w.flush();
        var in_buf: [64 * 1024]u8 = undefined;
        var r = std.Io.File.stdin().readerStreaming(io, &in_buf);
        while (try r.interface.takeDelimiter('\n')) |len| {
            const body = try r.interface.readAllocAll(init.arena.allocator(), try std.fmt.parseInt(usize, len, 10));
            const answer = if (std.mem.startsWith(u8, body, "decls\n")) "bogus\t1\n" else if (std.mem.startsWith(u8, body, "tokens\n")) "bad\tBadRequest\n" else {
                var eb: [256]u8 = undefined;
                var ew = std.Io.File.stderr().writer(io, &eb);
                try ew.interface.writeAll("fixture: no answer to this\n");
                try ew.interface.flush();
                return 3;
            };
            try w.print("{d}\n{s}", .{ answer.len, answer });
            try w.flush();
        }
    } else if (std.mem.eql(u8, verb, "lines")) {
        const n = try std.fmt.parseInt(usize, arg, 10);
        const text = if (argv.len > 3) argv[3] else "line";
        for (1..n + 1) |i| try w.print("{s} {d}\n", .{ text, i });
    } else if (std.mem.eql(u8, verb, "exit")) {
        return try std.fmt.parseInt(u8, arg, 10);
    } else if (std.mem.eql(u8, verb, "env")) {
        try w.print("{s}\n", .{init.environ_map.get(arg) orelse "(unset)"});
    } else if (std.mem.eql(u8, verb, "cat")) {
        var in_buf: [4096]u8 = undefined;
        var r = std.Io.File.stdin().reader(io, &in_buf);
        _ = try r.interface.streamRemaining(w);
    } else if (std.mem.eql(u8, verb, "sleep")) {
        try io.sleep(.fromMilliseconds(try std.fmt.parseInt(i64, arg, 10)), .awake);
        try w.writeAll("woke\n");
    } else if (std.mem.eql(u8, verb, "pwd")) {
        var path: [std.fs.max_path_bytes]u8 = undefined;
        const n = std.os.linux.getcwd(&path, path.len);
        try w.print("{s}\n", .{std.mem.sliceTo(path[0..n], 0)});
    } else if (std.mem.eql(u8, verb, "stderr")) {
        var eb: [256]u8 = undefined;
        var ew = std.Io.File.stderr().writer(io, &eb);
        try ew.interface.print("{s}\n", .{arg});
        try ew.interface.flush();
    } else if (std.mem.eql(u8, verb, "both")) {
        try w.writeAll(arg);
        try w.flush();
        var eb: [256]u8 = undefined;
        var ew = std.Io.File.stderr().writer(io, &eb);
        try ew.interface.writeAll("on stderr\n");
        try ew.interface.flush();
        return try std.fmt.parseInt(u8, argv[3], 10);
    } else if (std.mem.eql(u8, verb, "pid-sleep")) {
        try w.print("pid {d}\n", .{std.os.linux.getpid()});
        try w.flush();
        try io.sleep(.fromMilliseconds(try std.fmt.parseInt(i64, arg, 10)), .awake);
    } else if (std.mem.eql(u8, verb, "kill-self")) {
        try w.flush();
        _ = std.os.linux.kill(std.os.linux.getpid(), .KILL);
    } else if (std.mem.eql(u8, verb, "ignore-term")) {
        const ignore: std.os.linux.Sigaction = .{ .handler = .{ .handler = std.os.linux.SIG.IGN }, .mask = std.os.linux.sigemptyset(), .flags = 0 };
        _ = std.os.linux.sigaction(.TERM, &ignore, null);
        try w.writeAll("ignoring\n");
        try w.flush();
        try io.sleep(.fromMilliseconds(try std.fmt.parseInt(i64, arg, 10)), .awake);
    } else if (std.mem.eql(u8, verb, "chunks")) {
        const n = try std.fmt.parseInt(usize, arg, 10);
        const size = try std.fmt.parseInt(usize, argv[3], 10);
        var i: usize = 0;
        while (i < n) : (i += 1) {
            try w.writeByte(if (i % 100 == 99) '\n' else 'x');
            if ((i + 1) % size == 0) {
                try w.flush();
                try io.sleep(.fromMilliseconds(1), .awake);
            }
        }
    } else if (std.mem.eql(u8, verb, "burn")) {
        burn(try std.fmt.parseInt(u64, arg, 10));
    } else if (std.mem.eql(u8, verb, "burn-child")) {
        const linux = std.os.linux;
        const ms = try std.fmt.parseInt(u64, arg, 10);
        const pid: linux.pid_t = @intCast(linux.fork());
        if (pid == 0) {
            burn(ms);
            linux.exit_group(0);
        }
        var status: i32 = 0;
        _ = linux.wait4(pid, &status, 0, null);
    } else if (std.mem.eql(u8, verb, "grandchild-exit") or std.mem.eql(u8, verb, "grandchild-late") or std.mem.eql(u8, verb, "grandchild-signal") or std.mem.eql(u8, verb, "grandchild-escape")) {
        const linux = std.os.linux;
        const signal = std.mem.eql(u8, verb, "grandchild-signal");
        const ms = if (signal) 0 else try std.fmt.parseInt(isize, arg, 10);
        const escape = std.mem.eql(u8, verb, "grandchild-escape");
        const late = !escape and !std.mem.eql(u8, verb, "grandchild-exit");
        const leader = linux.getpid();
        const above = linux.getppid();
        // The child closes its end once it ignores TERM, so this exits only
        // after: a TERM to the group at this exit never finds it listening.
        var ready: [2]i32 = undefined;
        if (linux.errno(linux.pipe2(&ready, .{})) != .SUCCESS) return 3;
        const pid: linux.pid_t = @intCast(linux.fork());
        if (pid == 0) {
            // Raw calls only: the child shares the parent's buffered writer.
            _ = linux.close(ready[0]);
            if (escape) _ = linux.setsid();
            if (late) {
                const ignore: linux.Sigaction = .{ .handler = .{ .handler = linux.SIG.IGN }, .mask = linux.sigemptyset(), .flags = 0 };
                _ = linux.sigaction(.TERM, &ignore, null);
            }
            _ = linux.close(ready[1]);
            if (late) {
                // Its parent's exit re-parents it.
                const tick: linux.timespec = .{ .sec = 0, .nsec = 1_000_000 };
                while (linux.getppid() == leader) _ = linux.nanosleep(&tick, null);
                if (signal) {
                    _ = linux.kill(above, .TERM);
                    linux.exit_group(0);
                }
                _ = linux.write(1, "late\n", 5);
            }
            const span: linux.timespec = .{ .sec = @divTrunc(ms, 1000), .nsec = @rem(ms, 1000) * 1_000_000 };
            _ = linux.nanosleep(&span, null);
            linux.exit_group(0);
        }
        _ = linux.close(ready[1]);
        var byte: [1]u8 = undefined;
        _ = linux.read(ready[0], &byte, 1);
        try w.print("grandchild {d}\n", .{pid});
    } else if (std.mem.eql(u8, verb, "spill")) {
        const n = try std.fmt.parseInt(usize, arg, 10);
        for (0..n) |_| try w.writeByte('x');
    } else return 2;
    return 0;
}
