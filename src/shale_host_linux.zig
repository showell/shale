//! The Linux host the CLI hands the core: `host(name, ...)` calls (`call`) and the
//! mount-aware path resolution (`resolve`). A path whose first part has a `:` is a
//! mount's (`runs:x/test.log`). Metal runs the same core with no host: its `host`
//! stops and its paths are the root's.

const std = @import("std");
const webbox = @import("webbox");
const shale = @import("shale");
const script = shale.script;
const interp = script.interp;
const ast = script.ast;
const Value = script.value.Value;
const Run = interp.Run;
const Error = interp.Error;
const store = webbox.store;
const keep = @import("shale_keep.zig");
const spawn = @import("shale_spawn.zig");
const setup = @import("shale_host_setup.zig");
const host_run = @import("shale_host_run.zig");
const runProgram = host_run.runProgram;
pub const linux_prelude = setup.linux_prelude;
pub const git_prelude = setup.git_prelude;
pub const Mount = setup.Mount;
pub const parseMounts = setup.parseMounts;
pub const loadMounts = setup.loadMounts;
pub const loadPreludes = setup.loadPreludes;
pub const baseEnv = setup.baseEnv;

/// The most programs one run may start.
pub const runs_most = 64;

/// The most a `sleep` waits in one call: ten minutes.
pub const sleep_most_ms = 600_000;

/// Where a program's output goes as it comes: the scrubber, then the log
/// and the keep (or `whole`).
pub const Sink = struct {
    host: *Host,
    scrub: *Scrub,
    pub const Error = error{OutOfMemory};
    pub fn write(s: *Sink, bytes: []const u8) Sink.Error!void {
        try s.scrub.write(bytes);
    }
};

/// Starts a program and streams its output into `sink`. This is the only part a
/// gated test fakes; the options, filter, keep and charges around it are the
/// real code in both engines.
pub const Spawn = *const fn (ctx: ?*anyopaque, alloc: std.mem.Allocator, spec: spawn.Spec, sink: *Sink) (spawn.Error || Sink.Error)!spawn.Outcome;

fn realSpawn(_: ?*anyopaque, alloc: std.mem.Allocator, spec: spawn.Spec, sink: *Sink) (spawn.Error || Sink.Error)!spawn.Outcome {
    return spawn.run(alloc, spec, sink);
}

/// One line the CLI lists after the run: a program run, with its want, or a log
/// written.
pub const Record = struct { line: []const u8 };

/// Where a run's time went, for the line after its report: the programs
/// it ran, its waits on jobs and its sleeps (`now()` is the run's start, so
/// a script cannot time itself).
pub const Time = struct {
    runs: u32 = 0,
    run_ms: u64 = 0,
    slowest_ms: u64 = 0,
    /// The slowest program's argv, cut short; the host's allocator's.
    slowest: []const u8 = "",
    waits: u32 = 0,
    wait_ms: u64 = 0,
    sleep_ms: u64 = 0,

    /// Said after the report when it passes a second, or always with `--verbose`.
    pub fn worthSaying(t: Time, verbose: bool) bool {
        return (verbose and (t.runs > 0 or t.waits > 0 or t.sleep_ms > 0)) or t.run_ms + t.wait_ms + t.sleep_ms >= 1000;
    }

    /// `── time: 2 programs, 1.4 s in all; the slowest 1.2 s: zig build test; 1 job wait, 40.0 s; slept 0.3 s`
    pub fn print(t: Time, w: *std.Io.Writer) std.Io.Writer.Error!void {
        try w.print("── time: {d} program{s}, ", .{ t.runs, if (t.runs == 1) "" else "s" });
        try seconds(w, t.run_ms);
        try w.writeAll(" in all");
        if (t.runs > 0) {
            try w.writeAll("; the slowest ");
            try seconds(w, t.slowest_ms);
            try w.print(": {s}", .{t.slowest});
        }
        if (t.waits > 0) {
            try w.print("; {d} job wait{s}, ", .{ t.waits, if (t.waits == 1) "" else "s" });
            try seconds(w, t.wait_ms);
        }
        if (t.sleep_ms > 0) {
            try w.writeAll("; slept ");
            try seconds(w, t.sleep_ms);
        }
        try w.writeAll("\n");
    }

    fn seconds(w: *std.Io.Writer, ms: u64) std.Io.Writer.Error!void {
        try w.print("{d}.{d} s", .{ ms / 1000, ms % 1000 / 100 });
    }

    /// Counts one program's `ms`, keeping its argv if it is the slowest.
    pub fn ran(t: *Time, alloc: std.mem.Allocator, argv: []const u8, ms: u64) void {
        t.runs += 1;
        t.run_ms += ms;
        if (ms < t.slowest_ms and t.runs > 1) return;
        const cut = argv[0..@min(argv.len, 80)];
        const kept = alloc.dupe(u8, cut) catch return;
        if (t.slowest.len > 0) alloc.free(t.slowest);
        t.slowest = kept;
        t.slowest_ms = ms;
    }
};

pub const Host = struct {
    /// Where the run's time went (`Time`).
    time: Time = .{},
    alloc: std.mem.Allocator,
    /// The root as the CLI was given it, an absolute OS path. A mount inside it
    /// is taken against the run's own root, so each engine of a gated test, in
    /// its own copy of the root, uses its own copy of the mount.
    root_abs: []const u8,
    /// The process's working folder: what a relative root is against.
    cwd_abs: []const u8,
    mounts: []const Mount = &.{},
    /// Where the mounts came from, for a stop that names them.
    mounts_from: []const u8 = "~/.config/shale/mounts",
    /// The environment a program inherits, `K=V` each (`ZIG_PROGRESS` out).
    env: []const []const u8 = &.{},
    /// The program a job runs under (`shale --reap`). Null means no jobs.
    reaper: ?[]const u8 = null,
    spawn_fn: Spawn = realSpawn,
    spawn_ctx: ?*anyopaque = null,
    records: std.ArrayList(Record) = .empty,
    /// `--undo-on-stop`: record each path's state before its first change, so a
    /// run that stops can put back what it changed (`undoChanges`).
    undo: bool = false,
    journal: std.ArrayList(Prior) = .empty,
    journal_bytes: usize = 0,

    pub fn deinit(h: *Host) void {
        if (h.time.slowest.len > 0) h.alloc.free(h.time.slowest);
        for (h.records.items) |r| h.alloc.free(r.line);
        h.records.deinit(h.alloc);
        for (h.journal.items) |p| {
            h.alloc.free(p.abs);
            switch (p.was) {
                .file => |f| h.alloc.free(f.bytes),
                else => {},
            }
        }
        h.journal.deinit(h.alloc);
    }

    pub fn options(h: *Host) struct { host: interp.Host, path_hook: interp.PathHook } {
        return .{ .host = .{ .ctx = h, .call = call }, .path_hook = .{ .ctx = h, .resolve = resolve, .may_change = mayChange } };
    }

    pub fn record(h: *Host, comptime fmt: []const u8, args: anytype) void {
        const line = std.fmt.allocPrint(h.alloc, fmt, args) catch return;
        h.records.append(h.alloc, .{ .line = line }) catch h.alloc.free(line);
    }
};

