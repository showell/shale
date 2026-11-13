//! Tests jobs: programs that outlive the run that started them, reaped by a
//! detached `shale --reap` (the build's own `shale`) and found again by their
//! record's path. A job starts once, so these run the VM once (`T.runOnce`);
//! the mistakes a job call can make go through the gate.

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");
const jobs = @import("shale_jobs.zig");

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

fn onceStop(t: *T, src: []const u8, class: @import("shale").script.diag.Class, says: []const u8) !void {
    var r = t.runOnce(src, .atomic);
    defer r.deinit();
    const s = r.stop orelse return error.TestUnexpectedResult;
    errdefer std.debug.print("{s}: {s}: {s}\n", .{ src, @tagName(s.class), s.message });
    try testing.expectEqual(class, s.class);
    try testing.expect(std.mem.indexOf(u8, s.message, says) != null);
}

test "jobs: a job runs to its end, its output in its log, and job_wait says how it ended" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try testing.expectEqualStrings("exited\t0\tline 1\nline 2\nline 3\n\n", try once(&t, "local j = job_start({\"shale-fixture\", \"lines\", \"3\"}, {log = \"j.log\"}) local w = job_wait(j, {timeout_ms = 10000}) print(w.ended, w.code, read(\"j.log\"))"));
    try testing.expectEqualStrings("exited\t3\tfalse\n", try once(&t, "local j = job_start({\"shale-fixture\", \"exit\", \"3\"}, {log = \"e.log\"}) local w = job_wait(j, {timeout_ms = 10000}) print(w.ended, w.code, job_alive(j))"));
    // Its record, beside its log.
    const rec = try t.file("j.log.job");
    try testing.expect(std.mem.indexOf(u8, rec, "\nexit=0\n") != null);
    try testing.expect(std.mem.startsWith(u8, rec, "pid="));
}

test "jobs: a job asked about the moment it starts is alive, its group made or not" {
    // The reaper writes the record once it has forked the job, which makes
    // its own group only when it first runs: `job_alive` asked the group
    // alone, and a job started a moment ago could read as gone. Many starts,
    // so the window is crossed.
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try testing.expectEqualStrings("20\n", try once(&t,
        \\local n = 0
        \\for i = 1, 20 do
        \\  local j = job_start({"shale-fixture", "sleep", "5000"}, {log = "quick" .. i .. ".log"})
        \\  if job_alive(j) then n = n + 1 end
        \\  job_stop(j)
        \\end
        \\print(n)
    ));
}

test "jobs: a job outlives its run; a later run finds it by its record, waits for a line, and stops it" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try testing.expectEqualStrings("true\n", try once(&t, "local j = job_start({\"shale-fixture\", \"ignore-term\", \"20000\"}, {log = \"slow.log\"}) print(j.record == \"slow.log.job\")"));
    // A second run, as a later `shale` would be.
    try testing.expectEqualStrings("found\ttrue\n", try once(&t, "local w = job_wait(\"slow.log.job\", {timeout_ms = 10000, contains = \"ignoring\"}) print(w.ended, job_alive(\"slow.log.job\"))"));
    try testing.expectEqualStrings("timeout\n", try once(&t, "print(job_wait(\"slow.log.job\", {timeout_ms = 300}).ended)"));
    // It ignores TERM: the grace, then KILL.
    try testing.expectEqualStrings("true\tfalse\tfalse\n", try once(&t, "local was = job_stop(\"slow.log.job\") print(was, job_alive(\"slow.log.job\"), job_stop(\"slow.log.job\"))"));
    try testing.expectEqualStrings("exited\tfalse\t9\n", try once(&t, "local w = job_wait(\"slow.log.job\", {timeout_ms = 5000}) print(w.ended, has_key(w, \"code\"), w.signal)"));
    try testing.expect(std.mem.indexOf(u8, try t.file("slow.log.job"), "\nsignal=9\n") != null);
}

