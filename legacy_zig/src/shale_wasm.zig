//! Shale in a browser: the exports a page calls. The page owns no files; it
//! seeds and edits the tree in this module's memory (src/host_io_wasm.zig)
//! and reads it back to draw it. Strings cross as a pointer and a length into
//! memory from `alloc`; an answer is left in `answer` until the next call.

const std = @import("std");
const shale = @import("shale");
const script = shale.script;
const Io = @import("host_io").Io;

const gpa = std.heap.wasm_allocator;

/// The last call's answer, read by the page through `answerPtr`/`answerLen`.
var answer: std.ArrayList(u8) = .empty;
/// A run's report, apart from its output (`run`).
var report: std.ArrayList(u8) = .empty;

var out_buf: [64 * 1024]u8 = undefined;

export fn alloc(n: usize) ?[*]u8 {
    const s = gpa.alloc(u8, n) catch return null;
    return s.ptr;
}

export fn free(p: [*]u8, n: usize) void {
    gpa.free(p[0..n]);
}

export fn answerPtr() [*]const u8 {
    return answer.items.ptr;
}

export fn answerLen() usize {
    return answer.items.len;
}

export fn reportPtr() [*]const u8 {
    return report.items.ptr;
}

export fn reportLen() usize {
    return report.items.len;
}

/// 0, or 1 when the path is not one the tree can hold (a `..`, too long, a
/// file in the way).
export fn putFile(path: [*]const u8, path_len: usize, data: [*]const u8, data_len: usize) u32 {
    Io.putFile(path[0..path_len], data[0..data_len]) catch return 1;
    return 0;
}

export fn removePath(path: [*]const u8, path_len: usize) void {
    var buf: [Io.max_path]u8 = undefined;
    var it = std.mem.splitScalar(u8, path[0..path_len], '/');
    var len: usize = 0;
    while (it.next()) |p| {
        if (p.len == 0 or std.mem.eql(u8, p, ".")) continue;
        if (len + p.len + 1 > buf.len) return;
        if (len > 0) {
            buf[len] = '/';
            len += 1;
        }
        @memcpy(buf[len..][0..p.len], p);
        len += p.len;
    }
    if (len == 0) return;
    Io.removeTree(buf[0..len]);
}

export fn clearTree() void {
    Io.removeTree("");
}

/// The tree into `answer`, a line a node in path order: `d<TAB>path` for a
/// folder, `f<TAB>path<TAB>size` for a file.
export fn listTree() void {
    answer.clearRetainingCapacity();
    const Order = struct {
        keys: []const []const u8,
        pub fn lessThan(c: @This(), a: usize, b: usize) bool {
            return std.mem.lessThan(u8, c.keys[a], c.keys[b]);
        }
    };
    Io.nodes.sort(Order{ .keys = Io.nodes.keys() });
    for (Io.nodes.keys(), Io.nodes.values()) |k, n| {
        if (n.dir) {
            answer.print(gpa, "d\t{s}\n", .{k}) catch return;
        } else {
            answer.print(gpa, "f\t{s}\t{d}\n", .{ k, n.data.items.len }) catch return;
        }
    }
}

/// A file's bytes into `answer`; 1 when it is not a file.
export fn readFile(path: [*]const u8, path_len: usize) u32 {
    answer.clearRetainingCapacity();
    const n = Io.nodes.getPtr(path[0..path_len]) orelse return 1;
    if (n.dir) return 1;
    answer.appendSlice(gpa, n.data.items) catch return 1;
    return 0;
}

/// Runs `source` against the tree: what it printed into `answer`, the report
/// (its changes and how it ended) into `report`. `args` is `NAME=VALUE` a
/// line; `name` is what the report calls the script. Returns 0 when it
/// finished, 1 when it stopped.
export fn run(source: [*]const u8, source_len: usize, name: [*]const u8, name_len: usize, args: [*]const u8, args_len: usize, now_ms: i64) u32 {
    Io.setNow(now_ms);
    answer.clearRetainingCapacity();
    report.clearRetainingCapacity();
    var arena: std.heap.ArenaAllocator = .init(gpa);
    defer arena.deinit();
    var given: std.ArrayList(script.Arg) = .empty;
    var lines = std.mem.splitScalar(u8, args[0..args_len], '\n');
    while (lines.next()) |line| {
        const eq = std.mem.indexOfScalar(u8, line, '=') orelse continue;
        given.append(arena.allocator(), .{ .name = line[0..eq], .value = line[eq + 1 ..] }) catch return 1;
    }
    var r = script.runWith(Io.io(), gpa, "", source[0..source_len], .{}, @divFloor(now_ms, 1000), &out_buf, .{ .args = given.items });
    defer r.deinit();
    answer.appendSlice(gpa, r.output) catch {};
    var w: std.Io.Writer.Allocating = .fromArrayList(gpa, &report);
    script.report(&r, name[0..name_len], &w.writer) catch {};
    report = w.toArrayList();
    return if (r.finished()) 0 else 1;
}