/// Resolves a mount's path (`runs:x/y`) to the mount's folder joined with the
/// rest, which follows the root's rules (no `..`, not absolute, FAT names).
/// Returns `null` for a root path.
fn resolve(ctx: *anyopaque, run: *Run, pos: ast.Pos, path: []const u8) Error!?[]const u8 {
    const h: *Host = @ptrCast(@alignCast(ctx));
    const first_end = std.mem.indexOfScalar(u8, path, '/') orelse path.len;
    const colon = std.mem.indexOfScalar(u8, path[0..first_end], ':') orelse {
        // The core checks a root path's names; we refuse links only once every
        // part is a name, so a bad name gets the core's message.
        var it = std.mem.splitScalar(u8, path, '/');
        while (it.next()) |part| if (!store.fatName(part)) return null;
        try refuseLinks(run, pos, setup.trimSlash(run.root), path, "");
        if (!anySecrets(h)) return null;
        const root = try rootOf(h, run, pos);
        try refuseSecretsUnder(h, run, pos, path, if (path.len == 0) root else std.fmt.allocPrint(run.alloc, "{s}/{s}", .{ root, path }) catch return run.oom(pos));
        return null;
    };
    const m = try mountOf(h, run, pos, path, colon);
    if (m.secrets) return run.fail(pos, .refused, "\"{s}\": the mount `{s}` holds secrets: only `run`'s `secrets` (or `http_get`'s `bearer`) may name its files", .{ path, m.name });
    const os = try mountPath(h, run, pos, path, colon, m);
    try refuseSecretsUnder(h, run, pos, path, try absolute(h, run, pos, os));
    return os;
}

/// A secret's file, for `run`'s `secrets`: its OS path, through any mount,
/// a `secrets` one included (which every other path refuses).
pub fn secretPath(h: *Host, run: *Run, pos: ast.Pos, v: Value) Error![]const u8 {
    const path = switch (v) {
        .string => |s| s,
        else => return osPath(h, run, pos, v, false),
    };
    const first_end = std.mem.indexOfScalar(u8, path, '/') orelse path.len;
    const colon = std.mem.indexOfScalar(u8, path[0..first_end], ':') orelse return osPath(h, run, pos, v, false);
    const m = try mountOf(h, run, pos, path, colon);
    if (!m.secrets) return osPath(h, run, pos, v, false);
    if (colon + 1 == path.len) return run.fail(pos, .argument, "\"{s}\": a secret is a file in the mount, not the mount", .{path});
    return absolute(h, run, pos, try mountPath(h, run, pos, path, colon, m));
}

/// A mount's folder as this run sees it: one inside the root is taken
/// against the run's own root (each engine of a gated test has its copy).
fn runDirOf(h: *Host, run: *Run, pos: ast.Pos, m: Mount) Error![]const u8 {
    const inside = setup.inFolder(m.dir, h.root_abs);
    if (!inside) return m.dir;
    return std.fmt.allocPrint(run.alloc, "{s}{s}", .{ try rootOf(h, run, pos), m.dir[h.root_abs.len..] }) catch run.oom(pos);
}

fn anySecrets(h: *const Host) bool {
    for (h.mounts) |m| if (m.secrets) return true;
    return false;
}

/// Refuses a program's argument that names a `secrets` mount's folder: a
/// token is handed over as `secrets`, never as a path a program reads. Used
/// by `run` and `job_start`, with or without a `want`.
pub fn refuseSecretArgs(h: *Host, run: *Run, pos: ast.Pos, who: []const u8, argv: []const []const u8, cwd: []const u8, env: ?*script.value.Table) Error!void {
    for (h.mounts) |m| {
        if (!m.secrets) continue;
        const dir = try runDirOf(h, run, pos, m);
        for (argv, 1..) |arg, i| switch (try namesFolder(run, pos, arg, cwd, dir)) {
            .none => {},
            .inside => return run.fail(pos, .refused, "`{s}`'s argument {d} names the mount `{s}`'s folder, which holds secrets: hand one over as `secrets`", .{ who, i, m.name }),
            .holds => return run.fail(pos, .refused, "`{s}`'s argument {d} holds the mount `{s}`'s folder, which holds secrets: name a folder beside it", .{ who, i, m.name }),
        };
        if (env) |t| for (t.map.keys(), t.map.values()) |k, x| switch (x) {
            .string => |v| if (try namesFolder(run, pos, v, cwd, dir) != .none)
                return run.fail(pos, .refused, "`{s}`'s `env` {s} names the mount `{s}`'s folder, which holds secrets: hand one over as `secrets`", .{ who, k, m.name }),
            else => {},
        };
    }
}

/// Whether `arg`, as a program may take it (whole, or after an option's
/// `=`), reaches `dir`: inside it, by its text, resolved against the
/// program's `cwd`, or through a link; or holding it, a folder above it
/// (`cp -r .`). An argv guard is no sandbox (SHALE.md, "The store"): a
/// prefix (`-T<path>`), or a path a program builds, still reaches past it.
fn namesFolder(run: *Run, pos: ast.Pos, arg: []const u8, cwd: []const u8, dir: []const u8) Error!enum { none, inside, holds } {
    if (std.mem.indexOf(u8, arg, dir) != null) return .inside;
    const after_eq = if (std.mem.indexOfScalar(u8, arg, '=')) |e| arg[e + 1 ..] else "";
    for ([_][]const u8{ arg, after_eq }) |p| {
        if (p.len == 0) continue;
        try run.charge(pos, 1);
        const abs = std.fs.path.resolve(run.alloc, &.{ cwd, p }) catch return run.oom(pos);
        if (setup.inFolder(abs, dir)) return .inside;
        if (setup.inFolder(dir, abs)) return .holds;
        var buf: [std.fs.max_path_bytes]u8 = undefined;
        if (std.Io.Dir.cwd().realPathFile(run.io, abs, &buf)) |n| {
            if (setup.inFolder(buf[0..n], dir)) return .inside;
            if (setup.inFolder(dir, buf[0..n])) return .holds;
        } else |_| {}
    }
    return .none;
}

/// A path (`shown`, at `abs`) that reaches a `secrets` mount's folder by
/// another way, the root or another mount, is refused as the mount's own
/// path is. A folder above it may be listed: names are not secrets.
fn refuseSecretsUnder(h: *Host, run: *Run, pos: ast.Pos, shown: []const u8, abs: []const u8) Error!void {
    for (h.mounts) |m| {
        if (!m.secrets) continue;
        const dir = try runDirOf(h, run, pos, m);
        if (setup.inFolder(abs, dir))
            return run.fail(pos, .refused, "\"{s}\" is in the mount `{s}`, which holds secrets", .{ shown, m.name });
    }
}

/// The mount `path` names before its `:` at `colon`; a stop naming the
/// mounts there are when it is none.
fn mountOf(h: *Host, run: *Run, pos: ast.Pos, path: []const u8, colon: usize) Error!Mount {
    const name = path[0..colon];
    return for (h.mounts) |m| {
        if (std.mem.eql(u8, m.name, name)) break m;
    } else {
        var names: std.ArrayList(u8) = .empty;
        for (h.mounts, 0..) |m, i| {
            if (i > 0) names.appendSlice(run.alloc, ", ") catch return run.oom(pos);
            names.print(run.alloc, "`{s}`", .{m.name}) catch return run.oom(pos);
        }
        if (h.mounts.len == 0) return run.fail(pos, .argument, "\"{s}\": `{s}:` is not a mount: there are none ({s})", .{ path, name, h.mounts_from });
        return run.fail(pos, .argument, "\"{s}\": `{s}:` is not a mount: the mounts are {s} ({s})", .{ path, name, names.items, h.mounts_from });
    };
}

