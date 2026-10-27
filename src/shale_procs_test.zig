//! Tests `procs` and `proc_stop`: the processes a script may find and stop,
//! never the run's own or one above it (`shale_host_procs.zig`).

const std = @import("std");
const testing = std.testing;
const linux = std.os.linux;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");

fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{ host_linux.linux_prelude, host_linux.git_prelude };
}

fn once(t: *T, src: []const u8) ![]const u8 {
    var r = t.runOnce(src, .atomic);
    defer r.deinit();
    if (r.stop) |s| {
        std.debug.print("{s}\n  stopped {d}:{d} {s}: {s}\n", .{ src, s.line, s.column, @tagName(s.class), s.message });
        return error.TestUnexpectedResult;
    }
    return t.arena.allocator().dupe(u8, r.output);
}

test "procs: never this process or one above it, so no filter of its list names the caller (the pkill -f pitfall)" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    // A filter that every process passes, as `pkill -f` matching its own
    // shell's command line would: this run and its parent are not there.
    const src = try std.fmt.allocPrint(a,
        \\local seen = 0
        \\local mine = 0
        \\for p in procs() do
        \\  seen = seen + 1
        \\  if p.pid == {d} or p.pid == {d} then mine = mine + 1 end
        \\end
        \\print(seen > 0, mine)
    , .{ linux.getpid(), linux.getppid() });
    try testing.expectEqualStrings("true\t0\n", try once(&t, src));
}

test "procs: proc_stop refuses this process and those above it, whatever it is handed" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    for ([_]i32{ linux.getpid(), linux.getppid() }) |pid| {
        var r = t.runOnce(try std.fmt.allocPrint(a, "proc_stop({{pid = {d}, start = \"0\"}})", .{pid}), .atomic);
        defer r.deinit();
        const s = r.stop orelse return error.TestUnexpectedResult;
        try testing.expect(std.mem.indexOf(u8, s.message, "this run or one above it") != null);
    }
}

test "procs: proc_stop stops the process it names by pid and start, and a stale start is left alone" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try testing.expectEqualStrings("1\tfalse\ttrue\ttrue\n", try once(&t,
        \\local j = job_start({"shale-fixture", "sleep", "20000"}, {log = "s.log"})
        \\local found = {}
        \\for i = 1, 50 do
        \\  for p in procs() do
        \\    if p.pid == j.pid and find(p.args, "sleep 20000") ~= nil then push(found, p) end
        \\  end
        \\  if #found > 0 then break end
        \\  sleep(20)
        \\end
        \\local stale = proc_stop({pid = found[1].pid, start = "1"})
        \\local ran = proc_stop(found[1])
        \\local w = job_wait(j, {timeout_ms = 10000})
        \\print(#found, stale, ran, w.ended == "exited")
    ));
}

test "procs: a process the run may not signal stops proc_stop, named, never a success with it still there (the box's review)" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    if (linux.getuid() != 0) {
        // Not root: a process of root's (there is always one beside pid 1)
        // answers EPERM.
        var r = t.runOnce(
            \\for p in procs() do
            \\  if p.uid == 0 and p.pid > 1 then proc_stop(p) end
            \\end
        , .atomic);
        defer r.deinit();
        const s = r.stop orelse return error.TestUnexpectedResult;
        errdefer std.debug.print("{s}\n", .{s.message});
        try testing.expect(std.mem.indexOf(u8, s.message, "refused TERM") != null);
        return;
    }
    // Root: the target is root's, and the run that stops it is nobody's.
    const got = try once(&t,
        \\local j = job_start({"shale-fixture", "sleep", "20000"}, {log = "e.log"})
        \\for i = 1, 50 do
        \\  for p in procs() do
        \\    if p.pid == j.pid and find(p.args, "sleep 20000") ~= nil then print(p.pid .. " " .. p.start) end
        \\  end
        \\  sleep(20)
        \\  if exists("e.log.job") then break end
        \\end
    );
    var it = std.mem.tokenizeAny(u8, got, " \n");
    const pid = it.next() orelse return error.TestUnexpectedResult;
    const start = it.next() orelse return error.TestUnexpectedResult;
    const src = try std.fmt.allocPrint(a, "print(proc_stop({{pid = {s}, start = \"{s}\"}}))", .{ pid, start });
    // The build's shale, as an absolute path (the reaper's).
    const r = try std.process.run(a, t.threaded.io(), .{ .argv = &.{ "setpriv", "--reuid=65534", "--regid=65534", "--clear-groups", "env", "-i", "HOME=/tmp", "SHALE_PRELUDE=", "SHALE_MOUNTS=/dev/null", t.host.reaper.?, "--root", "/tmp", "-e", src } });
    errdefer std.debug.print("{s}{s}\n", .{ r.stdout, r.stderr });
    try testing.expectEqual(@as(u8, 1), r.term.exited);
    try testing.expect(std.mem.indexOf(u8, r.stderr, "refused TERM") != null);
    // Still there, and stopped now by one that may.
    try testing.expectEqualStrings("true\n", try once(&t, "print(job_stop(\"e.log.job\"))"));
}