test "jobs: a job gone while its reaper lives is still ending; gone with its reaper and no end written, it is lost" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    // No process has this id; this test's own process stands in for a
    // reaper between its wait and its last write.
    const none = 0x7ffffff0;
    var buf: [1024]u8 = undefined;
    const me = std.os.linux.getpid();
    const my_start = jobs.startOf(&buf, me).?;
    try t.put("ending.log.job", try std.fmt.allocPrint(a, "pid={d}\npgid={d}\nstart=1\nstarted=0\nargv=x\nreaper={d}\nreaper_start={s}\n", .{ none, none, me, my_start }));
    try testing.expectEqualStrings("timeout\n", try once(&t, "print(job_wait(\"ending.log.job\", {timeout_ms = 300}).ended)"));
    try t.put("lost.log.job", try std.fmt.allocPrint(a, "pid={d}\npgid={d}\nstart=1\nstarted=0\nargv=x\nreaper={d}\nreaper_start=1\n", .{ none, none, none }));
    try testing.expectEqualStrings("lost\tfalse\tfalse\n", try once(&t, "local w = job_wait(\"lost.log.job\", {timeout_ms = 5000}) print(w.ended, has_key(w, \"code\"), has_key(w, \"signal\"))"));
}

test "jobs: a job started on a log whose job or reaper still lives is refused, its record kept" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    const none = 0x7ffffff0;
    var buf: [1024]u8 = undefined;
    const me = std.os.linux.getpid();
    const my_group: i32 = @intCast(std.os.linux.syscall1(.getpgid, 0));
    const my_start = jobs.startOf(&buf, me).?;
    // Its job lives (this test's own group stands in), its reaper gone.
    const busy = try std.fmt.allocPrint(a, "pid={d}\npgid={d}\nstart={s}\nstarted=0\nargv=x\nreaper={d}\nreaper_start=1\n", .{ me, my_group, my_start, none });
    try t.put("busy.log.job", busy);
    try onceStop(&t, "job_start({\"shale-fixture\", \"exit\", \"0\"}, {log = \"busy.log\"})", .host, "a job still runs on busy.log");
    try testing.expectEqualStrings(busy, try t.file("busy.log.job"));
    // Its job gone, its reaper living: its end is still to be written.
    const ending = try std.fmt.allocPrint(a, "pid={d}\npgid={d}\nstart=1\nstarted=0\nargv=x\nreaper={d}\nreaper_start={s}\n", .{ none, none, me, my_start });
    try t.put("ending.log.job", ending);
    try onceStop(&t, "job_start({\"shale-fixture\", \"exit\", \"0\"}, {log = \"ending.log\"})", .host, "a job still runs on ending.log");
    try testing.expectEqualStrings(ending, try t.file("ending.log.job"));
}

test "jobs: a log whose job's end is written takes a new job, though its reaper is still exiting" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    const none = 0x7ffffff0;
    var buf: [1024]u8 = undefined;
    const me = std.os.linux.getpid();
    const my_start = jobs.startOf(&buf, me).?;
    // Its end written, its reaper (this test's process stands in) between
    // that last write and its exit.
    try t.put("done.log.job", try std.fmt.allocPrint(a, "pid={d}\npgid={d}\nstart=1\nstarted=0\nargv=x\nreaper={d}\nreaper_start={s}\nexit=0\n", .{ none, none, me, my_start }));
    try testing.expectEqualStrings("exited\t0\n", try once(&t, "local j = job_start({\"shale-fixture\", \"exit\", \"0\"}, {log = \"done.log\"}) local w = job_wait(j, {timeout_ms = 10000}) print(w.ended, w.code)"));
    // As a script does it: wait, then start again on the same log at once.
    try testing.expectEqualStrings("exited\nexited\nexited\n", try once(&t,
        \\for i = 1, 3 do
        \\  local j = job_start({"shale-fixture", "exit", "0"}, {log = "again.log"})
        \\  print(job_wait(j, {timeout_ms = 10000}).ended)
        \\end
    ));
}