/// `path` (the mount `m`'s, its `:` at `colon`) as an OS path, the rest
/// held to the root's rules.
fn mountPath(h: *Host, run: *Run, pos: ast.Pos, path: []const u8, colon: usize, m: Mount) Error![]const u8 {
    // `m:.` is the mount's folder, as `.` is the root.
    const rest = if (std.mem.eql(u8, path[colon + 1 ..], ".")) "" else path[colon + 1 ..];
    if (rest.len > 0 and rest[0] == '/') return run.fail(pos, .argument, "\"{s}\" is absolute inside its mount: a mount's paths are relative to it", .{path});
    if (rest.len > 0) {
        var it = std.mem.splitScalar(u8, rest, '/');
        while (it.next()) |part| if (!store.fatName(part)) return run.fail(pos, .argument, "\"{s}\" has a part (\"{s}\") that is not a name: no `.`, `..`, empty parts or characters FAT refuses", .{ path, part });
    }
    // A missing mount folder stops the run only when the mount is used.
    if (std.Io.Dir.cwd().openDir(run.io, m.dir, .{})) |d| d.close(run.io) else |e| switch (e) {
        error.FileNotFound, error.NotDir => return run.fail(pos, .store, "the mount `{s}` is {s}, which is not a folder there", .{ m.name, m.dir }),
        else => return run.fail(pos, .store, "the mount `{s}` is {s}, which cannot be opened: {s}", .{ m.name, m.dir, @errorName(e) }),
    }
    const inside = setup.inFolder(m.dir, h.root_abs);
    const dir = if (inside) std.fmt.allocPrint(run.alloc, "{s}{s}", .{ try rootOf(h, run, pos), m.dir[h.root_abs.len..] }) catch return run.oom(pos) else m.dir;
    try refuseLinks(run, pos, dir, rest, path[0 .. colon + 1]);
    if (rest.len == 0) return dir;
    return std.fmt.allocPrint(run.alloc, "{s}{s}{s}", .{ dir, if (std.mem.eql(u8, dir, "/")) "" else "/", rest }) catch run.oom(pos);
}

/// Stops the run if any part of `rest` below `base` is a symbolic link: the
/// store follows none, for reads, changes, `exists`, `stat` and listings alike.
/// Each part is checked with `lstat`, charging a step; a missing part ends the
/// walk. `walk` skips the links it lists, since a listed name is not a given path.
fn refuseLinks(run: *Run, pos: ast.Pos, base: []const u8, rest: []const u8, shown_prefix: []const u8) Error!void {
    if (rest.len == 0) return;
    var at: std.ArrayList(u8) = .empty;
    at.appendSlice(run.alloc, base) catch return run.oom(pos);
    var it = std.mem.splitScalar(u8, rest, '/');
    var shown_end: usize = 0;
    while (it.next()) |part| {
        try run.charge(pos, 1);
        at.append(run.alloc, '/') catch return run.oom(pos);
        at.appendSlice(run.alloc, part) catch return run.oom(pos);
        shown_end += part.len + @intFromBool(shown_end > 0);
        const st = std.Io.Dir.cwd().statFile(run.io, at.items, .{ .follow_symlinks = false }) catch return;
        if (st.kind == .sym_link) return run.fail(pos, .refused, "{s}{s} is a symbolic link: the store follows none", .{ shown_prefix, rest[0..shown_end] });
    }
}

/// Refuses a change through a read-only mount, whether by the mount's name or by
/// a root path that reaches into its folder. Journals the path under `--undo-on-stop`.
fn mayChange(ctx: *anyopaque, run: *Run, pos: ast.Pos, rel: []const u8, full: []const u8) Error!void {
    const h: *Host = @ptrCast(@alignCast(ctx));
    const abs = try absolute(h, run, pos, setup.trimSlash(full));
    for (h.mounts) |m| {
        if (m.access != .read_only) continue;
        const named = std.mem.startsWith(u8, rel, m.name) and rel.len > m.name.len and rel[m.name.len] == ':';
        const inside = setup.inFolder(m.dir, h.root_abs);
        const dir = if (inside) std.fmt.allocPrint(run.alloc, "{s}{s}", .{ try rootOf(h, run, pos), m.dir[h.root_abs.len..] }) catch return run.oom(pos) else m.dir;
        const under = setup.inFolder(abs, dir);
        if (named or (under and !underWritableWithin(h, run, pos, abs, dir))) return run.fail(pos, .refused, "{s}: mount `{s}` is read-only", .{ rel, m.name });
    }
    if (h.undo) try noteBefore(h, run, pos, abs);
}

/// Whether `abs` is under a writable mount whose folder is inside `ro_dir`
/// (`memory = ~/.claude/projects/x/memory` inside `projects =
/// ~/.claude/projects ro`): the nearer mount decides.
fn underWritableWithin(h: *Host, run: *Run, pos: ast.Pos, abs: []const u8, ro_dir: []const u8) bool {
    for (h.mounts) |m| {
        if (m.access == .read_only) continue;
        const dir = runDirOf(h, run, pos, m) catch return false;
        if (dir.len <= ro_dir.len or !setup.inFolder(dir, ro_dir)) continue;
        if (setup.inFolder(abs, dir)) return true;
    }
    return false;
}

/// A path as it was before the run's first change of it.
pub const Prior = struct {
    abs: []const u8,
    was: union(enum) {
        /// Nothing was there; undo deletes what the run made.
        absent,
        /// A folder; undo only reports it if it is gone.
        dir,
        file: struct { bytes: []const u8, permissions: @FieldType(std.Io.File.Stat, "permissions") },
    },
};

/// The most bytes `--undo-on-stop` journals. A change past it stops the run, so
/// nothing is changed that could not be put back.
pub const journal_most = 64 << 20;

/// Journals what is at `abs_in` before its first change; later changes are no-ops.
fn noteBefore(h: *Host, run: *Run, pos: ast.Pos, abs_in: []const u8) Error!void {
    const abs = setup.trimSlash(abs_in);
    for (h.journal.items) |p| if (std.mem.eql(u8, p.abs, abs)) return;
    const io = run.io;
    const cwd = std.Io.Dir.cwd();
    var bytes: ?[]u8 = null;
    errdefer if (bytes) |b| h.alloc.free(b);
    const was: @FieldType(Prior, "was") = if (cwd.statFile(io, abs, .{ .follow_symlinks = false })) |st| switch (st.kind) {
        .directory => .dir,
        .file => blk: {
            if (st.size > journal_most - h.journal_bytes) return run.fail(pos, .refused, "--undo-on-stop keeps at most {d} bytes of what a run changes, and {s} would pass it", .{ journal_most, abs });
            bytes = cwd.readFileAlloc(io, abs, h.alloc, .limited(journal_most - h.journal_bytes + 1)) catch |e| return run.fail(pos, .store, "--undo-on-stop: {s} cannot be read ({t}), so it could not be put back", .{ abs, e });
            h.journal_bytes += bytes.?.len;
            break :blk .{ .file = .{ .bytes = bytes.?, .permissions = st.permissions } };
        },
        else => return run.fail(pos, .refused, "--undo-on-stop: {s} is neither a file nor a folder, and could not be put back", .{abs}),
    } else |e| switch (e) {
        error.FileNotFound => .absent,
        else => return run.fail(pos, .store, "--undo-on-stop: {s} cannot be looked at ({t}), so it could not be put back", .{ abs, e }),
    };
    const owned = h.alloc.dupe(u8, abs) catch return run.oom(pos);
    h.journal.append(h.alloc, .{ .abs = owned, .was = was }) catch {
        h.alloc.free(owned);
        return run.oom(pos);
    };
}

