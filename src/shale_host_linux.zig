//! **SHALE'S LINUX HOST** (metal-vmm QUEUE 233, chunk 1; the dogfood
//! design's "The architecture", "Mounts", "Running a program"): what the
//! CLI hands the core through its seam. `host(name, ...)`
//! comes here (`call`): `run` a program under its options, `real` a Shale
//! path's OS path. Paths come here first (`resolve`): a path whose first
//! part has a `:` is a mount's (`runs:x/test.log`).
//!
//! Metal has none of this: the core it shares is unchanged, and with no
//! host its `host` stops and its paths are the root's.

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

/// The most programs a run may run (the design's "The CLI's defaults").
pub const runs_most = 64;

// ── the spawn, real or a test's ─────────────────────────────────────────

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

/// **THE ONE THING A GATED TEST FAKES**: the spawn, bytes in and bytes
/// out. Everything around it (the options, the filter, the keep, the
/// charges) is the real code in both engines.
pub const Spawn = *const fn (ctx: ?*anyopaque, alloc: std.mem.Allocator, spec: spawn.Spec, sink: *Sink) (spawn.Error || Sink.Error)!spawn.Outcome;

fn realSpawn(_: ?*anyopaque, alloc: std.mem.Allocator, spec: spawn.Spec, sink: *Sink) (spawn.Error || Sink.Error)!spawn.Outcome {
    return spawn.run(alloc, spec, sink);
}

/// What the host did that the CLI lists after the run: each program it
/// ran, with its want, and each log it wrote.
pub const Record = struct { line: []const u8 };

pub const Host = struct {
    alloc: std.mem.Allocator,
    /// The root as the CLI was given it, an absolute OS path. A mount
    /// inside it is taken against the run's own root, so a gated test's
    /// engines, each in a copy of the root, each use their own (the design's
    /// "The gate").
    root_abs: []const u8,
    /// The process's working folder: what a relative root is against.
    cwd_abs: []const u8,
    mounts: []const Mount = &.{},
    /// Where the mounts came from, for a stop that names them.
    mounts_from: []const u8 = "~/.config/shale/mounts",
    /// The environment a program inherits, `K=V` each (`ZIG_PROGRESS` out).
    env: []const []const u8 = &.{},
    /// The program a job runs under (`shale` itself, `--reap`): null, no
    /// jobs.
    reaper: ?[]const u8 = null,
    spawn_fn: Spawn = realSpawn,
    spawn_ctx: ?*anyopaque = null,
    records: std.ArrayList(Record) = .empty,

    pub fn deinit(h: *Host) void {
        for (h.records.items) |r| h.alloc.free(r.line);
        h.records.deinit(h.alloc);
    }

    pub fn options(h: *Host) struct { host: interp.Host, path_hook: interp.PathHook } {
        return .{ .host = .{ .ctx = h, .call = call }, .path_hook = .{ .ctx = h, .resolve = resolve, .may_change = mayChange } };
    }

    pub fn record(h: *Host, comptime fmt: []const u8, args: anytype) void {
        const line = std.fmt.allocPrint(h.alloc, fmt, args) catch return;
        h.records.append(h.alloc, .{ .line = line }) catch h.alloc.free(line);
    }
};

// ── paths ────────────────────────────────────────────────────────────────

