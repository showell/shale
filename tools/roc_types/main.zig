//! **EVERY EXPRESSION'S AND PATTERN'S TYPE, AS ROC'S OWN CHECKER INFERS IT**:
//! `roc-types <app.roc>` checks the app and every module it reaches (its
//! platform's and packages' too) through the compiler's embedding API
//! (`Coordinator`), then walks each checked module with the LSP's visitor
//! (cir_visitor.zig) and writes one fact a line for a program (Shale's
//! cookbook/roc/types.shale reads it). The checker is roc-lang/roc's, at the
//! commit roc-version pins, so the types are what our compiler infers.
//!
//! **THE FORMAT**: the first line says the format and whose checker,
//! `roc-types 1 <roc-version>`, written before anything else; a change a
//! reader must know of is a new number. Then, for each module:
//!   `== <path>`
//!   `t<TAB>expr|pat<TAB>tag<TAB>line<TAB>col<TAB>end_line<TAB>end_col<TAB>type`
//! The path is absolute and holds no `.` or `..` part. A `t` record is a
//! node of the module's canonical tree: `tag` is its kind (`e_call`,
//! `p_assign`), the span is its region (lines and columns from 1, columns in
//! bytes, the end one past, as roc-ast's), and `type` is the checker's, on
//! one line as its type writer prints it, with `\`, tab, newline and carriage
//! return escaped as `\\`, `\t`, `\n`, `\r`. Records come in the visitor's
//! pre-order (a node before the nodes inside it), so of several nodes with
//! one region (a `?`'s desugaring, a nominal tag, an `and`'s `if`) the
//! outermost comes first.
//!
//! **NOTHING HALF-CHECKED**: every report goes to stderr, rendered, before
//! any module. A warning only says so; a report of error severity, or a
//! module the checker left failed or without its environment (named), ends
//! the run with exit 1 and no module written. A reader holds a run that
//! exits 0 to every module's types.
//!
//! The argument is resolved to its real, absolute path here: the compiler
//! refuses a relative entry ("Import Path Case Mismatch"). It must be an
//! app: a type module (no app header) is refused, `NotAnAppHeader`, so a
//! type module's types are read through an app that imports it.

const std = @import("std");
const base = @import("base");
const can = @import("can");
const eval = @import("eval");
const types = @import("types");
const roc_target = @import("roc_target");
const roc_build_options = @import("roc_build_options");
const build_options = @import("build_options");
const Coordinator = @import("compile").coordinator.Coordinator;
const CoreCtx = @import("ctx").CoreCtx;
const CirVisitor = @import("cir_visitor.zig").CirVisitor;
const VisitAction = @import("cir_visitor.zig").VisitAction;

/// The format's number, in its first line.
const format = 1;

const CIR = can.CIR;
const ModuleEnv = can.ModuleEnv;

const Seen = struct {
    is_expr: bool,
    idx: u32,
    tag: []const u8,
    region: base.Region,
};

const Collect = struct {
    gpa: std.mem.Allocator,
    store: *const can.NodeStore,
    out: std.ArrayList(Seen) = .empty,
    // The visitor's callbacks return no error, so a failed append is kept
    // here and raised after the walk.
    oom: bool = false,

    fn add(c: *Collect, s: Seen) void {
        c.out.append(c.gpa, s) catch {
            c.oom = true;
        };
    }
    fn expr(c: *Collect, idx: CIR.Expr.Idx, e: CIR.Expr) VisitAction {
        c.add(.{ .is_expr = true, .idx = @backingInt(idx), .tag = @tagName(e), .region = c.store.getExprRegion(idx) });
        return .continue_traversal;
    }
    fn pat(c: *Collect, idx: CIR.Pattern.Idx, p: CIR.Pattern) VisitAction {
        c.add(.{ .is_expr = false, .idx = @backingInt(idx), .tag = @tagName(p), .region = c.store.getPatternRegion(idx) });
        return .continue_traversal;
    }
};

fn lineCol(starts: []const u32, offset: u32) [2]u32 {
    var line: u32 = 0;
    for (starts, 0..) |s, i| {
        if (s > offset) break;
        line = @intCast(i);
    }
    return .{ line + 1, offset - starts[line] + 1 };
}

fn escaped(out: *std.Io.Writer, s: []const u8) !void {
    for (s) |c| switch (c) {
        '\\' => try out.writeAll("\\\\"),
        '\t' => try out.writeAll("\\t"),
        '\n' => try out.writeAll("\\n"),
        '\r' => try out.writeAll("\\r"),
        else => try out.writeByte(c),
    };
}

/// `path` with its `.` and `..` parts resolved by its text, null for one
/// that is not absolute. The compiler names a module as its import reached
/// it (`app/../platform/Chat.roc`), and a reader keys a module by its path.
fn normal(arena: std.mem.Allocator, path: []const u8) !?[]const u8 {
    if (path.len == 0 or path[0] != '/') return null;
    var parts: std.ArrayList([]const u8) = .empty;
    var it = std.mem.tokenizeScalar(u8, path, '/');
    while (it.next()) |part| {
        if (std.mem.eql(u8, part, ".")) continue;
        if (std.mem.eql(u8, part, "..")) {
            _ = parts.pop();
            continue;
        }
        try parts.append(arena, part);
    }
    var out: std.ArrayList(u8) = .empty;
    for (parts.items) |part| {
        try out.append(arena, '/');
        try out.appendSlice(arena, part);
    }
    if (out.items.len == 0) try out.append(arena, '/');
    return out.items;
}