/// Puts back a stopped run's changes, newest first: a file's bytes restored, a
/// path that was not there deleted (a folder with its contents). A removed
/// folder and a program's changes cannot be restored; each such path is
/// reported on `w` and the result is false.
pub fn undoChanges(h: *Host, io: std.Io, w: *std.Io.Writer) !bool {
    const cwd = std.Io.Dir.cwd();
    var all = true;
    var k = h.journal.items.len;
    while (k > 0) {
        k -= 1;
        const p = h.journal.items[k];
        switch (p.was) {
            .absent => cwd.deleteFile(io, p.abs) catch |e| switch (e) {
                error.FileNotFound => {},
                error.IsDir => cwd.deleteTree(io, p.abs) catch |de| {
                    try w.print("── not put back: {s}: the folder the run made ({t})\n", .{ p.abs, de });
                    all = false;
                },
                else => {
                    try w.print("── not put back: {s}: what the run made ({t})\n", .{ p.abs, e });
                    all = false;
                },
            },
            .file => |f| cwd.writeFile(io, .{ .sub_path = p.abs, .data = f.bytes, .flags = .{ .permissions = f.permissions } }) catch |e| {
                try w.print("── not put back: {s} ({t})\n", .{ p.abs, e });
                all = false;
            },
            .dir => if (cwd.statFile(io, p.abs, .{ .follow_symlinks = false })) |_| {} else |_| {
                try w.print("── not put back: {s}: a folder removed whole, and its files with it\n", .{p.abs});
                all = false;
            },
        }
    }
    try w.print("── undone: {d} path(s) the run changed put back{s}\n", .{ h.journal.items.len, if (all) "" else ", but not all (above)" });
    return all;
}

/// The run's own root, absolute.
pub fn rootOf(h: *Host, run: *Run, pos: ast.Pos) Error![]const u8 {
    return absolute(h, run, pos, setup.trimSlash(run.root));
}

fn absolute(h: *Host, run: *Run, pos: ast.Pos, p: []const u8) Error![]const u8 {
    if (std.fs.path.isAbsolute(p)) return p;
    // Resolved, so `.` is the folder itself: a script that takes the folder
    // above `real(".")` gets its parent, not itself.
    return std.fs.path.resolve(run.alloc, &.{ h.cwd_abs, p }) catch run.oom(pos);
}

/// A Shale path's OS path, through the mounts or the root, made absolute.
pub fn osPath(h: *Host, run: *Run, pos: ast.Pos, v: Value, allow_root: bool) Error![]const u8 {
    const p = try script.builtins_store.pathArg(run, pos, .host, v, allow_root);
    return absolute(h, run, pos, setup.trimSlash(p.full()));
}

fn call(ctx: *anyopaque, run: *Run, pos: ast.Pos, name: []const u8, args: []const Value) Error!Value {
    const h: *Host = @ptrCast(@alignCast(ctx));
    if (std.mem.eql(u8, name, "real")) {
        if (args.len != 1) return run.fail(pos, .argument, "`real` takes one path, given {d}", .{args.len});
        return .{ .string = try osPath(h, run, pos, args[0], true) };
    }
    if (std.mem.eql(u8, name, "run")) return runProgram(h, run, pos, args);
    // A wait that is no program, so it spends none of `runs_most`.
    if (std.mem.eql(u8, name, "sleep")) {
        if (args.len != 1) return run.fail(pos, .argument, "`sleep` takes one argument, the ms, given {d}", .{args.len});
        const ms = switch (args[0]) {
            .int => |n| n,
            else => return run.fail(pos, .type, "`sleep`'s ms must be an integer, not {s}", .{args[0].typeName()}),
        };
        if (ms < 0 or ms > sleep_most_ms) return run.fail(pos, .argument, "`sleep` takes 0 to {d} ms, not {d}", .{ sleep_most_ms, ms });
        try run.charge(pos, 1);
        run.io.sleep(.fromMilliseconds(ms), .awake) catch return run.fail(pos, .host, "`sleep` was cancelled after its start", .{});
        h.time.sleep_ms += @intCast(ms);
        return .nil;
    }
    if (std.mem.startsWith(u8, name, "job_")) return @import("shale_host_jobs.zig").call(h, run, pos, name, args);
    if (std.mem.eql(u8, name, "procs") or std.mem.eql(u8, name, "proc_stop")) return @import("shale_host_procs.zig").call(h, run, pos, name, args);
    if (std.mem.eql(u8, name, "zig_rename")) return @import("shale_host_zig.zig").rename(h.alloc, run, pos, args);
    if (std.mem.eql(u8, name, "zig_decls")) return @import("shale_host_zig.zig").decls(h.alloc, run, pos, args);
    if (std.mem.eql(u8, name, "zig_move")) return @import("shale_host_zig.zig").move(h.alloc, run, pos, args);
    if (std.mem.eql(u8, name, "zig_imports")) return @import("shale_host_zig.zig").imports(h.alloc, run, pos, args);
    if (std.mem.eql(u8, name, "zig_tokens")) return @import("shale_host_zig.zig").tokens(h.alloc, run, pos, args);
    // `edits_all`'s write: every file planned and admitted before any is
    // written (`builtins_change.editsWrite`).
    if (std.mem.eql(u8, name, "edits_write")) {
        if (args.len != 1) return run.fail(pos, .argument, "`edits_write` takes one list, given {d}", .{args.len});
        return script.builtins_change.editsWrite(run, pos, args[0]);
    }
    // `edits_all` asks this of every file before it changes any.
    if (std.mem.eql(u8, name, "may_change")) {
        if (args.len != 1) return run.fail(pos, .argument, "`may_change` takes one path, given {d}", .{args.len});
        try script.builtins_change.mayChange(run, pos, try script.builtins_store.pathArg(run, pos, .host, args[0], false));
        return .{ .boolean = true };
    }
    return run.fail(pos, .host, "`{s}`: not on this host", .{name});
}

/// Programs `run` refuses without a `want`, by argv[0]'s basename with a version
/// suffix dropped (`python3.12` is `python3`) or its trailing digits dropped too
/// (`bash5.2` is `bash`, `perl5` is `perl`).
const refused = [_]struct { []const []const u8, []const u8 }{
    .{ &.{ "bash", "sh", "dash", "zsh", "busybox", "python", "python3", "perl", "node" }, "a shell or an interpreter: Shale is the script" },
    .{ &.{ "head", "tail", "grep", "sed", "awk", "gawk", "cut", "sort", "uniq", "wc", "cat", "tr" }, "a text tool: Shale's builtins and run's own filter and keep do this" },
    .{ &.{ "env", "xargs", "timeout", "nice", "nohup", "setsid", "stdbuf", "time" }, "a wrapper that runs another program: run that program, with run's own options" },
};

/// Refuses, unless given a `want`, a program in `refused` and a program by path
/// that git does not track, unless it is under a `tools` mount. Used by `run`
/// and `job_start`.
pub fn refuse(h: *Host, run: *Run, pos: ast.Pos, who: []const u8, argv0: []const u8, cwd: []const u8, want: ?[]const u8) Error!void {
    if (want != null) return;
    const base = baseName(argv0);
    const bare = std.mem.trimEnd(u8, base, "0123456789");
    for (refused) |group| for (group[0]) |n| if (std.mem.eql(u8, n, base) or (bare.len > 0 and std.mem.eql(u8, n, bare)))
        return run.fail(pos, .refused, "`{s}` of `{s}`: {s}; {{want = \"tag: why\"}} to run it anyway", .{ who, base, group[1] });
    if (std.mem.indexOfScalar(u8, argv0, '/') != null and !underToolsMount(h, argv0) and !try tracked(h, run, pos, argv0, cwd))
        return run.fail(pos, .refused, "`{s}` of `{s}`: a program by path that git does not track (a script just written?); {{want = \"tag: why\"}} to run it anyway", .{ who, argv0 });
}

/// Whether `argv0`, an absolute path, is inside a mount marked ` tools`.
fn underToolsMount(h: *Host, argv0: []const u8) bool {
    for (h.mounts) |m| {
        if (!m.tools) continue;
        if (argv0.len > m.dir.len and setup.inFolder(argv0, m.dir)) return true;
    }
    return false;
}

fn baseName(argv0: []const u8) []const u8 {
    var b = std.fs.path.basename(argv0);
    while (std.mem.lastIndexOfScalar(u8, b, '.')) |dot| {
        const tail = b[dot + 1 ..];
        if (tail.len == 0) break;
        for (tail) |c| if (!std.ascii.isDigit(c)) return b;
        b = b[0..dot];
    }
    return b;
}