test "jobs: waiting for a line reads only what the log gained since it last looked, so a long wait on a large log keeps to its budgets" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    // A 7 MB log, whose reads would pile up in the run's arena if each look
    // read it whole.
    // `code` and `signal` too, so a red names its case: an end with the
    // line missing (a scan's miss) or an early end (a signal, an exit).
    try testing.expectEqualStrings("found\t-\t-\n", try once(&t, "local j = job_start({\"shale-fixture\", \"lines\", \"300000\", \"a line of the log\"}, {log = \"big.log\"}) local w = job_wait(j, {timeout_ms = 10000, contains = \"log 300000\"}) print(w.ended, get(w, \"code\", \"-\"), get(w, \"signal\", \"-\"))"));
    // Across pieces: 20 bytes on both sides of where the first read
    // (a piece and the needle's length) ends, so only the overlap finds it.
    {
        const io = t.threaded.io();
        const log = try t.tmp.dir.readFileAlloc(io, "big.log", t.arena.allocator(), .limited(16 << 20));
        const cut = @import("shale_host_jobs.zig").Scan.piece + 40;
        const needle = log[cut - 20 .. cut + 20];
        try testing.expect(std.mem.indexOfScalar(u8, needle, '"') == null);
        const src = try std.fmt.allocPrint(t.arena.allocator(), "print(job_wait(\"big.log.job\", {{timeout_ms = 1000, contains = \"{f}\"}}).ended)", .{std.zig.fmtString(needle)});
        try testing.expectEqualStrings("found\n", try once(&t, src));
    }
    // 4 MB growing over about 4 seconds. Read whole at each look, the looks
    // would cost more than this budget; read as it grows, about 262,000 steps.
    t.budgets.steps = 400_000;
    try testing.expectEqualStrings("exited\n", try once(&t, "local j = job_start({\"shale-fixture\", \"chunks\", \"4000000\", \"1000\"}, {log = \"slow.log\"}) print(job_wait(j, {timeout_ms = 60000, contains = \"never printed\"}).ended)"));
}

test "jobs: a log whose record cannot be read is refused by name, never taken for no job" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.tmp.dir.createDirPath(t.threaded.io(), "odd.log.job");
    try onceStop(&t, "job_start({\"shale-fixture\", \"exit\", \"0\"}, {log = \"odd.log\"})", .store, "odd.log.job cannot be read");
}

test "jobs: a reaper that cannot write the job's first record ends the job, so nothing runs that no record names" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    const io = t.threaded.io();
    // A folder at the record's path makes its rename fail. `job_start`
    // refuses this before the reaper; this tests the reaper's own guard.
    try t.tmp.dir.createDirPath(io, "rec.log.job");
    const record = try std.fmt.allocPrint(a, "{s}/rec.log.job", .{t.root});
    const log = try std.fmt.allocPrint(a, "{s}/rec.log", .{t.root});
    const program = (try jobs.find(a, "shale-fixture", t.host.env, t.root)).?;
    try testing.expectError(error.NoRecord, jobs.launch(a, io, t.host.reaper.?, record, log, t.root, program, &.{ "shale-fixture", "pid-sleep", "20000" }, t.host.env));
    // If it ran long enough to say its pid, it is gone within a second.
    var said: ?i32 = null;
    for (0..50) |_| {
        const text = t.tmp.dir.readFileAlloc(io, "rec.log", a, .limited(4096)) catch "";
        if (std.mem.startsWith(u8, text, "pid ")) {
            said = try std.fmt.parseInt(i32, std.mem.trimEnd(u8, text[4..], "\n"), 10);
            break;
        }
        std.Io.sleep(io, .fromMilliseconds(10), .awake) catch {};
    }
    const pid = said orelse return;
    var buf: [1024]u8 = undefined;
    for (0..100) |_| {
        if (jobs.startOf(&buf, pid) == null) return;
        std.Io.sleep(io, .fromMilliseconds(10), .awake) catch {};
    }
    _ = std.os.linux.kill(pid, .KILL);
    std.debug.print("the job (pid {d}) runs on, and no record names it\n", .{pid});
    return error.TestUnexpectedResult;
}

