//! Shale, a small script language for a site's own data: Lua's look, a smaller language's
//! semantics, every effect through webbox's `store`, every run inside its budgets. The
//! language, its builtins and its budgets are documented in docs/SHALE.md.
//!
//! Portable as every webbox file is: the host's `Io` comes from `host_io`, the allocator
//! from the caller, and nothing names the host (`portable_test.zig` checks this).

pub const diag = @import("diag.zig");
pub const ast = @import("ast.zig");
pub const lex = @import("lex.zig");
pub const parse = @import("parse.zig");
pub const regex = @import("regex.zig");
pub const value = @import("value.zig");
pub const interp = @import("interp.zig");
pub const builtins = @import("builtins.zig");
pub const builtins_text = @import("builtins_text.zig");
pub const builtins_store = @import("builtins_store.zig");
pub const builtins_walk = @import("builtins_walk.zig");
pub const builtins_read = @import("builtins_read.zig");
pub const builtins_change = @import("builtins_change.zig");
pub const compile = @import("compile.zig");
pub const vm = @import("vm.zig");
pub const process = @import("process.zig");
pub const cli = @import("cli.zig");

const std = @import("std");
const Io = @import("host_io").Io;

pub const Budgets = interp.Budgets;
pub const Options = process.Options;
pub const Arg = interp.Arg;
pub const Effect = interp.Effect;
pub const Stop = diag.Stop;

/// What a run did. It owns the run's arena: every string in it lives there,
/// so the caller reads it, then calls `deinit`.
pub const Result = struct {
    arena: value.Budgeted,
    /// Null when the script finished; where and why it stopped otherwise.
    stop: ?Stop,
    /// What `print` wrote: a prefix of the caller's `out`.
    output: []const u8,
    /// Every store change the script attempted, in order, finished or
    /// stopped: a run has no rollback, so this says what it changed.
    effects: []const Effect,
    steps: u64,
    /// What the run took of its store budgets, which the Linux tool prints at the end
    /// beside what it was allowed.
    reads: u64 = 0,
    read_bytes: u64 = 0,
    changes: u64 = 0,
    /// The work counted in tests (`interp.Run.work`); 0 outside them.
    work: u64 = 0,
    /// The most Zig stack the parse or the walk took, in bytes (docs/SHALE.md,
    /// "The Zig stack").
    stack_bytes: usize = 0,
    /// The arena bytes the run held at its end, never past its budget.
    arena_bytes: usize = 0,
    /// The yields that collected (`interp.Run.collections`).
    collections: u64 = 0,
    /// Whether it called a builtin that changes the store (`interp.Run`).
    tried_change: bool = false,
    /// The machine's memory, under its own budget: the parse's tree, and while the run
    /// runs, its code, frames and operands. Kept apart from the values so that a `yield`
    /// that trades their arena for a fresh one leaves the code's literals and the
    /// functions' names where they are. Null only when it could not be allocated.
    machine: ?*MachineMemory = null,

    pub fn finished(r: *const Result) bool {
        return r.stop == null;
    }

    pub fn deinit(r: *Result) void {
        r.arena.deinit();
        if (r.machine) |m| {
            m.tree.deinit();
            const backing = m.budget.backing;
            m.budget.deinit();
            backing.destroy(m);
        }
    }
};

/// The machine's budgeted memory, and the tree's arena over it. Heap-allocated, so the
/// arena's pointer to the budget stays valid as a Result moves.
pub const MachineMemory = process.MachineMemory;