pub fn intOpt(run: *Run, pos: ast.Pos, k: []const u8, x: Value) Error!u64 {
    return switch (x) {
        .int => |n| if (n < 0) run.fail(pos, .argument, "`run`: its `{s}` must be 0 or more, not {d}", .{ k, n }) else @intCast(n),
        else => run.fail(pos, .type, "`run`: its `{s}` must be an integer, not {s}", .{ k, x.typeName() }),
    };
}

pub fn strOpt(run: *Run, pos: ast.Pos, k: []const u8, x: Value) Error![]const u8 {
    return switch (x) {
        .string => |s| s,
        else => run.fail(pos, .type, "`run`: its `{s}` must be a string, not {s}", .{ k, x.typeName() }),
    };
}

pub fn mapOpt(run: *Run, pos: ast.Pos, k: []const u8, x: Value) Error!*script.value.Table {
    return switch (x) {
        .table => |t| if (t.kind == .list) run.fail(pos, .type, "`run`: its `{s}` must be a map, not a list", .{k}) else t,
        else => run.fail(pos, .type, "`run`: its `{s}` must be a map, not {s}", .{ k, x.typeName() }),
    };
}

/// Replaces each secret's bytes with `<secret K>` in a program's output, before
/// the filter, the keep and the log. A possible secret split across reads is
/// held back until it can be decided.
pub const Scrub = struct {
    alloc: std.mem.Allocator,
    secrets: []const [2][]const u8 = &.{},
    carry: std.ArrayList(u8) = .empty,
    next: *Out,

    pub fn write(s: *Scrub, bytes: []const u8) Sink.Error!void {
        if (s.secrets.len == 0) return s.next.write(bytes);
        try s.carry.appendSlice(s.alloc, bytes);
        try s.scan(false);
    }

    /// Scrubs the carry and passes it on, keeping back a possible secret's start
    /// unless `last`. Where the rest is a prefix of a longer secret we wait even
    /// if a shorter one is whole (`abc` may go on to `abcdef`); then the longest
    /// whole match wins, so a secret that begins another never leaks its rest.
    fn scan(s: *Scrub, last: bool) Sink.Error!void {
        const buf = s.carry.items;
        var out: std.ArrayList(u8) = .empty;
        defer out.deinit(s.alloc);
        var i: usize = 0;
        scan: while (i < buf.len) {
            const rest = buf[i..];
            if (!last) for (s.secrets) |kv| if (rest.len < kv[1].len and std.mem.startsWith(u8, kv[1], rest)) break :scan;
            var best: ?[2][]const u8 = null;
            for (s.secrets) |kv| if (std.mem.startsWith(u8, rest, kv[1]) and (best == null or kv[1].len > best.?[1].len)) {
                best = kv;
            };
            if (best) |kv| {
                // A secret starting inside this one and running past its end
                // (`abc`, `bcdef` in `abcdef`) widens the match, so none of it is
                // left raw; one that may continue in the next read is waited for.
                var end = i + kv[1].len;
                var j = i + 1;
                while (j < end) : (j += 1) for (s.secrets) |o| {
                    const at = buf[j..];
                    if (std.mem.startsWith(u8, at, o[1])) {
                        end = @max(end, j + o[1].len);
                    } else if (!last and at.len < o[1].len and std.mem.startsWith(u8, o[1], at)) break :scan;
                };
                try out.print(s.alloc, "<secret {s}>", .{kv[0]});
                i = end;
                continue :scan;
            }
            try out.append(s.alloc, buf[i]);
            i += 1;
        }
        try s.next.write(out.items);
        std.mem.copyForwards(u8, s.carry.items, buf[i..]);
        s.carry.shrinkRetainingCapacity(buf.len - i);
    }

    /// Scrubs what is held, with nothing more to come.
    pub fn finish(s: *Scrub) Sink.Error!void {
        if (s.secrets.len == 0) return;
        try s.scan(true);
    }
};

/// Where scrubbed output goes: the whole log, and the keep or `whole`.
pub const Out = struct {
    alloc: std.mem.Allocator,
    log: ?*std.Io.File.Writer = null,
    log_failed: bool = false,
    keeper: ?*keep.Keep = null,
    whole: ?*std.ArrayList(u8) = null,
    whole_most: usize = 0,
    whole_over: bool = false,
    bytes: u64 = 0,

    fn write(o: *Out, bytes: []const u8) Sink.Error!void {
        o.bytes += bytes.len;
        if (o.log) |w| if (!o.log_failed) w.interface.writeAll(bytes) catch {
            o.log_failed = true;
        };
        if (o.keeper) |k| try k.feed(bytes);
        if (o.whole) |w| {
            if (w.items.len + bytes.len > o.whole_most) o.whole_over = true else try w.appendSlice(o.alloc, bytes);
        }
    }
};

/// An allocator for a run's stream that frees for real and allows at most `most`
/// bytes live at once (what is left of the run's arena budget). A tail of 3 over
/// a 10 GB log holds three lines; a keep that needs more stops the run (`streamOver`).
pub const Capped = struct {
    backing: std.mem.Allocator,
    most: usize,
    live: usize = 0,
    /// Set when the cap, not the backing allocator, refused an allocation.
    hit: bool = false,

    pub fn allocator(c: *Capped) std.mem.Allocator {
        return .{ .ptr = c, .vtable = &vtable };
    }

    const vtable: std.mem.Allocator.VTable = .{ .alloc = alloc, .resize = resize, .remap = remap, .free = free };

    fn fits(c: *Capped, more: usize) bool {
        if (more <= c.most -| c.live) return true;
        c.hit = true;
        return false;
    }

    fn alloc(ctx: *anyopaque, len: usize, al: std.mem.Alignment, ra: usize) ?[*]u8 {
        const c: *Capped = @ptrCast(@alignCast(ctx));
        if (!c.fits(len)) return null;
        const p = c.backing.rawAlloc(len, al, ra) orelse return null;
        c.live += len;
        return p;
    }

    fn resize(ctx: *anyopaque, mem: []u8, al: std.mem.Alignment, new_len: usize, ra: usize) bool {
        const c: *Capped = @ptrCast(@alignCast(ctx));
        if (new_len > mem.len and !c.fits(new_len - mem.len)) return false;
        if (!c.backing.rawResize(mem, al, new_len, ra)) return false;
        c.live = c.live - mem.len + new_len;
        return true;
    }

    fn remap(ctx: *anyopaque, mem: []u8, al: std.mem.Alignment, new_len: usize, ra: usize) ?[*]u8 {
        const c: *Capped = @ptrCast(@alignCast(ctx));
        if (new_len > mem.len and !c.fits(new_len - mem.len)) return null;
        const p = c.backing.rawRemap(mem, al, new_len, ra) orelse return null;
        c.live = c.live - mem.len + new_len;
        return p;
    }

    fn free(ctx: *anyopaque, mem: []u8, al: std.mem.Alignment, ra: usize) void {
        const c: *Capped = @ptrCast(@alignCast(ctx));
        c.backing.rawFree(mem, al, ra);
        c.live -= mem.len;
    }
};

/// Whether git tracks the program at `path`, relative to `cwd`.
fn tracked(h: *Host, run: *Run, pos: ast.Pos, path: []const u8, cwd: []const u8) Error!bool {
    var nothing: Out = .{ .alloc = run.alloc };
    var scrub: Scrub = .{ .alloc = run.alloc, .next = &nothing };
    var sink: Sink = .{ .host = h, .scrub = &scrub };
    const o = h.spawn_fn(h.spawn_ctx, run.alloc, .{ .argv = &.{ "git", "ls-files", "--error-unmatch", "--", path }, .cwd = cwd, .env = h.env, .timeout_ms = 10_000 }, &sink) catch |e| switch (e) {
        error.OutOfMemory => return run.oom(pos),
        else => return false,
    };
    return o.ended == .exit and o.code == 0;
}