test "jobs: the mistakes a job's call can make, each named" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.expectStop("job_start({\"shale-fixture\", \"lines\", \"1\"}, {})", .atomic, .argument, "`job_start` needs a `log`");
    try t.expectStop("job_start({\"shale-fixture\", \"lines\", \"1\"}, {log = \"x.log\", secrets = {K = \"k\"}})", .atomic, .argument, "no `secrets` on a job");
    try t.expectStop("job_start({\"shale-fixture\", \"lines\", \"1\"}, {log = \"x.log\", tail = 3})", .atomic, .argument, "`job_start` takes log, cwd, env, want and keep_on_stop, not `tail`");
    try t.expectStop("job_start({\"bash\", \"-c\", \"x\"}, {log = \"x.log\"})", .atomic, .refused, "`job_start` of `bash`");
    try t.expectStop("job_start({\"no-such-program-anywhere\"}, {log = \"x.log\"})", .atomic, .host, "`job_start`: `no-such-program-anywhere` is not found along PATH");
    try t.expectStop("job_start({\"shale-fixture\", \"lines\", \"1\"}, {log = \"no-such-dir/x.log\"})", .atomic, .store, "`job_start`: no-such-dir/x.log's directory is not there");
    try t.expectStop("job_alive(\"none.log.job\")", .atomic, .store, "`job_alive`: no job's record at none.log.job");
    try t.expectStop("job_wait(\"none.log.job\", {})", .atomic, .argument, "`job_wait` needs `timeout_ms`");
    try t.expectStop("job_wait(\"none.log.job\", {timeout_ms = 10, file = \"a\"})", .atomic, .argument, "`file` without `contains`");
}

test "jobs: a job stopped just after its start is gone when job_stop returns, its leader too, even one that ignores TERM before it made its group" {
    // The leader makes its group only when it first runs: a stop that
    // watched the group alone saw nothing, said "stopped", and sent no KILL.
    // **Probabilistic, and why:** the window is in the reaper's child,
    // between its fork and its `setsid`, before any program runs, so no
    // fixture can hold it open (one runs only after `exec`). Twenty starts
    // cross it often enough to have shown it red (1 in 20 before the fix).
    // The green waits on `alive` for up to 2 s after KILL, the reaper's
    // reap: a job not gone by then is a failure here, not a slow pass.
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try testing.expectEqualStrings("0\n", try once(&t,
        \\local n = 0
        \\for i = 1, 20 do
        \\  local j = job_start({"shale-fixture", "ignore-term", "20000"}, {log = "stopped" .. i .. ".log"})
        \\  job_stop(j)
        \\  if job_alive(j) then n = n + 1 end
        \\end
        \\print(n)
    ));
}

test "jobs: a job whose argument holds a newline is found again by its record; an argv too long for a record is said before it starts" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try testing.expectEqualStrings("exited\t0\n", try once(&t, "local j = job_start({\"shale-fixture\", \"lines\", \"1\", \"one\\ntwo\"}, {log = \"nl.log\"}) local w = job_wait(j, {timeout_ms = 10000}) print(w.ended, w.code)"));
    var r = t.runOnce("job_start({\"shale-fixture\", \"lines\", \"1\", rep(\"x\", 8000)}, {log = \"long.log\"})", .atomic);
    defer r.deinit();
    try testing.expect(std.mem.indexOf(u8, r.stop.?.message, "a record holds 7168") != null);
}

/// What a `shale` run of `src` (written to `name` in the root) did, as a
/// shell sees it: the binary the tests build, the fixture on its PATH.
const Launched = struct { code: u8, stderr: []const u8 };

