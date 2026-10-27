//! The host's `run`: reads the options, runs the program under its deadline,
//! and keeps what the script asked of its output. The refusals and option
//! readers it shares with the job and Zig calls live in `shale_host_linux.zig`.

const script = shale.script;
const Value = script.value.Value;
const Run = interp.Run;
const ast = script.ast;
const Error = interp.Error;
const std = @import("std");
const keep = @import("shale_keep.zig");
const shale = @import("shale");
const interp = script.interp;

const hl = @import("shale_host_linux.zig");

const regex = script.regex;

/// `run`'s options, as the script gives them.
const Opts = struct {
    /// The deadline; required.
    timeout_ms: ?u64 = null,
    /// Keep only lines matching this pattern, plus context.
    re: ?[]const u8 = null,
    /// Drop lines matching this pattern from the stream.
    not_re: ?[]const u8 = null,
    /// Match `re` and `not_re` ignoring case.
    nocase: bool = false,
    /// Context lines before a match; needs `re`.
    before: usize = 0,
    /// Context lines after a match; needs `re`.
    after: usize = 0,
    /// Lines kept from the start.
    head: ?usize = null,
    /// Lines kept from the end.
    tail: ?usize = null,
    /// The bytes of a kept line; a longer line is cut.
    width: usize = 200,
    /// Keep the whole output, up to this many bytes; excludes filters and keeps.
    whole: ?usize = null,
    /// A Shale path the whole scrubbed output is written to.
    log: ?Value = null,
    /// The Shale path the program starts in; the root by default.
    cwd: ?Value = null,
    /// A string, or the result of a `whole` run, given as stdin.
    stdin: ?Value = null,
    /// Variables added to the inherited environment.
    env: ?*script.value.Table = null,
    /// Variables whose values are read from the Shale paths given, added to the
    /// environment and scrubbed from the output.
    secrets: ?*script.value.Table = null,
    /// `"tag: why"`, to run a program `refuse` would refuse.
    want: ?[]const u8 = null,
};

/// A path option (`cwd`, `log`): a string, named as `run`'s when it is not.
fn pathOpt(run: *Run, pos: ast.Pos, k: []const u8, x: Value) Error!Value {
    _ = try hl.strOpt(run, pos, k, x);
    return x;
}

fn readOpts(run: *Run, pos: ast.Pos, v: Value) Error!Opts {
    var o: Opts = .{};
    const m = switch (v) {
        .table => |t| if (t.kind == .list) return run.fail(pos, .type, "`run`'s options are a map, `{{timeout_ms = ..., tail = ...}}`, not a list", .{}) else t,
        else => return run.fail(pos, .type, "`run`'s options are a map, not {s}", .{v.typeName()}),
    };
    for (m.map.keys(), m.map.values()) |k, x| {
        if (std.mem.eql(u8, k, "timeout_ms")) o.timeout_ms = try hl.intOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "re")) o.re = try hl.strOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "not_re")) o.not_re = try hl.strOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "nocase")) o.nocase = switch (x) {
            .boolean => |b| b,
            else => return run.fail(pos, .type, "`run`: its `nocase` must be true or false, not {s}", .{x.typeName()}),
        } else if (std.mem.eql(u8, k, "before")) o.before = try hl.intOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "after")) o.after = try hl.intOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "head")) o.head = try hl.intOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "tail")) o.tail = try hl.intOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "width")) o.width = try hl.intOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "whole")) o.whole = try hl.intOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "log")) o.log = try pathOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "cwd")) o.cwd = try pathOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "stdin")) o.stdin = x else if (std.mem.eql(u8, k, "env")) o.env = try hl.mapOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "secrets")) o.secrets = try hl.mapOpt(run, pos, k, x) else if (std.mem.eql(u8, k, "want")) o.want = try hl.strOpt(run, pos, k, x) else return run.fail(pos, .argument, "`run` takes timeout_ms, re, not_re, nocase, before, after, head, tail, width, whole, log, cwd, stdin, env, secrets and want, not `{s}`", .{k});
    }
    if (o.timeout_ms == null) return run.fail(pos, .argument, "`run` needs `timeout_ms`: how long the program may take is the script's to say", .{});
    if (o.whole) |_| {
        if (o.re != null or o.not_re != null or o.head != null or o.tail != null or o.before > 0 or o.after > 0 or o.nocase)
            return run.fail(pos, .argument, "`run`'s `whole` keeps everything, and takes no filter or keep (`re`, `not_re`, `nocase`, `before`, `after`, `head`, `tail`)", .{});
    } else if (o.head == null and o.tail == null) return run.fail(pos, .argument, "`run` needs a keep: `head`, `tail` (or both), or `whole = N`: what to keep of the output is the script's to say", .{});
    if ((o.before > 0 or o.after > 0) and o.re == null) return run.fail(pos, .argument, "`run`'s `before` and `after` are a match's context, and need `re`", .{});
    return o;
}

