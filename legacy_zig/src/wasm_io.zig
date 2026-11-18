//! The `Io` of Shale in a browser (host_io_wasm.zig): files are an in-memory
//! tree in the wasm module's own memory. It answers the surface zig-metal-unikernel's
//! io.zig answers (the calls webbox's `store` and Shale make), so the portable
//! sources compile unchanged; src/shale_wasm.zig hands the tree to the page.
//!
//! One thread, no processes: `Group` runs inline, a `Mutex` only checks it is
//! not taken twice, and a futex has nothing to wake.

const std = @import("std");

const Self = @This();

pub fn io() Self {
    return .{};
}

const gpa = std.heap.wasm_allocator;

pub const Error = error{
    FileNotFound,
    NotDir,
    IsDir,
    OutOfMemory,
    StreamTooLong,
    ReadFailed,
    WriteFailed,
    NameTooLong,
    NoSpaceLeft,
    PathAlreadyExists,
    ReadOnlyFileSystem,
};

pub const max_path: usize = 256;

// ---- the tree --------------------------------------------------------------

pub const Node = struct {
    dir: bool,
    data: std.ArrayList(u8) = .empty,
    mtime_s: i64 = 0,
};

/// Every file and folder but the root, by its path from the root: parts joined
/// by `/`, no `.` and no empty parts. A node's parent is always a folder here.
pub var nodes: std.StringArrayHashMapUnmanaged(Node) = .empty;

/// The wall clock, in nanoseconds, as the page last set it (`setNow`).
var real_ns: i96 = 0;

pub fn setNow(unix_ms: i64) void {
    real_ns = @as(i96, unix_ms) * 1_000_000;
}

fn nowSeconds() i64 {
    return @intCast(@divFloor(real_ns, 1_000_000_000));
}

/// `sub` from `base`, as a key of `nodes` (`""` is the root), in `buf`. A `..`
/// is refused as not found: nothing above the root exists.
fn keyOf(base: []const u8, sub: []const u8, buf: *[max_path]u8) Error![]const u8 {
    var len: usize = 0;
    for ([_][]const u8{ base, sub }) |part_path| {
        var parts = std.mem.splitScalar(u8, part_path, '/');
        while (parts.next()) |p| {
            if (p.len == 0 or std.mem.eql(u8, p, ".")) continue;
            if (std.mem.eql(u8, p, "..")) return Error.FileNotFound;
            const extra = p.len + @intFromBool(len > 0);
            if (len + extra > max_path) return Error.NameTooLong;
            if (len > 0) {
                buf[len] = '/';
                len += 1;
            }
            @memcpy(buf[len..][0..p.len], p);
            len += p.len;
        }
    }
    return buf[0..len];
}

fn parentOf(key: []const u8) []const u8 {
    return if (std.mem.lastIndexOfScalar(u8, key, '/')) |i| key[0..i] else "";
}

/// The folder a new node at `key` goes in must be there: NotDir when a part of
/// it is a file, FileNotFound when a part is missing.
fn checkParent(key: []const u8) Error!void {
    const parent = parentOf(key);
    if (parent.len == 0) return;
    if (nodes.getPtr(parent)) |n| {
        if (!n.dir) return Error.NotDir;
        return;
    }
    try checkParent(parent);
    return Error.FileNotFound;
}

fn find(key: []const u8) ?*Node {
    return nodes.getPtr(key);
}

fn isRoot(key: []const u8) bool {
    return key.len == 0;
}

/// A file at `key`, made empty when it is not there.
fn fileAt(key: []const u8) Error!*Node {
    if (isRoot(key)) return Error.IsDir;
    if (find(key)) |n| {
        if (n.dir) return Error.IsDir;
        return n;
    }
    try checkParent(key);
    const owned = try gpa.dupe(u8, key);
    errdefer gpa.free(owned);
    const gop = try nodes.getOrPut(gpa, owned);
    gop.value_ptr.* = .{ .dir = false, .mtime_s = nowSeconds() };
    return gop.value_ptr;
}

/// Puts `data` at `key` as a whole file, making the folders above it: the
/// page's seeding and editing, not a script's.
pub fn putFile(path: []const u8, data: []const u8) Error!void {
    var buf: [max_path]u8 = undefined;
    const key = try keyOf("", path, &buf);
    if (parentOf(key).len > 0) try makePath(parentOf(key));
    const n = try fileAt(key);
    n.data.clearRetainingCapacity();
    try n.data.appendSlice(gpa, data);
    n.mtime_s = nowSeconds();
}