fn launch(t: *T, name: []const u8, src: []const u8) !Launched {
    const a = t.arena.allocator();
    try t.put(name, src);
    const cmd = try std.fmt.allocPrint(a, "{s} '{s}' --root '{s}' '{s}/{s}'", .{ t.path_env, t.host.reaper.?, t.root, t.root, name });
    const r = try std.process.run(a, t.threaded.io(), .{ .argv = &.{ "sh", "-c", cmd } });
    return .{ .code = switch (r.term) {
        .exited => |c| c,
        else => 255,
    }, .stderr = r.stderr };
}

/// The `── stopped with the run` line of a run's report, or "" when it has none.
fn stopLine(stderr: []const u8) []const u8 {
    const at = std.mem.indexOf(u8, stderr, "── stopped with the run") orelse return "";
    return stderr[at .. std.mem.indexOfScalarPos(u8, stderr, at, '\n') orelse stderr.len];
}

test "jobs: a run that stops ends the jobs it started, in one line naming each; a run that ends keeps them; another run's job is never touched" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    // A run that ends: its job runs on, and nothing is said.
    const ended = try launch(&t, "ends.shale", "job_start({\"shale-fixture\", \"sleep\", \"30000\"}, {log = \"kept.log\"}) print(\"started\")\n");
    errdefer std.debug.print("{s}\n", .{ended.stderr});
    try testing.expectEqual(@as(u8, 0), ended.code);
    try testing.expectEqualStrings("", stopLine(ended.stderr));
    try testing.expectEqualStrings("true\n", try once(&t, "print(job_alive(\"kept.log.job\"))"));
    // A run that fails: its two jobs end, named; the first run's job, which
    // this one asks about, runs on.
    const failed = try launch(&t, "fails.shale",
        \\job_start({"shale-fixture", "sleep", "30000"}, {log = "one.log"})
        \\job_start({"shale-fixture", "sleep", "30000", "two"}, {log = "two.log"})
        \\print(job_alive("kept.log.job"))
        \\fail("on purpose")
        \\
    );
    errdefer std.debug.print("{s}\n", .{failed.stderr});
    try testing.expectEqual(@as(u8, 1), failed.code);
    try testing.expectEqualStrings("── stopped with the run, 2 jobs it started: `shale-fixture sleep 30000` (log one.log), `shale-fixture sleep 30000 two` (log two.log)", stopLine(failed.stderr));
    try testing.expectEqualStrings("false\tfalse\ttrue\n", try once(&t, "print(job_alive(\"one.log.job\"), job_alive(\"two.log.job\"), job_alive(\"kept.log.job\"))"));
    // A budget's stop is a stop too.
    const budget = try launch(&t, "budget.shale",
        \\-- budget: steps=100000
        \\job_start({"shale-fixture", "sleep", "30000"}, {log = "spent.log"})
        \\local i = 0
        \\while true do i = i + 1 end
        \\
    );
    errdefer std.debug.print("{s}\n", .{budget.stderr});
    try testing.expectEqual(@as(u8, 1), budget.code);
    try testing.expect(std.mem.indexOf(u8, budget.stderr, "budget steps") != null);
    try testing.expectEqualStrings("── stopped with the run, 1 job it started: `shale-fixture sleep 30000` (log spent.log)", stopLine(budget.stderr));
    try testing.expectEqualStrings("false\ttrue\ttrue\n", try once(&t, "print(job_alive(\"spent.log.job\"), job_alive(\"kept.log.job\"), job_stop(\"kept.log.job\"))"));
}