// The tests run `shale-fixture`, which the build puts on PATH.

const testing = std.testing;

const T = @import("shale_test_host.zig").T;

test "host: run keeps the head and tail of what a program prints, and says how it ended and what it counted" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    try t.expectOutput(
        \\local r = host("run", {"shale-fixture", "lines", "30"}, {timeout_ms = 10000, head = 2, tail = 2})
        \\print(r.ended, r.code, r.total_lines, r.matched_lines, r.kept_lines)
        \\print(r.out)
    , "exit\t0\t30\t30\t4\nline 1\nline 2\n… 26 lines left out …\nline 29\nline 30\n\n");
    // The filter: re, not_re, nocase; an exit code that is not 0.
    try t.expectOutput(
        \\local r = host("run", {"shale-fixture", "lines", "12", "Step"}, {timeout_ms = 10000, re = "step 1", not_re = "11", nocase = true, tail = 5})
        \\print(r.matched_lines, r.out)
        \\print(host("run", {"shale-fixture", "exit", "3"}, {timeout_ms = 10000, tail = 1}).code)
    , "3\tStep 1\nStep 10\nStep 12\n\n3\n");
}

test "host: what run keeps costs the run's arena the same however its output arrives (the gate holds the engines' arenas equal)" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    // The same 200,000 bytes, 1,000 or 9,999 at a time: the same argv's
    // length, the same output. Once each, outside the gate: its arenas
    // are what is compared.
    for ([_][]const u8{ "whole = 300000", "tail = 3", "head = 2, re = \"x\"" }) |keep_with| {
        var arena: [2]usize = undefined;
        for ([_][]const u8{ "1000", "9999" }, 0..) |size, k| {
            const src = try std.fmt.allocPrint(t.arena.allocator(), "local r = host(\"run\", {{\"shale-fixture\", \"chunks\", \"200000\", \"{s}\"}}, {{timeout_ms = 20000, {s}}}) print(r.total_lines)", .{ size, keep_with });
            var r = t.runOnce(src, .atomic);
            defer r.deinit();
            try testing.expect(r.stop == null);
            try testing.expectEqualStrings("2000\n", r.output);
            arena[k] = r.arena_bytes;
        }
        if (arena[0] != arena[1]) {
            std.debug.print("{s}: {d} arena bytes in pieces of 1,000, {d} in pieces of 9,999\n", .{ keep_with, arena[0], arena[1] });
            return error.TestUnexpectedResult;
        }
    }
}

test "host: what run holds of a stream is its live bytes, capped by the arena budget: lines it drops are given back, and a keep that needs more stops" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    // 20,000 lines of about 1 KB: 20 MB through the keep, more than the
    // 16 MiB arena budget. With 100 lines of context held, it finishes: the
    // lines dropped were given back.
    const long = &@as([1000]u8, @splat('x'));
    var r = t.runOnce("local r = host(\"run\", {\"shale-fixture\", \"lines\", \"20000\", \"" ++ long ++ "\"}, {timeout_ms = 60000, re = \"^nothing\", before = 100, tail = 1, width = 2000}) print(r.total_lines, r.kept_lines)", .atomic);
    defer r.deinit();
    if (r.stop) |s| std.debug.print("stopped: {s}\n", .{s.message});
    try testing.expectEqualStrings("20000\t0\n", r.output);
    // With 18,000 held (about 18 MB live), the budget stops it, named.
    var big = t.runOnce("host(\"run\", {\"shale-fixture\", \"lines\", \"20000\", \"" ++ long ++ "\"}, {timeout_ms = 60000, re = \"^nothing\", before = 18000, tail = 1, width = 2000})", .atomic);
    defer big.deinit();
    const s = big.stop orelse return error.TestUnexpectedResult;
    try testing.expectEqual(script.diag.Class.budget, s.class);
    try testing.expect(std.mem.indexOf(u8, s.message, "past the arena budget") != null);
}

test "host: run's options are the script's to say; what it is given wrong stops, saying what to give" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    try t.expectStop("host(\"run\", {\"shale-fixture\"}, {tail = 1})", .atomic, .argument, "needs `timeout_ms`");
    try t.expectStop("host(\"run\", {\"shale-fixture\"}, {timeout_ms = 10})", .atomic, .argument, "needs a keep");
    try t.expectStop("host(\"run\", {\"shale-fixture\"}, {timeout_ms = 10, whole = 10, tail = 1})", .atomic, .argument, "takes no filter or keep");
    try t.expectStop("host(\"run\", {\"shale-fixture\"}, {timeout_ms = 10, tail = 1, after = 2})", .atomic, .argument, "need `re`");
    try t.expectStop("host(\"run\", {\"shale-fixture\"}, {timeout_ms = 10, tail = 1, color = true})", .atomic, .argument, "not `color`");
    try t.expectStop("host(\"run\", {}, {timeout_ms = 10, tail = 1})", .atomic, .argument, "empty argv");
    try t.expectStop("host(\"run\", {\"shale-fixture\", 3}, {timeout_ms = 10, tail = 1})", .atomic, .type, "argv[2]");
    try t.expectStop("host(\"run\", {\"no-such-program-here\"}, {timeout_ms = 1000, tail = 1})", .atomic, .host, "not found along PATH");
    try t.expectStop("host(\"run\", {\"shale-fixture\"}, {timeout_ms = 10, tail = 1, re = \"a{2}\"})", .atomic, .argument, "`run`'s `re`");
    try t.expectStop("host(\"jobs\", 1)", .atomic, .host, "`jobs`: not on this host");
    // A path option of another type is named as `run`'s.
    try t.expectStop("host(\"run\", {\"shale-fixture\"}, {timeout_ms = 10, tail = 1, cwd = 3})", .atomic, .type, "`run`: its `cwd` must be a string, not an integer");
    try t.expectStop("host(\"run\", {\"shale-fixture\"}, {timeout_ms = 10, tail = 1, log = true})", .atomic, .type, "`run`: its `log` must be a string, not a boolean");
}

test "host: what run refuses without a want, by name and by an untracked path; a want runs it" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    for ([_][]const u8{ "bash", "/usr/bin/python3.12", "grep", "xargs", "sed", "bash5.2", "perl5", "/usr/bin/python3" }) |name| {
        var src: [256]u8 = undefined;
        try t.expectStop(try std.fmt.bufPrint(&src, "host(\"run\", {{\"{s}\", \"x\"}}, {{timeout_ms = 1000, tail = 1}})", .{name}), .atomic, .refused, "want =");
    }
    // A program by path that git does not track: the fixture, by its path.
    var buf: [std.fs.max_path_bytes + 128]u8 = undefined;
    const path = t.path_env["PATH=".len..std.mem.indexOfScalar(u8, t.path_env, ':').?];
    try t.expectStop(try std.fmt.bufPrint(&buf, "host(\"run\", {{\"{s}/shale-fixture\", \"lines\", \"1\"}}, {{timeout_ms = 10000, tail = 1}})", .{path}), .atomic, .refused, "git does not track");
    try t.expectOutput(try std.fmt.bufPrint(&buf, "print(host(\"run\", {{\"{s}/shale-fixture\", \"lines\", \"1\"}}, {{timeout_ms = 10000, tail = 1, want = \"test: the fixture\"}}).out)", .{path}), "line 1\n\n");
    // The want is in what the CLI lists.
    try testing.expect(std.mem.indexOf(u8, t.host.records.items[t.host.records.items.len - 1].line, "(want: test: the fixture)") != null);
    // A program under a `tools` mount (a toolchain's install, the user's
    // word for it once) runs without a want; a plain mount over it does not.
    const a = t.arena.allocator();
    var why: []const u8 = "";
    t.host.mounts = try parseMounts(a, try std.fmt.allocPrint(a, "bin = {s}\n", .{path}), "/home/x", &why);
    try t.expectStop(try std.fmt.bufPrint(&buf, "host(\"run\", {{\"{s}/shale-fixture\", \"lines\", \"1\"}}, {{timeout_ms = 10000, tail = 1}})", .{path}), .atomic, .refused, "git does not track");
    t.host.mounts = try parseMounts(a, try std.fmt.allocPrint(a, "bin = {s} ro tools\n", .{path}), "/home/x", &why);
    try t.expectOutput(try std.fmt.bufPrint(&buf, "print(host(\"run\", {{\"{s}/shale-fixture\", \"lines\", \"1\"}}, {{timeout_ms = 10000, tail = 1}}).out)", .{path}), "line 1\n\n");
    // A name on the lists is refused under a tools mount all the same.
    try t.expectStop(try std.fmt.bufPrint(&buf, "host(\"run\", {{\"{s}/bash\", \"x\"}}, {{timeout_ms = 1000, tail = 1}})", .{path}), .atomic, .refused, "want =");
}

