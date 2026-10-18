//! **THE LINUX TOOL'S COMMAND LINE, READ** (metal-vmm QUEUE 229, chunk 4):
//! what `shale` was asked, pure, so it is tested here and the runner
//! (`src/script_runner.zig`) only does the I/O.
//!
//!     shale [--root DIR] [--verbose] [--undo-on-stop] [--budget NAME=N]... [FILE [--args-stdin] | -e SOURCE] [NAME=VALUE]...
//!
//! **A run makes its changes and runs its programs**: on Linux, git is
//! the undo (Steve, 2026-10-17, deleting the dry run). **With no FILE and
//! no `-e`, the script is stdin** (metal-vmm QUEUE 233). **TERSE** (the
//! dogfood design's "The CLI's defaults"): what a run used is said only
//! past half a budget or on a stop, the effects only on `--verbose`;
//! `printReport` is the CLI's, and `script.report`, the route's, is frozen.

const std = @import("std");
const interp = @import("interp.zig");
const builtins = @import("builtins.zig");
const parse_mod = @import("parse.zig");
const script = @import("script.zig");
const diag = @import("diag.zig");

pub const usage =
    \\usage: shale [--root DIR] [--verbose] [--undo-on-stop] [--budget NAME=N]... [FILE [--args-stdin] | -e SOURCE] [NAME=VALUE]...
    \\  --root DIR        the folder the script's paths are relative to (SHALE_ROOT, or here)
    \\  --verbose         the effects, the programs run and what the run used, always
    \\  --undo-on-stop    a run that stops puts back every path it changed (not a program's)
    \\  --args-stdin      the FILE's NAME=VALUEs on stdin, one a line, each value raw to its end
    \\  --budget NAME=N   one budget: steps, reads, read_bytes, output_bytes, changes,
    \\                    change_bytes, arena_bytes, call_bytes, call_entries
    \\  -e SOURCE         the script itself, in place of a file (neither: stdin is the script)
    \\  NAME=VALUE        a value the script reads with arg("NAME", default)
    \\
;

/// **THE CLI'S BUDGETS** (the dogfood design's "The CLI's defaults"):
/// output 64 KiB, a call's bytes 64 MiB (a program's log read whole, a
/// `git diff`), the rest the core's.
pub const defaults: interp.Budgets = .{ .output_bytes = 64 * 1024, .call_bytes = 64 * 1024 * 1024 };

pub const Command = struct {
    /// The data root: `--root`, else the caller's `SHALE_ROOT`, else ".".
    root: []const u8,
    /// The file to read, or null for `-e`'s source or stdin's.
    file: ?[]const u8 = null,
    source: ?[]const u8 = null,
    /// Neither a FILE nor `-e`: the script is stdin.
    from_stdin: bool = false,
    verbose: bool = false,
    /// `--undo-on-stop`: a stopped run's changes put back (the Linux host's journal).
    undo_on_stop: bool = false,
    /// `--args-stdin`: the FILE's values are stdin's lines (`argLines`).
    args_stdin: bool = false,
    budgets: interp.Budgets = defaults,
    /// Into the caller's buffer.
    args: []const interp.Arg = &.{},
};

/// What went wrong reading it, said in `why`.
pub const Refused = struct { why: []const u8 };

/// The budgets a command line may set, by name.
const settable = [_][]const u8{ "steps", "reads", "read_bytes", "output_bytes", "changes", "change_bytes", "arena_bytes", "call_bytes", "call_entries" };

/// **THE COMMAND LINE** (`argv` past the program's name), the caller's
/// `SHALE_ROOT` if it has one, the `NAME=VALUE`s into `args_buf`; or why
/// not, in `why_buf`.
pub fn parse(argv: []const []const u8, env_root: ?[]const u8, args_buf: []interp.Arg, why_buf: []u8) union(enum) { command: Command, refused: Refused } {
    var c: Command = .{ .root = env_root orelse "." };
    var n_args: usize = 0;
    var i: usize = 0;
    while (i < argv.len) : (i += 1) {
        const a = argv[i];
        if (std.mem.eql(u8, a, "--verbose")) {
            c.verbose = true;
        } else if (std.mem.eql(u8, a, "--undo-on-stop")) {
            c.undo_on_stop = true;
        } else if (std.mem.eql(u8, a, "--args-stdin")) {
            c.args_stdin = true;
        } else if (std.mem.eql(u8, a, "--root") or std.mem.eql(u8, a, "--budget") or std.mem.eql(u8, a, "-e")) {
            if (i + 1 == argv.len) return refuse(why_buf, "{s} needs a value", .{a});
            i += 1;
            const v = argv[i];
            if (std.mem.eql(u8, a, "--root")) {
                c.root = v;
            } else if (std.mem.eql(u8, a, "-e")) {
                if (c.file != null or c.source != null) return refuse(why_buf, "one script: a file or -e, not both", .{});
                if (n_args > 0) return refuse(why_buf, "{s}={s} before the script: the script first, then NAME=VALUE", .{ args_buf[0].name, args_buf[0].value });
                c.source = v;
            } else {
                const eq = std.mem.indexOfScalar(u8, v, '=') orelse return refuse(why_buf, "--budget {s}: it is NAME=N", .{v});
                const name = v[0..eq];
                const n = builtins.parseInteger(v[eq + 1 ..]) orelse return refuse(why_buf, "--budget {s}: {s} is not a number", .{ v, v[eq + 1 ..] });
                if (n < 1) return refuse(why_buf, "--budget {s}: a budget is at least 1", .{v});
                if (!setBudget(&c.budgets, name, @intCast(n))) return refuse(why_buf, "--budget {s}: no budget is called {s}", .{ v, name });
            }
        } else if (std.mem.startsWith(u8, a, "-")) {
            return refuse(why_buf, "{s} is not an option", .{a});
        } else if (std.mem.indexOfScalar(u8, a, '=')) |eq| {
            // Before any script, a value is stdin's script's, unless a
            // script comes after it (refused there).
            if (eq == 0) return refuse(why_buf, "{s}: a value needs a name", .{a});
            if (n_args == args_buf.len) return refuse(why_buf, "more than {d} values", .{args_buf.len});
            args_buf[n_args] = .{ .name = a[0..eq], .value = a[eq + 1 ..] };
            n_args += 1;
        } else {
            if (c.file != null or c.source != null) return refuse(why_buf, "one script: {s} is a second", .{a});
            if (n_args > 0) return refuse(why_buf, "{s}={s} before the script: the script first, then NAME=VALUE", .{ args_buf[0].name, args_buf[0].value });
            c.file = a;
        }
    }
    c.from_stdin = c.file == null and c.source == null;
    if (c.args_stdin and c.file == null) return refuse(why_buf, "--args-stdin: stdin is the values of a FILE, and there is no FILE", .{});
    c.args = args_buf[0..n_args];
    return .{ .command = c };
}

/// **A FILE'S VALUES ON STDIN** (`--args-stdin`): one `NAME=VALUE` a line,
/// the value raw to the line's end (quotes, spaces, `|` and backslashes as
/// they are: no shell sees them), so a heredoc gives a script what a
/// command line could not without quoting. Blank lines are passed by. The
/// values go after `have` (the command line's), into `buf`; a name given
/// twice, in either, is refused.
pub fn argLines(text: []const u8, have: []const interp.Arg, buf: []interp.Arg, why_buf: []u8) union(enum) { args: []const interp.Arg, refused: Refused } {
    if (have.len > buf.len) return .{ .refused = .{ .why = "more values than the buffer holds" } };
    @memcpy(buf[0..have.len], have);
    var n = have.len;
    var it = std.mem.splitScalar(u8, text, '\n');
    var line_no: usize = 0;
    while (it.next()) |raw| {
        line_no += 1;
        const line = std.mem.trimEnd(u8, raw, "\r");
        if (std.mem.trim(u8, line, " \t").len == 0) continue;
        const eq = std.mem.indexOfScalar(u8, line, '=') orelse
            return .{ .refused = .{ .why = std.fmt.bufPrint(why_buf, "--args-stdin: line {d} is not NAME=VALUE", .{line_no}) catch "--args-stdin: a line is not NAME=VALUE" } };
        const name = line[0..eq];
        if (name.len == 0 or std.mem.indexOfAny(u8, name, " \t") != null)
            return .{ .refused = .{ .why = std.fmt.bufPrint(why_buf, "--args-stdin: line {d}: `{s}` is not a name", .{ line_no, name }) catch "--args-stdin: a line's name is not one" } };
        for (buf[0..n]) |a| if (std.mem.eql(u8, a.name, name))
            return .{ .refused = .{ .why = std.fmt.bufPrint(why_buf, "--args-stdin: line {d}: {s} is given twice", .{ line_no, name }) catch "--args-stdin: a name is given twice" } };
        if (n == buf.len) return .{ .refused = .{ .why = std.fmt.bufPrint(why_buf, "more than {d} values", .{buf.len}) catch "too many values" } };
        buf[n] = .{ .name = name, .value = line[eq + 1 ..] };
        n += 1;
    }
    return .{ .args = buf[0..n] };
}

/// What a stop and its trail call the script: its file, `-e` or `stdin`.
pub fn scriptName(c: Command) []const u8 {
    return c.file orelse if (c.source != null) "-e" else "stdin";
}

/// What the run took, beside its budgets.
pub const Used = struct { steps: u64 = 0, reads: u64 = 0, read_bytes: u64 = 0, output: usize = 0 };

/// **PAST HALF A BUDGET**: the run that will meet an edge next time.
pub fn pastHalf(u: Used, b: interp.Budgets) bool {
    return u.steps * 2 > b.steps or u.reads * 2 > b.reads or u.read_bytes * 2 > b.read_bytes or u.output * 2 > b.output_bytes;
}

/// How the CLI reports a run.
pub const How = struct {
    verbose: bool = false,
    /// What the host did (each program it ran, each log): on `--verbose`.
    records: []const []const u8 = &.{},
    budgets: interp.Budgets = .{},
};

/// **THE CLI'S REPORT** (stderr, after the script's own output): terse.
/// A stop, always, and where: a line in a prelude is that prelude's, by its
/// name (`the Linux prelude:12:5`), never the script's.
pub fn printReport(r: *const script.Result, name: []const u8, preludes: []const parse_mod.Prelude, how: How, w: *std.Io.Writer) std.Io.Writer.Error!void {
    if (how.verbose) {
        try w.print("── effects ({d}) ──\n", .{r.effects.len});
        for (r.effects) |e| try w.print("{s} {s}: {s}\n", .{ e.op, e.path, e.outcome });
    }
    if (how.verbose and how.records.len > 0) {
        try w.writeAll("── ran ──\n");
        for (how.records) |line| try w.print("{s}\n", .{line});
    }
    if (r.stop) |s| {
        try w.print("── stopped at {s}:{d}:{d}{s}{s}{s}: {s}{s}{s}: {s} ({d} steps)\n", .{ placeOf(s.function, name, preludes), s.line, s.column, if (s.function.len > 0) " (in `" else "", s.function, if (s.function.len > 0) "`)" else "", @tagName(s.class), if (s.budget != null) " " else "", if (s.budget) |b| @tagName(b) else "", s.message, r.steps });
        var k = s.trail.len;
        while (k > 0) {
            k -= 1;
            const c = s.trail[k];
            try w.print("   called from {s}:{d}:{d}{s}{s}{s}\n", .{ placeOf(c.function, name, preludes), c.line, c.column, if (c.function.len > 0) " (in `" else "", c.function, if (c.function.len > 0) "`)" else "" });
        }
    } else if (how.verbose) try w.print("── finished ({d} steps)\n", .{r.steps});
    const b = how.budgets;
    if (how.verbose or r.stop != null or pastHalf(.{ .steps = r.steps, .reads = r.reads, .read_bytes = r.read_bytes, .output = r.output.len }, b))
        try w.print("── used: {d} of {d} steps, {d} of {d} reads, {d} of {d} read bytes, {d} of {d} output bytes\n", .{ r.steps, b.steps, r.reads, b.reads, r.read_bytes, b.read_bytes, r.output.len, b.output_bytes });
}

/// The source a line of `function` is in: the core's prelude, a later
/// one's (its functions are shown `<its name>'s f`), or the script.
fn placeOf(function: []const u8, name: []const u8, preludes: []const parse_mod.Prelude) []const u8 {
    if (diag.inPrelude(function)) return "the prelude";
    for (preludes) |p| {
        if (function.len > p.name.len + 3 and std.mem.startsWith(u8, function, p.name) and std.mem.startsWith(u8, function[p.name.len..], "'s ")) return p.name;
    }
    return name;
}

fn refuse(buf: []u8, comptime fmt: []const u8, args: anytype) @typeInfo(@TypeOf(parse)).@"fn".return_type.? {
    return .{ .refused = .{ .why = std.fmt.bufPrint(buf, fmt, args) catch fmt } };
}

fn setBudget(b: *interp.Budgets, name: []const u8, n: u64) bool {
    inline for (settable) |field| {
        if (std.mem.eql(u8, name, field)) {
            const T = @TypeOf(@field(b, field));
            @field(b, field) = std.math.cast(T, n) orelse std.math.maxInt(T);
            return true;
        }
    }
    return false;
}

// ── tests ─────────────────────────────────────────────────────────────────

const testing = std.testing;

fn parsed(argv: []const []const u8, env_root: ?[]const u8) !Command {
    const S = struct {
        var args: [8]interp.Arg = undefined;
        var why: [256]u8 = undefined;
    };
    return switch (parse(argv, env_root, &S.args, &S.why)) {
        .command => |c| c,
        .refused => error.Refused,
    };
}

fn refused(argv: []const []const u8) ![]const u8 {
    const S = struct {
        var args: [2]interp.Arg = undefined;
        var why: [256]u8 = undefined;
    };
    return switch (parse(argv, null, &S.args, &S.why)) {
        .command => error.NotRefused,
        .refused => |r| r.why,
    };
}

test "cli: --undo-on-stop and --args-stdin are read; --args-stdin wants a FILE" {
    const c = try parsed(&.{ "--undo-on-stop", "split.shale", "--args-stdin", "n=1" }, null);
    try testing.expect(c.undo_on_stop and c.args_stdin);
    try testing.expectEqualStrings("split.shale", c.file.?);
    try testing.expectEqualStrings("--args-stdin: stdin is the values of a FILE, and there is no FILE", try refused(&.{ "--args-stdin", "-e", "x" }));
    try testing.expectEqualStrings("--args-stdin: stdin is the values of a FILE, and there is no FILE", try refused(&.{"--args-stdin"}));
}

test "cli: argLines takes NAME=VALUE a line, each value raw, after the command line's, and refuses a name twice" {
    var buf: [8]interp.Arg = undefined;
    var why: [128]u8 = undefined;
    const text =
        \\names=a|b|the site cache keeps what fits, up to its capacity
        \\
        \\doc=It's "quoted", with a \ and $HOME as they are
        \\empty=
    ;
    const got = switch (argLines(text, &.{.{ .name = "from", .value = "src/x.zig" }}, &buf, &why)) {
        .args => |a| a,
        .refused => |r| {
            std.debug.print("{s}\n", .{r.why});
            return error.TestUnexpectedResult;
        },
    };
    try testing.expectEqual(@as(usize, 4), got.len);
    try testing.expectEqualStrings("from", got[0].name);
    try testing.expectEqualStrings("a|b|the site cache keeps what fits, up to its capacity", got[1].value);
    try testing.expectEqualStrings("It's \"quoted\", with a \\ and $HOME as they are", got[2].value);
    try testing.expectEqualStrings("", got[3].value);
    for ([_]struct { []const u8, []const u8 }{
        .{ "x", "--args-stdin: line 1 is not NAME=VALUE" },
        .{ "=v", "--args-stdin: line 1: `` is not a name" },
        .{ "a b=v", "--args-stdin: line 1: `a b` is not a name" },
        .{ "from=y", "--args-stdin: line 1: from is given twice" },
        .{ "n=1\nn=2", "--args-stdin: line 2: n is given twice" },
    }) |c| switch (argLines(c[0], &.{.{ .name = "from", .value = "x" }}, &buf, &why)) {
        .args => return error.TestUnexpectedResult,
        .refused => |r| try testing.expectEqualStrings(c[1], r.why),
    };
}

test "cli: a file and its values, rooted here unless told otherwise" {
    const c = try parsed(&.{ "count.shale", "dir=src", "n=3" }, null);
    try testing.expectEqualStrings(".", c.root);
    try testing.expectEqualStrings("count.shale", c.file.?);
    try testing.expectEqual(@as(usize, 2), c.args.len);
    try testing.expectEqualStrings("dir", c.args[0].name);
    try testing.expectEqualStrings("3", c.args[1].value);
}

test "cli: --root, SHALE_ROOT, --budget, -e" {
    const c = try parsed(&.{ "--budget", "steps=99", "--budget", "reads=7", "-e", "print(1)", "x=" }, "/data");
    try testing.expectEqualStrings("/data", c.root);
    try testing.expectEqual(@as(u64, 99), c.budgets.steps);
    try testing.expectEqual(@as(u64, 7), c.budgets.reads);
    try testing.expectEqualStrings("print(1)", c.source.?);
    try testing.expectEqualStrings("", c.args[0].value);
    const r = try parsed(&.{ "--root", "here", "s.shale" }, "/data");
    try testing.expectEqualStrings("here", r.root);
}

test "cli: what it refuses, said" {
    try testing.expectEqualStrings("--wrte is not an option", try refused(&.{ "--wrte", "a.shale" }));
    // Every run writes, so there is no `--write`.
    try testing.expectEqualStrings("--write is not an option", try refused(&.{ "--write", "a.shale" }));
    try testing.expectEqualStrings("--budget stepz=3: no budget is called stepz", try refused(&.{ "--budget", "stepz=3", "a.shale" }));
    try testing.expectEqualStrings("--budget steps=x: x is not a number", try refused(&.{ "--budget", "steps=x", "a.shale" }));
    try testing.expectEqualStrings("--budget steps=0: a budget is at least 1", try refused(&.{ "--budget", "steps=0", "a.shale" }));
    try testing.expectEqualStrings("one script: b.shale is a second", try refused(&.{ "a.shale", "b.shale" }));
    try testing.expectEqualStrings("one script: a file or -e, not both", try refused(&.{ "a.shale", "-e", "x" }));
    try testing.expectEqualStrings("n=1 before the script: the script first, then NAME=VALUE", try refused(&.{ "n=1", "a.shale" }));
    try testing.expectEqualStrings("n=1 before the script: the script first, then NAME=VALUE", try refused(&.{ "n=1", "-e", "x" }));
    try testing.expectEqualStrings("--root needs a value", try refused(&.{"--root"}));
    try testing.expectEqualStrings("more than 2 values", try refused(&.{ "a.shale", "a=1", "b=2", "c=3" }));
}

test "cli: no FILE and no -e: the script is stdin's, its values still read" {
    // Re-pinned (QUEUE 233): "no script" was refused; stdin is the script.
    const c = try parsed(&.{}, null);
    try testing.expect(c.from_stdin);
    try testing.expect(c.file == null and c.source == null);
    const v = try parsed(&.{ "--verbose", "n=3" }, null);
    try testing.expect(v.from_stdin);
    try testing.expectEqualStrings("3", v.args[0].value);
    try testing.expect(!(try parsed(&.{"a.shale"}, null)).from_stdin);
    try testing.expectEqualStrings("stdin", scriptName(try parsed(&.{}, null)));
    try testing.expectEqualStrings("-e", scriptName(try parsed(&.{ "-e", "x" }, null)));
    try testing.expectEqualStrings("a.shale", scriptName(try parsed(&.{"a.shale"}, null)));
}

test "cli: the CLI's own defaults, and --verbose" {
    const c = try parsed(&.{"a.shale"}, null);
    try testing.expectEqual(@as(usize, 64 * 1024), c.budgets.output_bytes);
    try testing.expectEqual(@as(u64, 64 * 1024 * 1024), c.budgets.call_bytes);
    try testing.expectEqual(@as(u64, 5_000_000), c.budgets.steps);
    try testing.expect(!c.verbose);
    try testing.expect((try parsed(&.{ "--verbose", "a.shale" }, null)).verbose);
    // A budget given still wins.
    try testing.expectEqual(@as(usize, 9), (try parsed(&.{ "--budget", "output_bytes=9", "a.shale" }, null)).budgets.output_bytes);
}

test "cli: used: past half of a budget" {
    const b: interp.Budgets = .{ .steps = 100, .reads = 10, .read_bytes = 1000, .output_bytes = 50 };
    try testing.expect(!pastHalf(.{ .steps = 50, .reads = 5, .read_bytes = 500, .output = 25 }, b));
    try testing.expect(pastHalf(.{ .steps = 51 }, b));
    try testing.expect(pastHalf(.{ .reads = 6 }, b));
    try testing.expect(pastHalf(.{ .read_bytes = 501 }, b));
    try testing.expect(pastHalf(.{ .output = 26 }, b));
}