pub fn runProgram(h: *hl.Host, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    if (args.len != 2) return run.fail(pos, .argument, "`run` takes argv and its options, `run({{\"zig\", \"build\"}}, {{timeout_ms = ..., tail = 20}})`, given {d} arguments", .{args.len});
    const a = run.alloc;
    const argv_t = switch (args[0]) {
        .table => |t| if (t.kind == .map) return run.fail(pos, .type, "`run`'s argv is a list of strings, not a map", .{}) else t,
        else => return run.fail(pos, .type, "`run`'s argv is a list of strings, `{{\"zig\", \"build\"}}`, not {s}", .{args[0].typeName()}),
    };
    if (argv_t.kind == .empty or argv_t.list.items.len == 0) return run.fail(pos, .argument, "`run` of an empty argv: name the program", .{});
    const argv = a.alloc([]const u8, argv_t.list.items.len) catch return run.oom(pos);
    for (argv_t.list.items, argv, 0..) |v, *s, i| s.* = switch (v) {
        .string => |x| x,
        else => return run.fail(pos, .type, "`run`'s argv[{d}] is {s}, not a string: every argument is one, written as the program takes it", .{ i + 1, v.typeName() }),
    };
    const o = try readOpts(run, pos, args[1]);
    try run.charge(pos, argv.len);

    if (run.host_runs >= hl.runs_most) {
        run.stop = .{ .line = pos.line, .column = pos.column, .class = .budget, .budget = .runs, .function = run.function, .trail = run.calls.items, .message = std.fmt.allocPrint(a, "{d} programs a run, the most: run the rest in another", .{hl.runs_most}) catch "too many programs in one run" };
        return error.Stop;
    }

    const cwd = if (o.cwd) |c| try hl.osPath(h, run, pos, c, true) else try hl.rootOf(h, run, pos);
    try hl.refuse(h, run, pos, "run", argv[0], cwd, o.want);
    try hl.refuseSecretArgs(h, run, pos, "run", argv);

    // The inherited environment, overridden by `env` and `secrets`.
    var env: std.ArrayList([]const u8) = .empty;
    var secrets: std.ArrayList([2][]const u8) = .empty;
    var shown_env: std.ArrayList(u8) = .empty;
    const extra = if (o.env) |t| t.map.keys() else &.{};
    const secret_keys = if (o.secrets) |t| t.map.keys() else &.{};
    for (h.env) |e| {
        const k = e[0 .. std.mem.indexOfScalar(u8, e, '=') orelse e.len];
        const over = for (extra) |x| {
            if (std.mem.eql(u8, x, k)) break true;
        } else for (secret_keys) |x| {
            if (std.mem.eql(u8, x, k)) break true;
        } else false;
        if (!over) env.append(a, e) catch return run.oom(pos);
    }
    if (o.env) |t| for (t.map.keys(), t.map.values()) |k, x| {
        const v = try hl.strOpt(run, pos, "env", x);
        env.append(a, std.fmt.allocPrint(a, "{s}={s}", .{ k, v }) catch return run.oom(pos)) catch return run.oom(pos);
        shown_env.print(a, " {s}={s}", .{ k, v }) catch return run.oom(pos);
    };
    if (o.secrets) |t| for (t.map.keys(), t.map.values()) |k, x| {
        const path = try hl.secretPath(h, run, pos, x);
        const shown = switch (x) {
            .string => |s| s,
            else => "",
        };
        const bytes = std.Io.Dir.cwd().readFileAlloc(run.io, path, a, .limited(64 * 1024)) catch |e|
            return run.fail(pos, .store, "`run`: the secret `{s}`'s file {s} cannot be read: {s}", .{ k, shown, @errorName(e) });
        const v = std.mem.trim(u8, bytes, " \t\r\n");
        if (v.len == 0) return run.fail(pos, .argument, "`run`: the secret `{s}`'s file {s} is empty", .{ k, shown });
        env.append(a, std.fmt.allocPrint(a, "{s}={s}", .{ k, v }) catch return run.oom(pos)) catch return run.oom(pos);
        secrets.append(a, .{ k, v }) catch return run.oom(pos);
        shown_env.print(a, " {s}=<secret {s}>", .{ k, shown }) catch return run.oom(pos);
    };

    const stdin: ?[]const u8 = if (o.stdin) |v| switch (v) {
        .string => |s| s,
        .table => |t| blk: {
            if (t.kind != .map) return run.fail(pos, .type, "`run`'s stdin is a string or a run's result, not a list", .{});
            const whole = t.map.get("whole") orelse break :blk null;
            if (whole != .boolean or !whole.boolean) break :blk null;
            break :blk switch (t.map.get("out") orelse Value.nil) {
                .string => |s| s,
                else => null,
            };
        },
        else => return run.fail(pos, .type, "`run`'s stdin is a string or a run's result, not {s}", .{v.typeName()}),
    } else null;
    if (o.stdin != null and stdin == null) return run.fail(pos, .argument, "`run`'s stdin is a kept result: its `out` is not the program's output (lines left out, lines cut); run it with `whole = N` to pipe it", .{});

    // What grows as output arrives (the keep, `whole`'s buffer, the scrub's
    // carry) grows by how the pipe's reads fall, which the scheduler decides.
    // In the run's arena that would make the arena's size nondeterministic,
    // and the gate holds the engines' arenas equal. So the stream uses the
    // host's allocator, capped (`Capped`), and only results go to the arena.
    var capped: hl.Capped = .{ .backing = h.alloc, .most = run.budgets.arena_bytes -| run.budgeted.held };
    const sa = capped.allocator();
    var out: hl.Out = .{ .alloc = sa };
    var log_file: ?std.Io.File = null;
    var log_buf: [16 * 1024]u8 = undefined;
    var log_writer: std.Io.File.Writer = undefined;
    var log_shown: []const u8 = "";
    if (o.log) |l| {
        const path = try hl.osPath(h, run, pos, l, false);
        log_shown = switch (l) {
            .string => |s| s,
            else => "",
        };
        const f = std.Io.Dir.cwd().createFile(run.io, path, .{ .truncate = true }) catch |e|
            return run.fail(pos, .store, "`run`: its log {s} cannot be written: {s}", .{ log_shown, @errorName(e) });
        log_file = f;
        log_writer = f.writer(run.io, &log_buf);
        out.log = &log_writer;
    }
    defer if (log_file) |f| {
        log_writer.interface.flush() catch {};
        f.close(run.io);
    };
    var whole_buf: std.ArrayList(u8) = .empty;
    defer whole_buf.deinit(sa);
    var compiled: [2]?regex.Program = .{ null, null };
    var scratches: [2]?regex.Scratch = .{ null, null };
    var keeper: keep.Keep = undefined;
    if (o.whole) |n| {
        out.whole = &whole_buf;
        out.whole_most = n;
    } else {
        var ko: keep.Options = .{ .head = o.head orelse 0, .tail = o.tail orelse 0, .width = o.width, .before = o.before, .after = o.after };
        for ([_]?[]const u8{ o.re, o.not_re }, 0..) |pat, i| if (pat) |p| {
            var why: regex.Refusal = undefined;
            compiled[i] = regex.compile(a, p, .{ .nocase = o.nocase }, &why) catch |e| switch (e) {
                error.OutOfMemory => return run.oom(pos),
                error.Refused => return run.fail(pos, .argument, "`run`'s `{s}`, at its byte {d}: {s}", .{ if (i == 0) "re" else "not_re", why.at + 1, why.message }),
            };
            scratches[i] = regex.Scratch.init(a, &compiled[i].?) catch return run.oom(pos);
            const pattern: keep.Pattern = .{ .program = &compiled[i].?, .scratch = &scratches[i].? };
            if (i == 0) ko.re = pattern else ko.not_re = pattern;
        };
        keeper = keep.Keep.init(sa, ko) catch return run.oom(pos);
        out.keeper = &keeper;
    }
    defer if (out.keeper) |k| k.deinit();
    var scrub: hl.Scrub = .{ .alloc = sa, .secrets = secrets.items, .next = &out };
    defer scrub.carry.deinit(sa);
    var sink: hl.Sink = .{ .host = h, .scrub = &scrub };

    run.host_runs += 1;
    const outcome = h.spawn_fn(h.spawn_ctx, h.alloc, .{ .argv = argv, .cwd = cwd, .env = env.items, .stdin = stdin, .timeout_ms = o.timeout_ms.? }, &sink) catch |e| switch (e) {
        error.OutOfMemory => return if (capped.hit) streamOver(run, pos, capped.most) else run.oom(pos),
        error.NotFound => return run.fail(pos, .host, "`run`: `{s}` is not found{s}", .{ argv[0], if (std.mem.indexOfScalar(u8, argv[0], '/') == null) " along PATH" else "" }),
        error.NotRunnable => return run.fail(pos, .host, "`run`: `{s}` is there but cannot be run (its permissions, or not a program)", .{argv[0]}),
        error.NoCwd => return run.fail(pos, .argument, "`run`: its cwd is not a folder that can be entered", .{}),
        error.Spawn => return run.fail(pos, .host, "`run`: the program could not be started (a pipe, a fork or a pidfd refused)", .{}),
    };
    scrub.finish() catch return if (capped.hit) streamOver(run, pos, capped.most) else run.oom(pos);

    // Charge the output as bulk work and as read bytes, and the filter's
    // searches.
    try script.builtins.chargeBytes(run, pos, out.bytes);
    run.read_bytes += out.bytes;
    const joined = std.mem.join(a, " ", argv) catch return run.oom(pos);
    h.record("run {s}{s}{s}{s} -> {s}{s} ({d} ms)", .{
        joined,                                 shown_env.items,
        if (o.want != null) " (want: " else "", if (o.want) |w| std.fmt.allocPrint(a, "{s})", .{w}) catch w else "",
        @tagName(outcome.ended),                if (outcome.code) |c| std.fmt.allocPrint(a, " {d}", .{c}) catch "" else "",
        outcome.ms,
    });
    // Flush before the record says whether the log is whole.
    if (log_file != null) log_writer.interface.flush() catch {
        out.log_failed = true;
    };
    if (o.log != null) h.record("log {s}{s}", .{ log_shown, if (out.log_failed) ": a write failed, the log is short" else "" });

    const r = try run.newTable(pos);
    r.kind = .map;
    const argv_list = try run.newTable(pos);
    argv_list.kind = .list;
    for (argv) |s| argv_list.list.append(a, .{ .string = s }) catch return run.oom(pos);
    r.map.put(a, "argv", .{ .table = argv_list }) catch return run.oom(pos);
    if (outcome.code) |c| if (outcome.ended == .exit) r.map.put(a, "code", .{ .int = c }) catch return run.oom(pos);
    r.map.put(a, "ended", .{ .string = switch (outcome.ended) {
        .exit => "exit",
        .timeout => "timeout",
        .signal => "signal",
        .interrupted => "signal",
    } }) catch return run.oom(pos);
    r.map.put(a, "ms", .{ .int = @intCast(outcome.ms) }) catch return run.oom(pos);
    if (o.whole) |n| {
        if (out.whole_over) return run.fail(pos, .argument, "`run`: the output passed `whole = {d}` bytes ({d}): a keep, `head` and `tail`, takes any size", .{ n, out.bytes });
        const lines = countLines(whole_buf.items);
        r.map.put(a, "out", .{ .string = a.dupe(u8, whole_buf.items) catch return run.oom(pos) }) catch return run.oom(pos);
        r.map.put(a, "whole", .{ .boolean = true }) catch return run.oom(pos);
        for ([_][]const u8{ "total_lines", "matched_lines", "kept_lines" }) |k| r.map.put(a, k, .{ .int = @intCast(lines) }) catch return run.oom(pos);
    } else {
        const kept = keeper.finish() catch return if (capped.hit) streamOver(run, pos, capped.most) else run.oom(pos);
        defer sa.free(kept.out);
        try run.charge(pos, kept.work);
        r.map.put(a, "out", .{ .string = a.dupe(u8, kept.out) catch return run.oom(pos) }) catch return run.oom(pos);
        r.map.put(a, "total_lines", .{ .int = @intCast(kept.total) }) catch return run.oom(pos);
        r.map.put(a, "matched_lines", .{ .int = @intCast(kept.matched) }) catch return run.oom(pos);
        r.map.put(a, "kept_lines", .{ .int = @intCast(kept.kept) }) catch return run.oom(pos);
    }
    if (run.read_bytes > run.budgets.read_bytes) return run.budgetStop(pos, .read_bytes);
    // An interrupted `shale` stops here, once the program's group is gone.
    if (outcome.ended == .interrupted) return run.fail(pos, .host, "`run`: shale was told to stop (TERM or HUP) while `{s}` ran; it was ended", .{argv[0]});
    return .{ .table = r };
}

fn streamOver(run: *Run, pos: ast.Pos, most: usize) Error {
    run.stop = .{ .line = pos.line, .column = pos.column, .class = .budget, .budget = .arena, .function = run.function, .trail = run.calls.items, .message = std.fmt.allocPrint(run.alloc, "`run`: what it holds of the output is past the arena budget ({d} bytes live): a smaller `before`, `head`, `tail` or `whole`", .{most}) catch "`run`: what it holds of the output is past the arena budget" };
    return error.Stop;
}

fn countLines(s: []const u8) usize {
    var n = std.mem.count(u8, s, "\n");
    if (s.len > 0 and s[s.len - 1] != '\n') n += 1;
    return n;
}