/// **A MOUNT'S PATH** (`runs:x/y`): the mount's folder and the rest, with
/// the root's rules on the rest (no `..`, no absolute path, FAT's names).
/// Any other path is the root's (null).
fn resolve(ctx: *anyopaque, run: *Run, pos: ast.Pos, path: []const u8) Error!?[]const u8 {
    const h: *Host = @ptrCast(@alignCast(ctx));
    const first_end = std.mem.indexOfScalar(u8, path, '/') orelse path.len;
    const colon = std.mem.indexOfScalar(u8, path[0..first_end], ':') orelse {
        // The root's path: the root's rules are the core's; a link in it is
        // refused here, when its parts are names (else the core says why).
        var it = std.mem.splitScalar(u8, path, '/');
        while (it.next()) |part| if (!store.fatName(part)) return null;
        try refuseLinks(run, pos, std.mem.trimEnd(u8, run.root, "/"), path, "");
        return null;
    };
    const name = path[0..colon];
    const rest = path[colon + 1 ..];
    const m = for (h.mounts) |m| {
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
    if (rest.len > 0 and rest[0] == '/') return run.fail(pos, .argument, "\"{s}\" is absolute inside its mount: a mount's paths are relative to it", .{path});
    if (rest.len > 0) {
        var it = std.mem.splitScalar(u8, rest, '/');
        while (it.next()) |part| if (!store.fatName(part)) return run.fail(pos, .argument, "\"{s}\" has a part (\"{s}\") that is not a name: no `.`, `..`, empty parts or characters FAT refuses", .{ path, part });
    }
    // A mount whose folder is not there, or cannot be opened, stops when
    // it is used, saying which.
    if (std.Io.Dir.cwd().openDir(run.io, m.dir, .{})) |d| d.close(run.io) else |e| switch (e) {
        error.FileNotFound, error.NotDir => return run.fail(pos, .store, "the mount `{s}` is {s}, which is not a folder there", .{ m.name, m.dir }),
        else => return run.fail(pos, .store, "the mount `{s}` is {s}, which cannot be opened: {s}", .{ m.name, m.dir, @errorName(e) }),
    }
    const inside = std.mem.startsWith(u8, m.dir, h.root_abs) and (m.dir.len == h.root_abs.len or m.dir[h.root_abs.len] == '/');
    const dir = if (inside) std.fmt.allocPrint(run.alloc, "{s}{s}", .{ try rootOf(h, run, pos), m.dir[h.root_abs.len..] }) catch return run.oom(pos) else m.dir;
    try refuseLinks(run, pos, dir, rest, path[0 .. colon + 1]);
    if (rest.len == 0) return dir;
    return std.fmt.allocPrint(run.alloc, "{s}/{s}", .{ dir, rest }) catch run.oom(pos);
}

/// **THE STORE FOLLOWS NO LINK** (metal-vmm QUEUE 235; Steve: "refuse
/// any"): each part of `rest` below `base`, looked at without following
/// it (`lstat`, a component walk); a symbolic link stops the run, for a
/// read, a change, `exists`, `stat` and a listing alike. A part not there
/// ends the walk: what is below it is not there either. A step a part.
/// (`walk` passes the links it lists by: what a listing names is not a
/// path given.)
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

/// **A CHANGE THROUGH A READ-ONLY MOUNT IS REFUSED** (metal-vmm QUEUE 235),
/// by the mount's name, or by its folder when a root path reaches into it.
fn mayChange(ctx: *anyopaque, run: *Run, pos: ast.Pos, rel: []const u8, full: []const u8) Error!void {
    const h: *Host = @ptrCast(@alignCast(ctx));
    const abs = try absolute(h, run, pos, std.mem.trimEnd(u8, full, "/"));
    for (h.mounts) |m| {
        if (m.access != .read_only) continue;
        const named = std.mem.startsWith(u8, rel, m.name) and rel.len > m.name.len and rel[m.name.len] == ':';
        const inside = std.mem.startsWith(u8, m.dir, h.root_abs) and (m.dir.len == h.root_abs.len or m.dir[h.root_abs.len] == '/');
        const dir = if (inside) std.fmt.allocPrint(run.alloc, "{s}{s}", .{ try rootOf(h, run, pos), m.dir[h.root_abs.len..] }) catch return run.oom(pos) else m.dir;
        const under = std.mem.startsWith(u8, abs, dir) and (abs.len == dir.len or abs[dir.len] == '/');
        if (named or under) return run.fail(pos, .refused, "{s}: mount `{s}` is read-only", .{ rel, m.name });
    }
}

/// The run's own root, absolute.
pub fn rootOf(h: *Host, run: *Run, pos: ast.Pos) Error![]const u8 {
    return absolute(h, run, pos, std.mem.trimEnd(u8, run.root, "/"));
}

fn absolute(h: *Host, run: *Run, pos: ast.Pos, p: []const u8) Error![]const u8 {
    if (std.fs.path.isAbsolute(p)) return p;
    return std.fmt.allocPrint(run.alloc, "{s}/{s}", .{ h.cwd_abs, p }) catch run.oom(pos);
}

/// A Shale path's OS path, through the mounts or the root, made absolute.
pub fn osPath(h: *Host, run: *Run, pos: ast.Pos, v: Value, allow_root: bool) Error![]const u8 {
    const p = try script.builtins_store.pathArg(run, pos, .host, v, allow_root);
    return absolute(h, run, pos, std.mem.trimEnd(u8, p.full, "/"));
}

// ── the host's calls ─────────────────────────────────────────────────────

fn call(ctx: *anyopaque, run: *Run, pos: ast.Pos, name: []const u8, args: []const Value) Error!Value {
    const h: *Host = @ptrCast(@alignCast(ctx));
    if (std.mem.eql(u8, name, "real")) {
        if (args.len != 1) return run.fail(pos, .argument, "`real` takes one path, given {d}", .{args.len});
        return .{ .string = try osPath(h, run, pos, args[0], true) };
    }
    if (std.mem.eql(u8, name, "run")) return runProgram(h, run, pos, args);
    if (std.mem.startsWith(u8, name, "job_")) return @import("shale_host_jobs.zig").call(h, run, pos, name, args);
    if (std.mem.eql(u8, name, "zig_rename")) return @import("shale_host_zig.zig").rename(h.alloc, run, pos, args);
    if (std.mem.eql(u8, name, "zig_decls")) return @import("shale_host_zig.zig").decls(h.alloc, run, pos, args);
    if (std.mem.eql(u8, name, "zig_move")) return @import("shale_host_zig.zig").move(h.alloc, run, pos, args);
    if (std.mem.eql(u8, name, "zig_imports")) return @import("shale_host_zig.zig").imports(h.alloc, run, pos, args);
    if (std.mem.eql(u8, name, "zig_tokens")) return @import("shale_host_zig.zig").tokens(h.alloc, run, pos, args);
    // Whether a path may change: what `edits_all` asks of every file first.
    if (std.mem.eql(u8, name, "may_change")) {
        if (args.len != 1) return run.fail(pos, .argument, "`may_change` takes one path, given {d}", .{args.len});
        try script.builtins_change.mayChange(run, pos, try script.builtins_store.pathArg(run, pos, .host, args[0], false));
        return .{ .boolean = true };
    }
    return run.fail(pos, .host, "`{s}`: not on this host", .{name});
}

/// What `run` refuses unless it is given a `want`, by argv[0]'s basename
/// with a version suffix dropped (`python3.12` is `python3`), or with its
/// trailing version digits dropped too (`bash5.2` is `bash`, `perl5` is
/// `perl`).
const refused = [_]struct { []const []const u8, []const u8 }{
    .{ &.{ "bash", "sh", "dash", "zsh", "busybox", "python", "python3", "perl", "node" }, "a shell or an interpreter: Shale is the script" },
    .{ &.{ "head", "tail", "grep", "sed", "awk", "gawk", "cut", "sort", "uniq", "wc", "cat", "tr" }, "a text tool: Shale's builtins and run's own filter and keep do this" },
    .{ &.{ "env", "xargs", "timeout", "nice", "nohup", "setsid", "stdbuf", "time" }, "a wrapper that runs another program: run that program, with run's own options" },
};

/// **WHAT `run` AND `job_start` REFUSE**, without a `want`: the lists
/// above, by basename, and a program by path that git does not track.
pub fn refuse(h: *Host, run: *Run, pos: ast.Pos, who: []const u8, argv0: []const u8, cwd: []const u8, want: ?[]const u8) Error!void {
    if (want != null) return;
    const base = baseName(argv0);
    const bare = std.mem.trimEnd(u8, base, "0123456789");
    for (refused) |group| for (group[0]) |n| if (std.mem.eql(u8, n, base) or (bare.len > 0 and std.mem.eql(u8, n, bare)))
        return run.fail(pos, .refused, "`{s}` of `{s}`: {s}; {{want = \"tag: why\"}} to run it anyway", .{ who, base, group[1] });
    if (std.mem.indexOfScalar(u8, argv0, '/') != null and !try tracked(h, run, pos, argv0, cwd))
        return run.fail(pos, .refused, "`{s}` of `{s}`: a program by path that git does not track (a script just written?); {{want = \"tag: why\"}} to run it anyway", .{ who, argv0 });
}

fn baseName(argv0: []const u8) []const u8 {
    var b = std.fs.path.basename(argv0);
    // `python3.12` is `python3`: a version's `.N` parts dropped.
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

/// **THE SCRUBBER**: a secret's bytes replaced by `<secret K>` in all a
/// program prints, before the filter, the keep and the log; a secret split
/// across reads is held back until it can be told.
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

    /// The carry scrubbed and passed on, but for what may be a secret's
    /// start when more is to come (`last` false). At each place, a
    /// secret the rest is the start of is waited for, though a shorter one
    /// is whole there (`abc` and `abcdef`: `abc` at a read's end may go on
    /// to the longer); then the longest whole one is told, so a secret that
    /// begins another never tells the other's rest.
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
                // **OVERLAPS TOLD AS ONE**: a secret that starts inside this
                // one and runs past its end (`abc`, `bcdef` in `abcdef`)
                // widens what is told, so none of it is left raw; one that
                // may still go on in the next read is waited for.
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

    /// The output ended: what was held, scrubbed with nothing more to come.
    pub fn finish(s: *Scrub) Sink.Error!void {
        if (s.secrets.len == 0) return;
        try s.scan(true);
    }
};

/// The scrubbed output's two ends: the log, whole, and the keep or the
/// collected `whole`.
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

/// **WHAT A RUN'S STREAM HOLDS, LIVE AND CAPPED**: the host's allocator,
/// every free given back, and at most `most` bytes live at once (what is
/// left of the run's arena budget). A tail of 3 over a 10 GB log holds three lines; a keep
/// that needs more than the budget stops the run (`streamOver`).
pub const Capped = struct {
    backing: std.mem.Allocator,
    most: usize,
    live: usize = 0,
    /// Whether an allocation was refused for the cap, not by the backing.
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

/// Whether git tracks the program at `path` (against `cwd`): a program
/// by path that git does not is refused without a `want`.
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

// ── tests: the fixture the build makes, found along PATH ─────────────────

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
    const long = "x" ** 1000;
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
    const mounts = try parseMounts(a, try std.fmt.allocPrint(a, "# where things are\n\nruns = {s}/elsewhere\nhome = ~/\ngone = /no/such/folder\n", .{t.root}), "/home/someone", &why);
    try testing.expectEqualStrings("/home/someone", mounts[1].dir);
    t.host.mounts = mounts;
    try t.expectOutput(
        \\replace("runs:logs/a.txt", "via the mount")
        \\print(read("runs:logs/a.txt"), exists("runs:"), sub(host("real", "runs:logs"), -14), sub(host("real", ""), 1, 1))
    , "via the mount\ttrue\telsewhere/logs\t/\n");
    try testing.expectEqualStrings("via the mount", try t.file("elsewhere/logs/a.txt"));
    // A program's cwd and log through a mount.
    try t.expectOutput("print(sub(host(\"run\", {\"shale-fixture\", \"pwd\"}, {timeout_ms = 10000, tail = 1, cwd = \"runs:logs\"}).out, -15))", "elsewhere/logs\n\n");
    // (The pwd's log would differ per gate copy: a fixed program's, instead.)
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