/// Writes what a run did: its effects, a line each, then how it ended, a stop named as
/// `name:line:column` (`the prelude:line:column` for a stop in the prelude). After a stop in a function, a line follows for each call that led
/// there, innermost first (`called from name:line:column (in `f`)`); a call in the prelude
/// is `the prelude:line:column`, its line the prelude's. The Linux runner and the script
/// route both print this layout, so a run reads the same on Linux as on the site.
pub fn report(r: *const Result, name: []const u8, w: *std.Io.Writer) std.Io.Writer.Error!void {
    try w.print("── effects ({d}) ──\n", .{r.effects.len});
    for (r.effects) |e| try w.print("{s} {s}: {s}\n", .{ e.op, e.path, e.outcome });
    if (r.stop) |s| {
        const place = if (diag.inPrelude(s.function)) "the prelude" else name;
        try w.print("── stopped at {s}:{d}:{d}{s}{s}{s}: {s}{s}{s}: {s} ({d} steps)\n", .{ place, s.line, s.column, if (s.function.len > 0) " (in `" else "", s.function, if (s.function.len > 0) "`)" else "", @tagName(s.class), if (s.budget != null) " " else "", if (s.budget) |b| @tagName(b) else "", s.message, r.steps });
        var k = s.trail.len;
        while (k > 0) {
            k -= 1;
            const c = s.trail[k];
            const at = if (diag.inPrelude(c.function)) "the prelude" else name;
            try w.print("   called from {s}:{d}:{d}{s}{s}{s}\n", .{ at, c.line, c.column, if (c.function.len > 0) " (in `" else "", c.function, if (c.function.len > 0) "`)" else "" });
        }
    } else try w.print("── finished ({d} steps)\n", .{r.steps});
}

/// Parses `source` and runs it against the store under `root`, inside `budgets`, with `now`
/// the only time it can know. Every value it makes comes from one arena over `backing`;
/// `print` writes into `out`, and the output budget is at most `out.len`. Always returns a
/// Result: a source however hostile ends finished or stopped.
pub fn run(io: Io, backing: std.mem.Allocator, root: []const u8, source: []const u8, budgets: Budgets, now: i64, out: []u8) Result {
    return runWith(io, backing, root, source, budgets, now, out, .{});
}

/// `run`, with options: the mode and the values given by name.
pub fn runWith(io: Io, backing: std.mem.Allocator, root: []const u8, source: []const u8, budgets: Budgets, now: i64, out: []u8, options: Options) Result {
    // In tests every run goes through the gate: the VM's run, checked against the
    // tree-walker's on a copy of the store.
    if (@import("builtin").is_test) return @import("gate_test.zig").run(io, backing, root, source, budgets, now, out, options);
    return runOn(.vm, io, backing, root, source, budgets, now, out, options);
}

/// What runs a script. The tests' gate holds every engine to the same run.
pub const Engine = union(enum) {
    /// The VM, which the route and the runner use.
    vm,
    /// The tree-walker, kept as a second implementation.
    walker,
    /// The VM in slices of this many steps, each a pause the next resumes (`vm.Task`).
    vm_sliced: u64,
    /// `vm_sliced`, as a `background` run, which only reads. For a script that changes
    /// nothing, it is the same run.
    vm_background: u64,
};

