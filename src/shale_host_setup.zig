//! A Linux run's setup before its first step: the mounts, the preludes after
//! the core's, and the environment its programs start from.
//! `shale_host_linux.zig` re-exports what its callers use.

const script = shale.script;
const std = @import("std");
const interp = script.interp;
const shale = @import("shale");

/// The script-facing names over `host` (`run`, `real`) and the helpers only
/// Linux has.
pub const linux_prelude: script.parse.Prelude = .{ .name = "the Linux prelude", .source = @embedFile("shale_prelude_linux.shale") };

/// Git over `run`. Loads after the Linux prelude, whose `run` and `real` it calls.
pub const git_prelude: script.parse.Prelude = .{ .name = "the git prelude", .source = @embedFile("shale_prelude_git.shale") };

/// A named folder outside the root, reached as `name:path`.
pub const Mount = struct {
    /// What precedes the `:`; letters, digits, `_` and `-`.
    name: []const u8,
    /// The folder, absolute; links in it are resolved if it exists.
    dir: []const u8,
    /// ` ro` after the path refuses every change through the mount before
    /// anything is written; reads and programs are unaffected.
    access: Access = .read_write,
    /// ` tools` after the path marks a toolchain's install, whose programs
    /// `run` takes by path without a `want` though git does not track them.
    tools: bool = false,

    pub const Access = enum { read_write, read_only };
};

/// Parses a mounts file: one `name = value` per line, with `#` comments and
/// blank lines. A value is an absolute path or one from `~/`. On
/// `error.Malformed`, `why` names the line and what is wrong with it.
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
        // The path, then any of ` ro` and ` tools`.
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

/// Loads the mounts from `pinned` (`SHALE_MOUNTS`) or else
/// `~/.config/shale/mounts`. A missing default file means no mounts; a missing
/// pinned file, or any mistake in a file, is `error.Malformed` naming the file.
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
    // A link in a mount's own path (`~/zig` to `~/zig-0.16.0`) is the user's
    // choice, so we resolve it once here; below it, the store follows none.
    for (mounts) |*m| {
        var buf: [std.fs.max_path_bytes]u8 = undefined;
        const n = std.Io.Dir.cwd().realPathFile(io, m.dir, &buf) catch continue;
        m.dir = try alloc.dupe(u8, buf[0..n]);
    }
    return .{ .mounts = mounts, .from = from };
}

/// The preludes after the core's, in order: the Linux prelude, the git prelude,
/// the repo's `.shale/prelude.shale` if present, and the user's: the file
/// `pinned` (`SHALE_PRELUDE`) names, none if it is `""`, or else
/// `~/.config/shale/prelude.shale` if present. Each is named as a stop shows it.
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

/// A prelude's source. Returns `null` if it is missing and not `required`.
fn readPrelude(alloc: std.mem.Allocator, io: std.Io, path: []const u8, required: bool, shown: []const u8, why: *[]const u8) error{ OutOfMemory, Malformed }!?[]const u8 {
    return std.Io.Dir.cwd().readFileAlloc(io, path, alloc, .limited(interp.Budgets.default_source_bytes)) catch |e| {
        if (!required and e == error.FileNotFound) return null;
        why.* = try std.fmt.allocPrint(alloc, "{s}{s}: {s}", .{ shown, path, @errorName(e) });
        return error.Malformed;
    };
}

/// The environment a program inherits, as `K=V` strings, without
/// `ZIG_PROGRESS`: otherwise a `zig build` that `run` runs writes its progress
/// to the pipe of the build that ran the CLI, not to `run`'s output.
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