fn makePath(key: []const u8) Error!void {
    if (isRoot(key)) return;
    if (find(key)) |n| {
        if (!n.dir) return Error.NotDir;
        return;
    }
    try makePath(parentOf(key));
    const owned = try gpa.dupe(u8, key);
    errdefer gpa.free(owned);
    try nodes.put(gpa, owned, .{ .dir = true, .mtime_s = nowSeconds() });
}

fn removeKey(key: []const u8) void {
    const kv = nodes.fetchOrderedRemove(key) orelse return;
    var n = kv.value;
    n.data.deinit(gpa);
    gpa.free(kv.key);
}

/// Whether `key` is `under` or inside it.
fn within(key: []const u8, under: []const u8) bool {
    if (under.len == 0) return true;
    return std.mem.startsWith(u8, key, under) and (key.len == under.len or key[under.len] == '/');
}

pub fn removeTree(key: []const u8) void {
    var i: usize = 0;
    while (i < nodes.count()) {
        const k = nodes.keys()[i];
        if (within(k, key)) removeKey(k) else i += 1;
    }
}

// ---- std's shapes ----------------------------------------------------------

pub const Limit = enum(u64) {
    unlimited = std.math.maxInt(u64),
    _,
    pub fn limited(n: u64) Limit {
        return @fromBackingInt(@intCast(n));
    }
};

/// std's `File.Kind` whole, so a caller's `else` prong still compiles.
pub const Kind = enum {
    block_device,
    character_device,
    directory,
    named_pipe,
    sym_link,
    file,
    unix_domain_socket,
    whiteout,
    door,
    event_port,
    unknown,
};

pub const Timestamp = struct { nanoseconds: i96 };

pub const Stat = struct {
    size: u64,
    kind: Kind,
    mtime: Timestamp = .{ .nanoseconds = 0 },
};

fn statOf(key: []const u8) Error!Stat {
    if (isRoot(key)) return .{ .size = 0, .kind = .directory };
    const n = find(key) orelse return Error.FileNotFound;
    return .{
        .size = if (n.dir) 0 else n.data.items.len,
        .kind = if (n.dir) .directory else .file,
        .mtime = .{ .nanoseconds = @as(i96, n.mtime_s) * 1_000_000_000 },
    };
}

/// A file names its path, not a node: a node's bytes move as it grows, and a
/// removed node must not be read through a handle kept from before.
pub const File = struct {
    path: [max_path]u8 = undefined,
    path_len: usize = 0,

    fn at(key: []const u8) File {
        var f: File = .{ .path_len = key.len };
        @memcpy(f.path[0..key.len], key);
        return f;
    }

    fn name(self: *const File) []const u8 {
        return self.path[0..self.path_len];
    }

    pub fn close(_: File, _: Self) void {}

    pub fn stat(self: File, _: Self) Error!Stat {
        return statOf(self.name());
    }

    pub fn readPositionalAll(self: File, _: Self, buffer: []u8, offset: u64) Error!usize {
        const n = find(self.name()) orelse return Error.FileNotFound;
        if (n.dir) return Error.IsDir;
        const bytes = n.data.items;
        if (offset >= bytes.len) return 0;
        const from: usize = @intCast(offset);
        const count = @min(buffer.len, bytes.len - from);
        @memcpy(buffer[0..count], bytes[from..][0..count]);
        return count;
    }

    /// At most at the end: a write past it would leave a hole no host here
    /// fills the same way.
    pub fn writePositionalAll(self: File, _: Self, bytes: []const u8, offset: u64) Error!void {
        const n = find(self.name()) orelse return Error.FileNotFound;
        if (n.dir) return Error.IsDir;
        if (offset > n.data.items.len) return Error.WriteFailed;
        const from: usize = @intCast(offset);
        const end = from + bytes.len;
        if (end > n.data.items.len) try n.data.resize(gpa, end);
        @memcpy(n.data.items[from..end], bytes);
        n.mtime_s = nowSeconds();
    }
};

pub const Entry = struct {
    name: []const u8,
    kind: Kind,
};

/// A folder's children, named at `iterate`: a script that changes the folder
/// while walking it walks what was there when it started.
pub const Iterator = struct {
    names: std.ArrayList(Entry) = .empty,
    next_index: usize = 0,
    failed: ?Error = null,

    pub fn next(self: *Iterator, _: Self) Error!?Entry {
        if (self.failed) |e| return e;
        if (self.next_index == self.names.items.len) {
            for (self.names.items) |e| gpa.free(e.name);
            self.names.deinit(gpa);
            self.names = .empty;
            self.next_index = 0;
            return null;
        }
        self.next_index += 1;
        return self.names.items[self.next_index - 1];
    }
};

pub const OpenOptions = struct {
    iterate: bool = false,
};