fn dumpModule(gpa: std.mem.Allocator, out: *std.Io.Writer, path: []const u8, env: *ModuleEnv) !void {
    var c: Collect = .{ .gpa = gpa, .store = &env.store };
    defer c.out.deinit(gpa);
    var v = CirVisitor(Collect).init(&c, .{ .visit_expr_pre = Collect.expr, .visit_pattern_pre = Collect.pat });
    for (env.store.sliceDefs(env.all_defs)) |def_idx| {
        const def = env.store.getDef(def_idx);
        v.walkPattern(&env.store, def.pattern);
        v.walkExpr(&env.store, def.expr);
    }
    v.walkModule(&env.store, env.all_statements);
    if (c.oom) return error.OutOfMemory;

    var tw = try env.initTypeWriter();
    defer tw.deinit();
    const starts = env.getLineStartsAll();
    try out.print("== {s}\n", .{path});
    for (c.out.items) |s| {
        const a = lineCol(starts, s.region.start.offset);
        const b = lineCol(starts, s.region.end.offset);
        // A node's index is its type variable's: the checker gives each
        // canonical node the variable of the same number.
        const tv: types.Var = @fromBackingInt(@intCast(s.idx));
        try tw.write(tv, .one_line);
        try out.print("t\t{s}\t{s}\t{d}\t{d}\t{d}\t{d}\t", .{ if (s.is_expr) "expr" else "pat", s.tag, a[0], a[1], b[0], b[1] });
        try escaped(out, tw.get());
        try out.writeByte('\n');
    }
}

pub fn main(init: std.process.Init) !void {
    const io = init.io;
    const gpa = init.gpa;
    const arena = init.arena.allocator();
    const args = try init.minimal.args.toSlice(arena);
    if (args.len != 2) {
        std.debug.print("roc-types: one argument, an app's .roc (an app, not a type module: read a type module through an app that imports it)\n", .{});
        std.process.exit(2);
    }
    const entry = std.Io.Dir.cwd().realPathFileAlloc(io, args[1], arena) catch |e| {
        std.debug.print("roc-types: {s}: {t}\n", .{ args[1], e });
        std.process.exit(1);
    };

    var out_buf: [64 * 1024]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &out_buf);
    const out = &stdout.interface;
    var err_buf: [4096]u8 = undefined;
    var stderr = std.Io.File.stderr().writer(io, &err_buf);
    const err = &stderr.interface;

    try out.print("roc-types {d} {s}\n", .{ format, build_options.roc_pin });
    // Out at once: a crash in the checker loses what is buffered, and a
    // reader must still see whose format follows.
    try out.flush();

    var builtin_modules = try eval.BuiltinModules.init(gpa);
    defer builtin_modules.deinit();
    var coord = try Coordinator.init(
        gpa,
        .single_threaded,
        1,
        roc_target.RocTarget.detectNative(),
        &builtin_modules,
        roc_build_options.compiler_compatibility_id,
        null,
        CoreCtx.default(gpa, arena, io),
    );
    defer coord.deinit();
    coord.enable_hosted_transform = true;
    try coord.start();
    coord.discoverAppFromPath(arena, .{ .entry_path = entry }) catch |e| switch (e) {
        error.NotAnAppHeader => {
            std.debug.print("roc-types: {s} is not an app (NotAnAppHeader): read a type module through an app that imports it\n", .{entry});
            std.process.exit(1);
        },
        else => return e,
    };
    try coord.coordinatorLoop();

    var errors: usize = 0;
    var reports = coord.iterReports();
    while (reports.next()) |r| {
        if (r.report.severity.isError()) errors += 1;
        try err.print("roc-types: {s}.{s} {t}: {s}\n", .{ r.package_name, r.module_name, r.report.severity, r.report.title });
        try r.report.render(err, .markdown);
        try err.writeByte('\n');
    }

    var failed: usize = 0;
    var pkgs = coord.packages.iterator();
    while (pkgs.next()) |kv| {
        for (kv.value_ptr.*.modules.items) |*m| {
            if (m.completion != .succeeded or m.moduleEnv() == null) {
                try err.print("roc-types: {s}: not checked (phase {t}, {t}{s})\n", .{ m.path, m.phase, m.completion, if (m.moduleEnv() == null) ", no environment" else "" });
                failed += 1;
            } else if (try normal(arena, m.path) == null) {
                try err.print("roc-types: {s}: a module path that is not absolute\n", .{m.path});
                failed += 1;
            }
        }
    }
    if (errors != 0 or failed != 0) {
        try err.print("roc-types: {d} error report(s), {d} module(s) without types: no module written\n", .{ errors, failed });
        try err.flush();
        std.process.exit(1);
    }
    // The warnings before the first module, never among its records.
    try err.flush();

    pkgs = coord.packages.iterator();
    while (pkgs.next()) |kv| {
        for (kv.value_ptr.*.modules.items) |*m| {
            try dumpModule(gpa, out, (try normal(arena, m.path)).?, m.moduleEnv().?);
        }
    }
    try out.flush();
}