test "procs: a command line is read whole, and one past args_most says it was cut" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    // Past one read of 4096 bytes: read to its end.
    try testing.expectEqualStrings("true\tfalse\ttrue\n", try once(&t,
        \\local long = ""
        \\for i = 1, 600 do long = long .. "0123456789" end
        \\local j = job_start({"shale-fixture", "sleep", "20000", long}, {log = "l.log"})
        \\local whole = false
        \\local cut = true
        \\for i = 1, 50 do
        \\  for p in procs() do
        \\    if p.pid == j.pid and find(p.args, "sleep 20000") ~= nil then
        \\      whole = #p.args == #"shale-fixture sleep 20000 " + #long
        \\      cut = get(p, "args_cut", false)
        \\    end
        \\  end
        \\  if whole then break end
        \\  sleep(20)
        \\end
        \\print(whole, cut, job_stop(j))
    ));
    // Past args_most (more than a job's record holds, so spawned here): said.
    const fixture = @import("shale_test_options").fixture;
    var argv: std.ArrayList([]const u8) = .empty;
    try argv.appendSlice(a, &.{ fixture, "sleep", "20000" });
    const part: []const u8 = &@as([120 * 1024]u8, @splat('x'));
    for (0..9) |_| try argv.append(a, part);
    var child = try std.process.spawn(t.threaded.io(), .{ .argv = argv.items, .stdin = .ignore, .stdout = .ignore, .stderr = .ignore });
    defer child.kill(t.threaded.io());
    const src = try std.fmt.allocPrint(a,
        \\local cut = nil
        \\for i = 1, 50 do
        \\  for p in procs() do
        \\    if p.pid == {d} and find(p.args, "sleep 20000") ~= nil then cut = get(p, "args_cut", false) end
        \\  end
        \\  if cut then break end
        \\  sleep(20)
        \\end
        \\print(cut)
    , .{child.id.?});
    try testing.expectEqualStrings("true\n", try once(&t, src));
}

test "procs: the decoys the pattern also names survive: one whose argv holds the target's command line, one whose name is a prefix of the target's" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    // The second decoy is a shale of its own: `shale` is a prefix of
    // `shale-fixture`, as `zig` is of `zig-server`.
    const src = try std.fmt.allocPrint(a,
        \\local target = job_start({{"shale-fixture", "sleep", "20000"}}, {{log = "t.log"}})
        \\local decoy = job_start({{"shale-fixture", "sleep", "20000", "shale-fixture sleep 20000"}}, {{log = "d.log"}})
        \\local prefix = job_start({{"{s}", "--root", "/tmp", "-e", "sleep(20000)"}}, {{log = "p.log", want = "a shale of its own, the decoy"}})
        \\local stopped = false
        \\for i = 1, 50 do
        \\  for p in procs() do
        \\    -- What `pkill -f "shale-fixture sleep 20000"` matches is both
        \\    -- fixtures, and `pkill shale` all three: the one meant is the one
        \\    -- whose name and command line are exactly its own.
        \\    if p.args == "shale-fixture sleep 20000" and p.name == "shale-fixture" then stopped = proc_stop(p) end
        \\  end
        \\  if stopped then break end
        \\  sleep(20)
        \\end
        \\local w = job_wait(target, {{timeout_ms = 10000}})
        \\print(stopped, w.ended == "exited", job_alive(decoy), job_alive(prefix), job_stop(decoy), job_stop(prefix))
    , .{t.host.reaper.?});
    try testing.expectEqualStrings("true\ttrue\ttrue\ttrue\ttrue\ttrue\n", try once(&t, src));
}