pub const Mode = enum { read_only, write_only, read_write };

pub const OpenFileOptions = struct {
    mode: Mode = .read_only,
};

/// Accepted so a caller's private write compiles; the tree has no other reader.
pub const Permissions = enum(u32) {
    default_file = 0o666,
    _,
};

pub const CreateFileOptions = struct {
    truncate: bool = true,
    permissions: Permissions = .default_file,
    exclusive: bool = false,
};

pub const WriteFileOptions = struct {
    sub_path: []const u8,
    data: []const u8,
    flags: CreateFileOptions = .{},
};

pub const StatFileOptions = struct {};
pub const AccessOptions = struct {};

pub const Dir = struct {
    /// The key of this folder; `""` is the root. Points into `nodes`' keys or
    /// is a literal, so a `Dir` stays valid until its folder is removed.
    key: []const u8 = "",

    pub fn cwd() Dir {
        return .{};
    }

    pub fn close(_: Dir, _: Self) void {}

    pub fn openDir(self: Dir, _: Self, sub_path: []const u8, _: OpenOptions) Error!Dir {
        var buf: [max_path]u8 = undefined;
        const k = try keyOf(self.key, sub_path, &buf);
        if (isRoot(k)) return .{};
        const entry = nodes.getEntry(k) orelse return Error.FileNotFound;
        if (!entry.value_ptr.dir) return Error.NotDir;
        return .{ .key = entry.key_ptr.* };
    }

    pub fn openFile(self: Dir, _: Self, sub_path: []const u8, _: OpenFileOptions) Error!File {
        var buf: [max_path]u8 = undefined;
        const k = try keyOf(self.key, sub_path, &buf);
        const st = try statOf(k);
        if (st.kind == .directory) return Error.IsDir;
        return File.at(k);
    }

    pub fn statFile(self: Dir, _: Self, sub_path: []const u8, _: StatFileOptions) Error!Stat {
        var buf: [max_path]u8 = undefined;
        return statOf(try keyOf(self.key, sub_path, &buf));
    }

    pub fn access(self: Dir, ignored: Self, sub_path: []const u8, _: AccessOptions) Error!void {
        _ = try self.statFile(ignored, sub_path, .{});
    }

    pub fn readFileAlloc(self: Dir, _: Self, sub_path: []const u8, alloc: std.mem.Allocator, limit: Limit) Error![]u8 {
        var buf: [max_path]u8 = undefined;
        const k = try keyOf(self.key, sub_path, &buf);
        if (isRoot(k)) return Error.IsDir;
        const n = find(k) orelse return Error.FileNotFound;
        if (n.dir) return Error.IsDir;
        if (n.data.items.len > @backingInt(limit)) return Error.StreamTooLong;
        return alloc.dupe(u8, n.data.items) catch Error.OutOfMemory;
    }

    pub fn writeFile(self: Dir, _: Self, options: WriteFileOptions) Error!void {
        var buf: [max_path]u8 = undefined;
        const k = try keyOf(self.key, options.sub_path, &buf);
        if (options.flags.exclusive and find(k) != null) return Error.PathAlreadyExists;
        const n = try fileAt(k);
        if (options.flags.truncate) n.data.clearRetainingCapacity();
        const keep = n.data.items.len;
        if (options.data.len > keep) try n.data.resize(gpa, options.data.len);
        @memcpy(n.data.items[0..options.data.len], options.data);
        n.mtime_s = nowSeconds();
    }

    pub fn createDirPath(self: Dir, _: Self, sub_path: []const u8) Error!void {
        var buf: [max_path]u8 = undefined;
        try makePath(try keyOf(self.key, sub_path, &buf));
    }

    pub fn createFile(self: Dir, _: Self, sub_path: []const u8, opts: CreateFileOptions) Error!File {
        var buf: [max_path]u8 = undefined;
        const k = try keyOf(self.key, sub_path, &buf);
        if (opts.exclusive and find(k) != null) return Error.PathAlreadyExists;
        const n = try fileAt(k);
        if (opts.truncate) n.data.clearRetainingCapacity();
        return File.at(k);
    }

    pub fn deleteFile(self: Dir, _: Self, sub_path: []const u8) Error!void {
        var buf: [max_path]u8 = undefined;
        const k = try keyOf(self.key, sub_path, &buf);
        if (isRoot(k)) return Error.IsDir;
        const n = find(k) orelse {
            try checkParent(k);
            return Error.FileNotFound;
        };
        if (n.dir) return Error.IsDir;
        removeKey(k);
    }

    /// A file over a file, as std's: a folder at either end is refused.
    pub fn rename(self: Dir, old_sub_path: []const u8, new_dir: Dir, new_sub_path: []const u8, _: Self) Error!void {
        var old_buf: [max_path]u8 = undefined;
        var new_buf: [max_path]u8 = undefined;
        const old = try keyOf(self.key, old_sub_path, &old_buf);
        const new = try keyOf(new_dir.key, new_sub_path, &new_buf);
        const from = find(old) orelse return Error.FileNotFound;
        if (from.dir) return Error.IsDir;
        if (std.mem.eql(u8, old, new)) return;
        const to = try fileAt(new);
        // `fileAt` may have grown the map, so the source is found again.
        const src = find(old).?;
        to.data.deinit(gpa);
        to.* = src.*;
        src.* = .{ .dir = false };
        removeKey(old);
    }

    pub fn renamePreserve(self: Dir, old_sub_path: []const u8, new_dir: Dir, new_sub_path: []const u8, ignored: Self) Error!void {
        var buf: [max_path]u8 = undefined;
        if (find(try keyOf(new_dir.key, new_sub_path, &buf)) != null) return Error.PathAlreadyExists;
        return self.rename(old_sub_path, new_dir, new_sub_path, ignored);
    }

    pub fn deleteTree(self: Dir, _: Self, sub_path: []const u8) Error!void {
        var buf: [max_path]u8 = undefined;
        const k = try keyOf(self.key, sub_path, &buf);
        if (find(k)) |n| if (!n.dir) return Error.NotDir;
        removeTree(k);
    }

    /// No `io`, as std's: the caller passes it to `next`.
    pub fn iterate(self: Dir) Iterator {
        var it: Iterator = .{};
        for (nodes.keys(), nodes.values()) |k, n| {
            if (isRoot(k) or !std.mem.eql(u8, parentOf(k), self.key)) continue;
            const name = gpa.dupe(u8, k[if (self.key.len == 0) 0 else self.key.len + 1..]) catch return .{ .failed = Error.OutOfMemory };
            it.names.append(gpa, .{ .name = name, .kind = if (n.dir) .directory else .file }) catch return .{ .failed = Error.OutOfMemory };
        }
        return it;
    }
};