pub fn runOn(engine: Engine, io: Io, backing: std.mem.Allocator, root: []const u8, source: []const u8, budgets: Budgets, now: i64, out: []u8, options: Options) Result {
    // The VM runs through its `process.zig` contract, so a one-shot run is exactly what
    // a host runs, in slices or in one.
    switch (engine) {
        .walker => {},
        .vm, .vm_sliced, .vm_background => {
            const slice: u64 = switch (engine) {
                .vm_sliced, .vm_background => |n| n,
                else => std.math.maxInt(u64),
            };
            var opts = options;
            if (engine == .vm_background) opts.mode = .background;
            const p = process.Process.startWith(io, backing, root, source, opts, budgets, now, out.len) catch {
                return .{ .arena = .{ .backing = backing, .limit = budgets.arena_bytes }, .stop = .{ .line = 1, .column = 1, .class = .budget, .budget = .machine, .message = "the host could not hold the run" }, .output = out[0..0], .effects = &.{}, .steps = 0 };
            };
            while (p.step(.{ .steps = slice }) == .paused) {}
            return p.intoResult(Result, out);
        },
    }
    var result: Result = .{ .arena = .{ .backing = backing, .limit = budgets.arena_bytes }, .stop = null, .output = out[0..0], .effects = &.{}, .steps = 0 };
    const budgeted = &result.arena;
    const alloc = budgeted.allocator();
    if (source.len > budgets.source_bytes) {
        result.stop = .{ .line = 1, .column = 1, .class = .budget, .budget = .source, .message = "the source is larger than the source budget" };
        return result;
    }
    var stop: ?Stop = null;
    const top = @frameAddress();
    const machine_stop: Stop = .{ .line = 1, .column = 1, .class = .budget, .budget = .machine, .message = "the machine's memory (the code, the frames, the operands) is past its budget: raise it with `--budget machine_bytes=N` or the script's `-- budget: machine_bytes=N` line" };
    const mm = backing.create(MachineMemory) catch {
        result.stop = machine_stop;
        return result;
    };
    mm.* = .{ .budget = .{ .backing = backing, .limit = budgets.machine_bytes }, .tree = undefined };
    mm.tree = .init(mm.budget.allocator());
    result.machine = mm;
    const tree = mm.tree.allocator();
    const program = tree.create(ast.Program) catch {
        result.stop = machine_stop;
        return result;
    };
    program.* = parse.parseMeasured(tree, mm.budget.allocator(), source, budgets.nesting, &stop, &result.stack_bytes, options.preludes) catch |e| {
        result.stop = switch (e) {
            error.Parse => stop.?,
            error.OutOfMemory => machine_stop,
        };
        return result;
    };
    if (parse.unreadArgStop(tree, program, options.args) catch {
        result.stop = machine_stop;
        return result;
    }) |s| {
        result.stop = s;
        return result;
    }
    var r: interp.Run = .{
        .io = io,
        .alloc = alloc,
        .machine = mm.budget.allocator(),
        .machine_heap = mm.budget.heap(),
        .budgeted = budgeted,
        .budgets = budgets,
        .root = root,
        .now = now,
        .program = program,
        .out = out,
        .out_cap = @min(out.len, budgets.output_bytes),
        .read_only = options.mode == .background,
        .args = options.args,
        .host = options.host,
        .path_hook = options.path_hook,
        .line_hook = options.line_hook,
        .diffs = options.diffs,
    };
    defer r.deinit();
    std.debug.assert(engine == .walker);
    // A stop is the run's answer, in `r.stop`.
    r.main() catch {};
    result.stop = r.stop;
    result.output = out[0..r.out_len];
    result.effects = r.effects.items;
    result.steps = r.steps;
    result.reads = r.reads;
    result.read_bytes = r.read_bytes;
    result.changes = r.changes;
    result.collections = r.collections;
    result.tried_change = r.tried_change;
    result.work = r.work;
    result.stack_bytes = @max(result.stack_bytes, top -| r.stack_low);
    result.arena_bytes = budgeted.held;
    return result;
}

test {
    _ = lex;
    _ = parse;
    _ = @import("parse_test.zig");
    _ = value;
    _ = @import("interp_test.zig");
    _ = @import("builtins_test.zig");
    _ = @import("prelude_test.zig");
    _ = @import("builtins_cookbook_test.zig");
    _ = @import("examples_test.zig");
    _ = @import("portable_test.zig");
    _ = @import("regex_test.zig");
    _ = @import("hunt_guard_test.zig");
    _ = builtins;
    _ = @import("charge_test.zig");
    _ = @import("gate_test.zig");
    _ = @import("errors_test.zig");
    _ = @import("yield_test.zig");
    _ = @import("budget_test.zig");
    _ = @import("process_test.zig");
    _ = @import("agent_test.zig");
    _ = @import("host_test.zig");
    _ = @import("cli_test.zig");
    _ = cli;
}