test "host: a timeout is a result" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    try t.expectOutput(
        \\local r = host("run", {"shale-fixture", "sleep", "20000"}, {timeout_ms = 200, tail = 1})
        \\print(r.ended, has_key(r, "code"), r.ms < 5000)
    , "timeout\tfalse\ttrue\n");
}

test "host: env adds to what a program inherits; a secret reaches it and never comes back, in out or in the log" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    try t.put("key.txt", "s3cret-value\n");
    try t.expectOutput(
        \\local r = host("run", {"shale-fixture", "env", "MODE"}, {timeout_ms = 10000, tail = 1, env = {MODE = "pci"}})
        \\print(r.out)
        \\local s = host("run", {"shale-fixture", "env", "API_KEY"}, {timeout_ms = 10000, tail = 1, secrets = {API_KEY = "key.txt"}, log = "run.log"})
        \\print(s.out)
    , "pci\n\n<secret API_KEY>\n\n");
    try testing.expectEqualStrings("<secret API_KEY>\n", try t.file("run.log"));
    for (t.host.records.items) |r| try testing.expect(std.mem.indexOf(u8, r.line, "s3cret") == null);
    try testing.expect(std.mem.indexOf(u8, t.host.records.items[t.host.records.items.len - 2].line, "API_KEY=<secret key.txt>") != null);
    // A secret split across the pipe's reads is still scrubbed.
    var scrub_out: Out = .{ .alloc = testing.allocator, .whole = undefined, .whole_most = 1 << 20 };
    var got: std.ArrayList(u8) = .empty;
    defer got.deinit(testing.allocator);
    scrub_out.whole = &got;
    var scrub: Scrub = .{ .alloc = testing.allocator, .secrets = &.{.{ "K", "abcdef" }}, .next = &scrub_out };
    defer scrub.carry.deinit(testing.allocator);
    for ([_][]const u8{ "x ab", "cd", "ef y abc", "Z abcde" }) |piece| try scrub.write(piece);
    try scrub.finish();
    try testing.expectEqualStrings("x <secret K> y abcZ abcde", got.items);
}

test "host: the scrubber: a secret that begins another is never told for it, whole or split, nor left raw at the end" {
    const Case = struct { pieces: []const []const u8, want: []const u8 };
    for ([_]Case{
        .{ .pieces = &.{"x abcdef y"}, .want = "x <secret LONG> y" },
        .{ .pieces = &.{ "x abc", "def y" }, .want = "x <secret LONG> y" },
        .{ .pieces = &.{ "x abc", "dX y" }, .want = "x <secret SHORT>dX y" },
        .{ .pieces = &.{"x abc"}, .want = "x <secret SHORT>" },
        .{ .pieces = &.{ "x ab", "c" }, .want = "x <secret SHORT>" },
    }) |c| {
        var out: Out = .{ .alloc = testing.allocator, .whole = undefined, .whole_most = 1 << 20 };
        var got: std.ArrayList(u8) = .empty;
        defer got.deinit(testing.allocator);
        out.whole = &got;
        var scrub: Scrub = .{ .alloc = testing.allocator, .secrets = &.{ .{ "SHORT", "abc" }, .{ "LONG", "abcdef" } }, .next = &out };
        defer scrub.carry.deinit(testing.allocator);
        for (c.pieces) |piece| try scrub.write(piece);
        try scrub.finish();
        try testing.expectEqualStrings(c.want, got.items);
    }
}

test "host: the scrubber: secrets that overlap are told as one, none of either left raw, whole or split" {
    const Case = struct { pieces: []const []const u8, want: []const u8 };
    for ([_]Case{
        .{ .pieces = &.{"x abcdef y"}, .want = "x <secret A> y" },
        .{ .pieces = &.{ "x abcd", "ef y" }, .want = "x <secret A> y" },
        .{ .pieces = &.{"x abcdX y"}, .want = "x <secret A>dX y" },
        .{ .pieces = &.{"x bcdef y"}, .want = "x <secret B> y" },
    }) |c| {
        var out: Out = .{ .alloc = testing.allocator, .whole = undefined, .whole_most = 1 << 20 };
        var got: std.ArrayList(u8) = .empty;
        defer got.deinit(testing.allocator);
        out.whole = &got;
        var scrub: Scrub = .{ .alloc = testing.allocator, .secrets = &.{ .{ "A", "abc" }, .{ "B", "bcdef" } }, .next = &out };
        defer scrub.carry.deinit(testing.allocator);
        for (c.pieces) |piece| try scrub.write(piece);
        try scrub.finish();
        try testing.expectEqualStrings(c.want, got.items);
    }
}

test "host: whole keeps everything up to its bound and pipes into stdin; a kept result is refused as stdin" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    try t.expectOutput(
        \\local a = host("run", {"shale-fixture", "lines", "3"}, {timeout_ms = 10000, whole = 1000})
        \\local b = host("run", {"shale-fixture", "cat"}, {timeout_ms = 10000, whole = 1000, stdin = a})
        \\local c = host("run", {"shale-fixture", "cat"}, {timeout_ms = 10000, tail = 1, stdin = "typed\n"})
        \\print(b.out .. c.out, a.total_lines)
    , "line 1\nline 2\nline 3\ntyped\n\t3\n");
    try t.expectStop("host(\"run\", {\"shale-fixture\", \"lines\", \"300\"}, {timeout_ms = 10000, whole = 100})", .atomic, .argument, "passed `whole = 100` bytes");
    try t.expectStop("local a = host(\"run\", {\"shale-fixture\", \"lines\", \"3\"}, {timeout_ms = 10000, tail = 1}) host(\"run\", {\"shale-fixture\", \"cat\"}, {timeout_ms = 10000, tail = 1, stdin = a})", .atomic, .argument, "a kept result");
    // The log is the whole stream, whatever is kept.
    try t.expectOutput("print(host(\"run\", {\"shale-fixture\", \"lines\", \"5\"}, {timeout_ms = 10000, tail = 1, log = \"five.log\"}).out)", "… 4 lines left out …\nline 5\n\n");
    try testing.expectEqualStrings("line 1\nline 2\nline 3\nline 4\nline 5\n", try t.file("five.log"));
}

