//! What a Linux run of Shale is set up with, before its first step: the
//! mounts (`~/.config/shale/mounts`, or `SHALE_MOUNTS`), the preludes after
//! the core's (the Linux prelude, the git prelude, the repo's and the user's)
//! and the environment its programs start from. shale_host_linux.zig
//! re-exports what its callers use.

const script = shale.script;
const std = @import("std");
const interp = script.interp;
const shale = @import("shale");

/// **THE LINUX PRELUDE** (QUEUE 233, chunk 2): the names a script types
/// (`run`, `real`) over `host`, and the helpers only Linux has.
pub const linux_prelude: script.parse.Prelude = .{ .name = "the Linux prelude", .source = @embedFile("shale_prelude_linux.shale") };

/// **THE GIT PRELUDE** (QUEUE 233, chunk 3): git over `run`, after the
/// Linux prelude whose `run` and `real` it calls.
pub const git_prelude: script.parse.Prelude = .{ .name = "the git prelude", .source = @embedFile("shale_prelude_git.shale") };

// ── mounts ───────────────────────────────────────────────────────────────

pub const Mount = struct {
    name: []const u8,
    dir: []const u8,
    /// ` ro` after the path (metal-vmm QUEUE 235): every change through it
    /// is refused before anything is written; reads and programs as ever.
    access: Access = .read_write,
    /// ` tools` after the path: a toolchain's install (a newer Zig), whose
    /// programs `run` takes by path without a `want`. The user's word, given
    /// once in the mounts file, for programs git does not track.
    tools: bool = false,

    pub const Access = enum { read_write, read_only };
};

/// **THE MOUNTS FILE**: `name = value` a line, `#` comments and blank
/// lines; a value an absolute path or one from `~/`. What is wrong is said
/// with its line: a relative value, a name twice, a line that is neither.
pub fn parseMounts(alloc: std.mem.Allocator, text: []const u8, home: []const u8, why: *[]const u8) error{ OutOfMemory, Malformed }![]Mount {
    var out: std.ArrayList(Mount) = .empty;
    var it = std.mem.splitScalar(u8, text, '\n');
    var n: usize = 0;
    while (it.next()) |raw| {
        n += 1;
        const line = std.mem.trim(u8, raw, " \t\r");
        if (line.len == 0 or line[0] == '#') continue;
        const eq = std.mem.indexOfScalar(u8, line, '=') orelse {
            why.* = try std.fmt.allocPrint(alloc, "line {d}: `{s}` is not `name = value`", .{ n, line });
            return error.Malformed;
        };
        const name = std.mem.trim(u8, line[0..eq], " \t");
        // The path, and after it ` ro`, ` tools`, both or nothing.
        var words = std.mem.tokenizeAny(u8, line[eq + 1 ..], " \t");
        const value = words.next() orelse "";
        var access: Mount.Access = .read_write;
        var tools = false;
        while (words.next()) |w| {
            const seen = if (std.mem.eql(u8, w, "ro")) access == .read_only else if (std.mem.eql(u8, w, "tools")) tools else {
                why.* = try std.fmt.allocPrint(alloc, "line {d}: `{s}` after `{s}`'s path: only `ro` and `tools` may follow it (and a path has no space)", .{ n, w, name });
                return error.Malformed;
            };
            if (seen) {
                why.* = try std.fmt.allocPrint(alloc, "line {d}: `{s}` twice after `{s}`'s path", .{ n, w, name });
                return error.Malformed;
            }
            if (std.mem.eql(u8, w, "ro")) access = .read_only else tools = true;
        }
        if (name.len == 0 or !validName(name)) {
            why.* = try std.fmt.allocPrint(alloc, "line {d}: `{s}` is not a mount's name: letters, digits, `_` and `-`", .{ n, name });
            return error.Malformed;
        }
        for (out.items) |m| if (std.mem.eql(u8, m.name, name)) {
            why.* = try std.fmt.allocPrint(alloc, "line {d}: the mount `{s}` is named twice", .{ n, name });
            return error.Malformed;
        };
        const dir = if (std.mem.startsWith(u8, value, "~/"))
            try std.fmt.allocPrint(alloc, "{s}/{s}", .{ home, value[2..] })
        else if (value.len > 0 and value[0] == '/')
            try alloc.dupe(u8, value)
        else {
            why.* = try std.fmt.allocPrint(alloc, "line {d}: `{s}`'s value `{s}` is relative: an absolute path, or one from `~/`", .{ n, name, value });
            return error.Malformed;
        };
        try out.append(alloc, .{ .name = try alloc.dupe(u8, name), .dir = std.mem.trimEnd(u8, dir, "/"), .access = access, .tools = tools });
    }
    return out.toOwnedSlice(alloc);
}