test "jobs: keep_on_stop keeps its job through the run's stop, said apart; a keep_on_stop that is no boolean is refused" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const r = try launch(&t, "keep.shale",
        \\job_start({"shale-fixture", "sleep", "30000"}, {log = "server.log", keep_on_stop = true})
        \\job_start({"shale-fixture", "sleep", "30000"}, {log = "helper.log", keep_on_stop = false})
        \\fail("on purpose")
        \\
    );
    errdefer std.debug.print("{s}\n", .{r.stderr});
    try testing.expectEqual(@as(u8, 1), r.code);
    try testing.expectEqualStrings("── stopped with the run, 1 job it started: `shale-fixture sleep 30000` (log helper.log)", stopLine(r.stderr));
    try testing.expect(std.mem.indexOf(u8, r.stderr, "── left running (keep_on_stop): `shale-fixture sleep 30000` (log server.log)\n") != null);
    try testing.expectEqualStrings("true\tfalse\ttrue\n", try once(&t, "print(job_alive(\"server.log.job\"), job_alive(\"helper.log.job\"), job_stop(\"server.log.job\"))"));
    try t.expectStop("job_start({\"shale-fixture\", \"exit\", \"0\"}, {log = \"x.log\", keep_on_stop = \"yes\"})", .atomic, .type, "`keep_on_stop` is a boolean, not a string");
}

test "jobs: a failure attempt catches is no stop, so its jobs run on; a later stop ends them, one started inside the attempt too" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const caught = try launch(&t, "caught.shale",
        \\function f()
        \\  job_start({"shale-fixture", "sleep", "30000"}, {log = "inside.log"})
        \\  fail("caught")
        \\end
        \\print(attempt(f).ok)
        \\job_start({"shale-fixture", "sleep", "30000"}, {log = "after.log"})
        \\
    );
    errdefer std.debug.print("{s}\n", .{caught.stderr});
    try testing.expectEqual(@as(u8, 0), caught.code);
    try testing.expectEqualStrings("", stopLine(caught.stderr));
    try testing.expectEqualStrings("true\ttrue\ttrue\ttrue\n", try once(&t, "print(job_alive(\"inside.log.job\"), job_alive(\"after.log.job\"), job_stop(\"inside.log.job\"), job_stop(\"after.log.job\"))"));
    const later = try launch(&t, "later.shale",
        \\function f()
        \\  job_start({"shale-fixture", "sleep", "30000"}, {log = "inside2.log"})
        \\  fail("caught")
        \\end
        \\attempt(f)
        \\fail("the run stops now")
        \\
    );
    errdefer std.debug.print("{s}\n", .{later.stderr});
    try testing.expectEqual(@as(u8, 1), later.code);
    try testing.expectEqualStrings("── stopped with the run, 1 job it started: `shale-fixture sleep 30000` (log inside2.log)", stopLine(later.stderr));
    try testing.expectEqualStrings("false\n", try once(&t, "print(job_alive(\"inside2.log.job\"))"));
}

test "jobs: a job the run already stopped or waited to its end makes no second stop; a log taken again names only its new job, once" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const none = try launch(&t, "none.shale",
        \\local s = job_start({"shale-fixture", "sleep", "30000"}, {log = "stopped.log"})
        \\job_stop(s)
        \\local w = job_start({"shale-fixture", "exit", "0"}, {log = "waited.log"})
        \\job_wait(w, {timeout_ms = 10000})
        \\fail("on purpose")
        \\
    );
    errdefer std.debug.print("{s}\n", .{none.stderr});
    try testing.expectEqual(@as(u8, 1), none.code);
    try testing.expectEqualStrings("", stopLine(none.stderr));
    // The log a waited job ended on, taken by a job that still runs: that
    // one job is stopped, named once.
    const again = try launch(&t, "again.shale",
        \\local w = job_start({"shale-fixture", "exit", "0"}, {log = "reused.log"})
        \\job_wait(w, {timeout_ms = 10000})
        \\job_start({"shale-fixture", "sleep", "30000"}, {log = "reused.log"})
        \\fail("on purpose")
        \\
    );
    errdefer std.debug.print("{s}\n", .{again.stderr});
    try testing.expectEqual(@as(u8, 1), again.code);
    try testing.expectEqualStrings("── stopped with the run, 1 job it started: `shale-fixture sleep 30000` (log reused.log)", stopLine(again.stderr));
    try testing.expectEqualStrings("false\n", try once(&t, "print(job_alive(\"reused.log.job\"))"));
}
