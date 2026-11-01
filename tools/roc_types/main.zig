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
//! node of the module's canonical tree: `tag` is its kind (`e_call`, a
//! pattern's `assign`), the span is its region (lines and columns from 1, columns in
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
//!
//! **A HEADER'S SPECS AS THE `roc` CLI TAKES THEM** (`discover`): a spec
//! holding `://` is a URL, refused, as nothing is downloaded here; any other
//! is a path from the app's folder, `platform "platform/main.roc"` as
//! `"./platform/main.roc"`; a platform's absolute path is refused.

const std = @import("std");
const base = @import("base");
const can = @import("can");
const eval = @import("eval");
const types = @import("types");
const roc_target = @import("roc_target");
const roc_build_options = @import("roc_build_options");
const build_options = @import("build_options");
const compile = @import("compile");
const Coordinator = compile.coordinator.Coordinator;
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

/// A spec the `roc` CLI reads as a URL (compile/package_resolution.zig's
/// `specIsUrlLike`); any other is a local path from the app's folder.
fn isUrlSpec(spec: []const u8) bool {
    return std.mem.find(u8, spec, "://") != null;
}

/// A spec the coordinator's own discovery takes (coordinator.zig's
/// `isRelativeSpec`).
fn isDotSpec(spec: []const u8) bool {
    return std.mem.startsWith(u8, spec, "./") or std.mem.startsWith(u8, spec, "../");
}

/// A local spec the CLI takes and the coordinator's discovery refuses.
fn isBareLocalSpec(spec: []const u8) bool {
    return !isUrlSpec(spec) and !isDotSpec(spec) and !std.fs.path.isAbsolute(spec);
}

/// **THE APP, ITS PLATFORM AND PACKAGES REGISTERED** with `coord` as the `roc`
/// CLI takes the header. `Coordinator.discoverAppFromPath` takes a local
/// spec only with `./` or `../` (compile/coordinator.zig:2081, its
/// `isRelativeSpec`), where the CLI's resolver reads any spec without `://`
/// as a path from the app's folder (compile/package_resolution.zig:984 and
/// :1073, `specIsUrlLike` then `resolveLocalPath`). A header the coordinator
/// takes, or one it cannot read (its reports are the coordinator's), goes
/// to `discoverAppFromPath`; one with a bare local spec is registered here,
/// step for step as `discoverAppFromPath` and `registerPlatformPackage` do,
/// the spec joined to the app's folder.
fn discover(coord: *Coordinator, arena: std.mem.Allocator, entry: []const u8) !void {
    const opts: Coordinator.AppDiscoveryOptions = .{ .entry_path = entry };
    const header = compile.app_header.parseAppHeader(coord.roc_ctx, coord.gpa, arena, entry) catch
        return coord.discoverAppFromPath(arena, opts);
    var bare = switch (header.platform_ref) {
        .path_or_url => |spec| isBareLocalSpec(spec),
        .none, .compiler_owned => false,
    };
    for (header.non_platform_packages) |p| {
        if (!isUrlSpec(p.spec) and !isDotSpec(p.spec)) bare = true;
    }
    if (!bare) return coord.discoverAppFromPath(arena, opts);

    const app_dir = std.fs.path.dirname(entry) orelse ".";
    const app_identity = try compile.package_identity.packageIdentityFor(coord.gpa, coord.roc_ctx, .{ .local_path = entry });
    defer coord.gpa.free(app_identity);
    const app_pkg = try coord.ensurePackage(app_identity, app_dir);
    coord.markAppPackage(app_pkg.name);
    const app_module_id = try app_pkg.ensureModule(coord.gpa, base.module_path.getModuleName(entry), entry);
    app_pkg.root_module_id = app_module_id;
    app_pkg.modules.items[app_module_id].depth = 0;
    app_pkg.remaining_modules += 1;
    coord.total_remaining += 1;
    // Read again into the app module's reports, as the coordinator does: it
    // read above, so a failure here is the file changed under the run.
    const info = try compile.app_header.parseAppHeaderReporting(coord.roc_ctx, coord.gpa, arena, entry, &app_pkg.modules.items[app_module_id].reports);

    switch (info.platform_ref) {
        .none => {},
        .path_or_url => |spec| {
            if (std.fs.path.isAbsolute(spec)) return error.AbsolutePlatformPath;
            if (isUrlSpec(spec)) return error.UnsupportedPlatformSpec;
            const main_path = try std.fs.path.join(arena, &.{ app_dir, spec });
            const pf_dir = std.fs.path.dirname(main_path) orelse return error.InvalidPlatformPath;
            // registerPlatformPackage's steps: its identity (the local path),
            // package, public modules and the app's shorthand, then the
            // platform's mark and its `main` module enqueued.
            const pf_pkg = try coord.registerInlinePackageWithRootFile(main_path, pf_dir, app_pkg, info.platform_qualifier);
            coord.markPlatformPackage(pf_pkg.name);
            const pf_module_id = try pf_pkg.ensureModule(coord.gpa, "main", main_path);
            pf_pkg.root_module_id = pf_module_id;
            pf_pkg.modules.items[pf_module_id].depth = 1;
            pf_pkg.remaining_modules += 1;
            coord.total_remaining += 1;
            try coord.enqueueParseTask(pf_pkg.name, pf_module_id);
        },
        .compiler_owned => {
            // Its materialising is the coordinator's own (private) path, and
            // only a bare package spec brings such a header here.
            std.debug.print("roc-types: {s}: a compiler-owned platform beside a package spec without `./`: give the package spec a `./`\n", .{entry});
            std.process.exit(1);
        },
    }

    for (info.non_platform_packages) |p| {
        if (isUrlSpec(p.spec)) return error.UnsupportedPackageSpec;
        const pkg_main_path = try std.fs.path.join(arena, &.{ app_dir, p.spec });
        const pkg_dir = std.fs.path.dirname(pkg_main_path) orelse ".";
        _ = try coord.registerInlinePackageWithRootFile(pkg_main_path, pkg_dir, app_pkg, p.shorthand);
    }

    try coord.enqueueParseTask(app_pkg.name, app_module_id);
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
    discover(&coord, arena, entry) catch |e| switch (e) {
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