test "host: mounts: a mount's paths, real() for argv, an unknown mount or a missing folder named; the mounts file's mistakes, by line" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    try t.tmp.dir.createDirPath(t.threaded.io(), "elsewhere/logs");
    const a = t.arena.allocator();
    var why: []const u8 = "";
    const mounts = try parseMounts(a, try std.fmt.allocPrint(a, "# where things are\n\nruns = {s}/elsewhere\nhome = ~/\ngone = /no/such/folder\nmy-runs = {s}/elsewhere\n", .{ t.root, t.root }), "/home/someone", &why);
    try testing.expectEqualStrings("/home/someone", mounts[1].dir);
    // The filesystem's root is a folder too: its `/` is kept, not trimmed away.
    const top = try parseMounts(a, "top = /\nslash = /tmp/\n", "/home/someone", &why);
    try testing.expectEqualStrings("/", top[0].dir);
    try testing.expectEqualStrings("/tmp", top[1].dir);
    t.host.mounts = mounts;
    try t.expectOutput(
        \\replace("runs:logs/a.txt", "via the mount")
        \\print(read("runs:logs/a.txt"), exists("runs:"), sub(host("real", "runs:logs"), -14), sub(host("real", ""), 1, 1))
    , "via the mount\ttrue\telsewhere/logs\t/\n");
    try t.expectOutput("print(host(\"real\", \".\") == host(\"real\", \"\"), sub(host(\"real\", \".\"), -2) ~= \"/.\")", "true\ttrue\n");
    // A mount's `.` is the mount, as the root's `.` is the root.
    try t.expectOutput("print(host(\"real\", \"runs:.\") == host(\"real\", \"runs:\"), exists(\"runs:.\"), is_dir(\"runs:.\"))", "true\ttrue\ttrue\n");
    // A mount whose name has a `-` is globbed through, as any other.
    try t.expectOutput("print(join(glob(\"my-runs:**/*.txt\"), \",\"))", "my-runs:logs/a.txt\n");
    // And what a walk of it gives is spelled as the mount's own paths are.
    try t.expectOutput("local p = {} for e in walk(\"runs:.\") do push(p, e.path) end print(join(p, \",\"))", "runs:logs,runs:logs/a.txt\n");
    try testing.expectEqualStrings("via the mount", try t.file("elsewhere/logs/a.txt"));
    // A program's cwd and log through a mount.
    try t.expectOutput("print(sub(host(\"run\", {\"shale-fixture\", \"pwd\"}, {timeout_ms = 10000, tail = 1, cwd = \"runs:logs\"}).out, -15))", "elsewhere/logs\n\n");
    // `pwd`'s output differs per gate copy, so the log test uses a fixed program.
    try t.expectOutput("print(host(\"run\", {\"shale-fixture\", \"lines\", \"2\"}, {timeout_ms = 10000, tail = 1, log = \"runs:two.log\"}).kept_lines)", "1\n");
    try testing.expectEqualStrings("line 1\nline 2\n", try t.file("elsewhere/two.log"));
    try t.expectStop("read(\"nope:x\")", .atomic, .argument, "the mounts are `runs`, `home`, `gone`");
    try t.expectStop("read(\"gone:x\")", .atomic, .store, "not a folder there");
    try t.expectStop("read(\"runs:../x\")", .atomic, .argument, "not a name");
    try t.expectStop("read(\"runs:/etc/passwd\")", .atomic, .argument, "absolute inside its mount");
    for ([_]struct { []const u8, []const u8 }{
        .{ "runs = relative/path\n", "line 1: `runs`'s value `relative/path` is relative" },
        .{ "a = /x\na = /y\n", "line 2: the mount `a` is named twice" },
        .{ "# c\njust words\n", "line 2: `just words` is not `name = value`" },
        .{ "bad name = /x\n", "is not a mount's name" },
    }) |c| {
        try testing.expectError(error.Malformed, parseMounts(a, c[0], "/home/x", &why));
        try testing.expect(std.mem.indexOf(u8, why, c[1]) != null);
    }
}

test "host: the CLI's environment: inherited, ZIG_PROGRESS out" {
    var m: std.process.Environ.Map = .init(testing.allocator);
    defer m.deinit();
    try m.put("HOME", "/home/x");
    try m.put("ZIG_PROGRESS", "3");
    try m.put("PATH", "/bin");
    const env = try baseEnv(testing.allocator, &m);
    defer {
        for (env) |e| testing.allocator.free(e);
        testing.allocator.free(env);
    }
    try testing.expectEqual(@as(usize, 2), env.len);
    try testing.expectEqualStrings("HOME=/home/x", env[0]);
    try testing.expectEqualStrings("PATH=/bin", env[1]);
}

test "host: the mounts file: SHALE_MOUNTS pins it; the default's absence is no mounts, a pinned one's a mistake" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    const a = t.arena.allocator();
    const io = t.threaded.io();
    var why: []const u8 = "";
    const none = try loadMounts(a, io, null, "/no/such/home", &why);
    try testing.expectEqual(@as(usize, 0), none.mounts.len);
    try testing.expectEqualStrings("/no/such/home/.config/shale/mounts", none.from);
    try testing.expectError(error.Malformed, loadMounts(a, io, "/no/such/mounts", "/h", &why));
    try testing.expectEqualStrings("SHALE_MOUNTS=/no/such/mounts: FileNotFound", why);
    try t.tmp.dir.writeFile(io, .{ .sub_path = "mounts", .data = "runs = ~/runs\n" });
    const file = try std.fmt.allocPrint(a, "{s}/mounts", .{t.root});
    const got = try loadMounts(a, io, file, "/h", &why);
    try testing.expectEqualStrings("/h/runs", got.mounts[0].dir);
    try testing.expectEqualStrings(file, got.from);
    try t.tmp.dir.writeFile(io, .{ .sub_path = "mounts", .data = "runs = runs\n" });
    try testing.expectError(error.Malformed, loadMounts(a, io, file, "/h", &why));
    try testing.expect(std.mem.startsWith(u8, why, file));
    try testing.expect(std.mem.endsWith(u8, why, ": line 1: `runs`'s value `runs` is relative: an absolute path, or one from `~/`"));
}

/// A spawn that runs nothing: the bytes it is told to give, every time.
const Fake = struct {
    out: []const u8,
    calls: usize = 0,
    fn spawnFn(ctx: ?*anyopaque, _: std.mem.Allocator, _: spawn.Spec, sink: *Sink) (spawn.Error || Sink.Error)!spawn.Outcome {
        const f: *Fake = @ptrCast(@alignCast(ctx.?));
        f.calls += 1;
        try sink.write(f.out);
        return .{ .ended = .exit, .code = 0, .ms = 1 };
    }
};

test "host: the gate's fake spawn: the same bytes into both engines, the keep and the charges the real code's; the cap on a run's programs" {
    var t: T = undefined;
    try t.init();
    defer t.deinit();
    var fake: Fake = .{ .out = "alpha\nbeta\ngamma\n" };
    t.host.spawn_fn = Fake.spawnFn;
    t.host.spawn_ctx = &fake;
    try t.expectOutput("print(host(\"run\", {\"anything\"}, {timeout_ms = 10, re = \"a$\", head = 1, tail = 1}).out)", "alpha\n… 1 line left out …\ngamma\n\n");
    // A want lets a refused name through to the spawn.
    try t.expectOutput("print(host(\"run\", {\"bash\", \"-c\", \"x\"}, {timeout_ms = 10, tail = 1, want = \"test: a fake\"}).out)", "… 2 lines left out …\ngamma\n\n");
    // 64 programs a run, the 65th a stop, counted per run (per engine).
    try t.expectStop("for i = 1, 65 do host(\"run\", {\"x\"}, {timeout_ms = 10, tail = 1}) end", .atomic, .budget, "64 programs a run");
    try t.expectOutput("for i = 1, 64 do host(\"run\", {\"x\"}, {timeout_ms = 10, tail = 1}) end print(\"ok\")", "ok\n");
}