/// **WHERE THE MOUNTS ARE**: `SHALE_MOUNTS` names the file (the tests pin
/// it), else `~/.config/shale/mounts`. The default's absence is no mounts;
/// a file named and missing, or any file's mistake, is said with the file.
pub fn loadMounts(alloc: std.mem.Allocator, io: std.Io, pinned: ?[]const u8, home: []const u8, why: *[]const u8) error{ OutOfMemory, Malformed }!struct { mounts: []Mount, from: []const u8 } {
    const from = if (pinned) |p| p else try std.fmt.allocPrint(alloc, "{s}/.config/shale/mounts", .{home});
    const text = std.Io.Dir.cwd().readFileAlloc(io, from, alloc, .limited(64 * 1024)) catch |e| {
        if (pinned == null and e == error.FileNotFound) return .{ .mounts = &.{}, .from = from };
        why.* = try std.fmt.allocPrint(alloc, "{s}{s}: {s}", .{ if (pinned != null) "SHALE_MOUNTS=" else "", from, @errorName(e) });
        return error.Malformed;
    };
    var line_why: []const u8 = "";
    const mounts = parseMounts(alloc, text, home, &line_why) catch |e| {
        if (e == error.Malformed) why.* = try std.fmt.allocPrint(alloc, "{s}: {s}", .{ from, line_why });
        return e;
    };
    // **A MOUNT'S OWN PATH, RESOLVED ONCE** (metal-vmm QUEUE 235): a link
    // in it (`~/zig` to `~/zig-0.16.0`) is the user's choice, taken here;
    // below it, the store follows none.
    for (mounts) |*m| {
        var buf: [std.fs.max_path_bytes]u8 = undefined;
        const n = std.Io.Dir.cwd().realPathFile(io, m.dir, &buf) catch continue;
        m.dir = try alloc.dupe(u8, buf[0..n]);
    }
    return .{ .mounts = mounts, .from = from };
}

/// **THE PRELUDES AFTER THE CORE'S, IN ORDER** (the dogfood design's "The
/// architecture"): the Linux prelude and the git prelude; the repo's, `.shale/prelude.shale` at
/// the root, if it is there; the user's, the file `SHALE_PRELUDE` names
/// (`""` none, the tests' pin) or else `~/.config/shale/prelude.shale` if it
/// is there. Each is named as a stop shows its lines.
pub fn loadPreludes(alloc: std.mem.Allocator, io: std.Io, root_abs: []const u8, pinned: ?[]const u8, home: []const u8, why: *[]const u8) error{ OutOfMemory, Malformed }![]script.parse.Prelude {
    var out: std.ArrayList(script.parse.Prelude) = .empty;
    try out.append(alloc, linux_prelude);
    try out.append(alloc, git_prelude);
    const repo = try std.fmt.allocPrint(alloc, "{s}/.shale/prelude.shale", .{root_abs});
    if (try readPrelude(alloc, io, repo, false, "", why)) |src| try out.append(alloc, .{ .name = ".shale/prelude.shale", .source = src });
    if (pinned) |p| {
        if (p.len > 0) if (try readPrelude(alloc, io, p, true, "SHALE_PRELUDE=", why)) |src| try out.append(alloc, .{ .name = p, .source = src });
    } else {
        const user = try std.fmt.allocPrint(alloc, "{s}/.config/shale/prelude.shale", .{home});
        if (try readPrelude(alloc, io, user, false, "", why)) |src| try out.append(alloc, .{ .name = "~/.config/shale/prelude.shale", .source = src });
    }
    return out.toOwnedSlice(alloc);
}

/// A prelude's source; null for one not there that need not be.
fn readPrelude(alloc: std.mem.Allocator, io: std.Io, path: []const u8, required: bool, shown: []const u8, why: *[]const u8) error{ OutOfMemory, Malformed }!?[]const u8 {
    return std.Io.Dir.cwd().readFileAlloc(io, path, alloc, .limited(interp.Budgets.default_source_bytes)) catch |e| {
        if (!required and e == error.FileNotFound) return null;
        why.* = try std.fmt.allocPrint(alloc, "{s}{s}: {s}", .{ shown, path, @errorName(e) });
        return error.Malformed;
    };
}

/// **A PROGRAM'S ENVIRONMENT**, inherited, `K=V` each, but for
/// `ZIG_PROGRESS`: a `zig build` that `run` runs writes its progress to a
/// pipe the build that ran the CLI holds, never to `run`'s output.
pub fn baseEnv(alloc: std.mem.Allocator, map: *const std.process.Environ.Map) error{OutOfMemory}![]const []const u8 {
    var out: std.ArrayList([]const u8) = .empty;
    errdefer {
        for (out.items) |e| alloc.free(e);
        out.deinit(alloc);
    }
    var it = map.iterator();
    while (it.next()) |kv| {
        if (std.mem.eql(u8, kv.key_ptr.*, "ZIG_PROGRESS")) continue;
        try out.append(alloc, try std.fmt.allocPrint(alloc, "{s}={s}", .{ kv.key_ptr.*, kv.value_ptr.* }));
    }
    return out.toOwnedSlice(alloc);
}

fn validName(s: []const u8) bool {
    for (s) |c| if (!(std.ascii.isAlphanumeric(c) or c == '_' or c == '-')) return false;
    return true;
}
