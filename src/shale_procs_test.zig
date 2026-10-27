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