// ---- time, randomness, and the one thread ----------------------------------

var seed: u64 = 0x9E3779B97F4A7C15;

pub fn random(_: Self, buf: []u8) void {
    for (buf) |*b| {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        b.* = @truncate(seed);
    }
}

pub const Duration = struct {
    nanoseconds: i96,

    pub fn fromSeconds(s: i64) Duration {
        return .{ .nanoseconds = @as(i96, s) * 1_000_000_000 };
    }
};

/// Every clock is the page's wall clock: a run here is one call, and nothing
/// in it waits.
pub const Clock = enum {
    real,
    awake,
    boot,
    cpu_process,
    cpu_thread,

    pub fn now(_: Clock, _: Self) Self.Timestamp {
        return .{ .nanoseconds = real_ns };
    }

    pub const Duration = struct { raw: Self.Duration, clock: Clock };
    pub const Timestamp = struct { raw: Self.Timestamp, clock: Clock };
};

pub const Timeout = union(enum) {
    none,
    duration: Clock.Duration,
    deadline: Clock.Timestamp,
};

pub const Group = struct {
    pub const init = Group{};

    pub fn concurrent(_: *Group, _: Self, comptime f: anytype, args: anytype) error{}!void {
        @call(.auto, f, args);
    }

    pub fn async(_: *Group, _: Self, comptime f: anytype, args: anytype) error{}!void {
        @call(.auto, f, args);
    }

    pub fn wait(_: *Group, _: Self) void {}
    pub fn cancel(_: *Group, _: Self) void {}
};

pub fn futexWake(_: Self, comptime T: type, ptr: *align(@alignOf(u32)) const T, max_waiters: u32) void {
    _ = ptr;
    _ = max_waiters;
}

pub fn futexWaitTimeout(_: Self, comptime T: type, ptr: *align(@alignOf(u32)) const T, expected: T, timeout: Timeout) error{Canceled}!void {
    _ = ptr;
    _ = expected;
    _ = timeout;
}

/// Taken twice without an unlock is re-entrancy: one thread cannot wait for
/// itself, so it stops instead of hanging the page.
pub const Mutex = struct {
    held: bool = false,

    pub const init = Mutex{};

    pub fn lockUncancelable(self: *Mutex, io_: Self) void {
        self.lock(io_);
    }

    pub fn lock(self: *Mutex, _: Self) void {
        if (self.held) @panic("a mutex was locked twice without an unlock");
        self.held = true;
    }

    pub fn unlock(self: *Mutex, _: Self) void {
        self.held = false;
    }
};
