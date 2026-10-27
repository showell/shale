//! The store's builtins that read files: whole or in parts, by lines, the lines
//! holding a literal, and scans that count or find without holding the file.

const std = @import("std");
const ast = @import("ast.zig");
const interp = @import("interp.zig");
const value = @import("value.zig");
const bi = @import("builtins.zig");
const text = @import("builtins_text.zig");

const store = @import("webbox").store;

const Run = interp.Run;
const Value = value.Value;
const Error = interp.Error;
const B = ast.Builtin;
const spec = bi.spec;
const hub = @import("builtins_store.zig");

const Path = hub.Path;
const pathArg = hub.pathArg;
const storeFailed = hub.storeFailed;
const countRead = hub.countRead;

pub const specs = [_]bi.Spec{
    spec(.read, 1, 3, readFile),
    spec(.read_or, 2, 2, readOr),
    spec(.read_lines, 1, 3, readLines),
    spec(.line_at, 2, 2, lineAt),
    spec(.grep, 2, 2, grepFile),
    spec(.count_lines, 1, 3, countLines),
    spec(.count_in, 2, 4, scanner(.count)),
    spec(.find_in, 2, 4, scanner(.find)),
};

/// `read(p)` returns the whole file. `read(p, offset[, n])` returns `n` bytes
/// from 0-based byte `offset`, or to the end, fewer where the file ends, and
/// "" at or past the end. Only the bytes asked for are read and counted, so a
/// part of a huge file costs what the part does. The remaining read budget
/// limits the read (docs/SHALE.md, "The budgets").
fn readFile(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const p = try pathArg(run, pos, .read, args[0], false);
    if (args.len > 1) {
        const offset = try bi.intArg(run, pos, .read, args[1], "its offset");
        if (offset < 0) return run.fail(pos, .argument, "`read`: offset {d} is before the file's start", .{offset});
        const n: ?i64 = if (args.len > 2) try bi.intArg(run, pos, .read, args[2], "its count of bytes") else null;
        if (n != null and n.? < 0) return run.fail(pos, .argument, "`read`: a count of {d} bytes", .{n.?});
        return readPart(run, pos, p, @intCast(offset), if (n) |x| @intCast(x) else null);
    }
    // Sized by stat into one exact buffer, never grown: growing by doubling
    // is several times slower on large files.
    return readPart(run, pos, p, 0, null);
}

/// Returns the size of the file at `p`, failing if it is missing or a folder.
/// One store call, counted as part of the read.
pub fn fileSize(run: *Run, pos: ast.Pos, b: B, p: Path) Error!u64 {
    const st = (store.statOrNull(run.io, run.alloc, p.full()) catch |e| return storeFailed(run, pos, b, p.rel(), e)) orelse return storeFailed(run, pos, b, p.rel(), error.FileNotFound);
    if (st.kind != .file) return storeFailed(run, pos, b, p.rel(), error.IsDir);
    return st.size;
}

fn readPart(run: *Run, pos: ast.Pos, p: Path, offset: u64, n: ?u64) Error!Value {
    try countRead(run, pos);
    return readSized(run, pos, .read, p, try fileSize(run, pos, .read, p), offset, n);
}

/// `read_or(p, default)` returns the whole file, or `default` when nothing is
/// at `p`, in one store read. Anything else (a folder, a failure) is the
/// store's error, as for `read`; the bytes count as a read, under its cap and
/// budget.
fn readOr(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const p = try pathArg(run, pos, .read_or, args[0], false);
    try countRead(run, pos);
    const st = (store.statOrNull(run.io, run.alloc, p.full()) catch |e| return storeFailed(run, pos, .read_or, p.rel(), e)) orelse return args[1];
    if (st.kind != .file) return storeFailed(run, pos, .read_or, p.rel(), error.IsDir);
    return readSized(run, pos, .read_or, p, st.size, 0, null);
}

/// Reads `n` bytes from `offset`, or to the end, of a file whose size is known.
pub fn readSized(run: *Run, pos: ast.Pos, b: B, p: Path, size: u64, offset: u64, n: ?u64) Error!Value {
    return .{ .string = try readSizedIn(run.alloc, run, pos, b, p, size, offset, n) };
}

/// Like `readSized`, but allocates the bytes from `into`: a host's own memory,
/// for a read the script never sees (a parse's source, freed with the parse).
/// Charged as any read.
pub fn readSizedIn(into: std.mem.Allocator, run: *Run, pos: ast.Pos, b: B, p: Path, size: u64, offset: u64, n: ?u64) Error![]const u8 {
    if (offset >= size) return "";
    const want = @min(n orelse size - offset, size - offset);
    if (want > run.budgets.call_bytes) return run.fail(pos, .argument, "`{s}` brings at most {d} bytes in one call, and this would be {d}: read it in parts, `read(p, offset, n)`", .{ @tagName(b), run.budgets.call_bytes, want });
    if (want > run.budgets.read_bytes -| run.read_bytes) return run.budgetStop(pos, .read_bytes);
    const buf = into.alloc(u8, @intCast(want)) catch return run.oom(pos);
    const got = store.readAt(run.io, run.alloc, p.full(), offset, buf) catch |e| return storeFailed(run, pos, b, p.rel(), e);
    run.tally(got);
    run.read_bytes += got;
    try bi.chargeBytes(run, pos, got);
    return buf[0..got];
}

/// The default part size of `read_lines`, clamped to the call-bytes cap.
const read_lines_part: u64 = 64 * 1024;

/// How much `read_lines` and `line_at` read at a time beyond the requested
/// range, to find a line's end or start.
const line_piece: u64 = 512;

/// `read_lines(p[, offset[, n]])` returns whole lines from 0-based byte
/// `offset` (default 0), split as `lines` splits them: at least `n` bytes
/// (default 64 KiB) and on to the end of the line it stops in, so no line is
/// cut. Returns `{lines, next, done}`: `next` is where the next part starts,
/// `done` whether that is the end of the file. One store read per call; the
/// part is at most the call-bytes cap. `offset` should be a line's start, as
/// `next` is; one inside a line yields the rest of that line.
fn readLines(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const b: B = .read_lines;
    const p = try pathArg(run, pos, b, args[0], false);
    const offset: u64 = if (args.len > 1) blk: {
        const o = try bi.intArg(run, pos, b, args[1], "its offset");
        if (o < 0) return run.fail(pos, .argument, "`read_lines`: offset {d} is before the file's start", .{o});
        break :blk @intCast(o);
    } else 0;
    const cap = run.budgets.call_bytes;
    const n: u64 = if (args.len > 2) blk: {
        const x = try bi.intArg(run, pos, b, args[2], "its count of bytes");
        if (x < 1) return run.fail(pos, .argument, "`read_lines`: a part of {d} bytes: it brings at least 1", .{x});
        break :blk @intCast(x);
    } else @min(read_lines_part, cap);
    if (n > cap) return run.fail(pos, .argument, "`read_lines` brings at most {d} bytes in one call, and this asks {d}: a smaller part, and `next` to go on", .{ cap, n });
    try countRead(run, pos);
    const size = try fileSize(run, pos, b, p);
    const start = @min(offset, size);
    const a = run.alloc;
    var got: std.ArrayList(u8) = .empty;
    got.appendSlice(a, (try readSized(run, pos, b, p, size, start, n)).string) catch return run.oom(pos);
    var end = start + got.items.len;
    // Extend a piece at a time to the end of the last line.
    if (end < size and got.items[got.items.len - 1] != '\n') while (true) {
        if (end - start >= cap) return lineTooLong(run, pos, b, start, cap);
        const piece = (try readSized(run, pos, b, p, size, end, @min(line_piece, cap - (end - start)))).string;
        if (piece.len == 0) break;
        if (std.mem.indexOfScalar(u8, piece, '\n')) |i| {
            got.appendSlice(a, piece[0 .. i + 1]) catch return run.oom(pos);
            end += i + 1;
            break;
        }
        got.appendSlice(a, piece) catch return run.oom(pos);
        end += piece.len;
        if (end == size) break;
    };
    const out = try run.newTable(pos);
    const list = try run.newTable(pos);
    list.kind = .list;
    try run.charge(pos, got.items.len);
    var rest: []const u8 = got.items;
    while (rest.len > 0) {
        const nl = std.mem.indexOfScalar(u8, rest, '\n');
        const line = if (nl) |i| rest[0..i] else rest;
        list.list.append(a, .{ .string = line }) catch return run.oom(pos);
        run.tally(line.len + 1);
        rest = if (nl) |i| rest[i + 1 ..] else "";
    }
    out.kind = .map;
    out.map.put(a, "lines", .{ .table = list }) catch return run.oom(pos);
    out.map.put(a, "next", .{ .int = @intCast(end) }) catch return run.oom(pos);
    out.map.put(a, "done", .{ .boolean = end >= size }) catch return run.oom(pos);
    return .{ .table = out };
}

/// `grep(p, s)` returns the lines of the file containing the literal `s`, as
/// `{line, text}` with `line` 1-based, in order, each line once. The file is
/// read whole, so it is limited by the call-bytes cap; a bigger file goes
/// through `read_lines` and `contains` a part at a time. Charges a step per
/// 16 bytes read and a step per byte of each line kept.
fn grepFile(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const b: B = .grep;
    const p = try pathArg(run, pos, b, args[0], false);
    const needle = try bi.stringArg(run, pos, b, args[1], "what it looks for");
    if (needle.len == 0) return run.fail(pos, .argument, "`grep` of \"\" is an error: there is nothing to look for", .{});
    try countRead(run, pos);
    const size = try fileSize(run, pos, b, p);
    if (size > run.budgets.call_bytes) return run.fail(pos, .argument, "`grep` reads a file whole, at most {d} bytes in one call, and {s} is {d}: `read_lines` and `contains` go through it a part at a time", .{ run.budgets.call_bytes, p.rel(), size });
    const whole = (try readSized(run, pos, b, p, size, 0, null)).string;
    const a = run.alloc;
    const finder = try text.Finder.init(run, pos, needle);
    const out = try run.newTable(pos);
    out.kind = .list;
    var line: u64 = 1;
    var counted: usize = 0;
    var from: usize = 0;
    while (try finder.find(whole, from)) |i| {
        const start = if (std.mem.lastIndexOfScalar(u8, whole[0..i], '\n')) |nl| nl + 1 else 0;
        const end = std.mem.indexOfScalarPos(u8, whole, i, '\n') orelse whole.len;
        line += std.mem.count(u8, whole[counted..start], "\n");
        run.tally(start - counted);
        counted = start;
        try run.charge(pos, end - start);
        const m = try run.newTable(pos);
        m.kind = .map;
        m.map.put(a, "line", .{ .int = @intCast(line) }) catch return run.oom(pos);
        m.map.put(a, "text", .{ .string = whole[start..end] }) catch return run.oom(pos);
        out.list.append(a, .{ .table = m }) catch return run.oom(pos);
        from = end + 1;
        if (from > whole.len) break;
    }
    return .{ .table = out };
}

fn lineTooLong(run: *Run, pos: ast.Pos, b: B, at: u64, cap: u64) Error {
    return run.fail(pos, .argument, "`{s}`: the line at offset {d} runs past {d} bytes, the most one call brings", .{ @tagName(b), at, cap });
}

/// `line_at(p, offset)` returns the whole line holding byte `offset`, without
/// its newline (a newline belongs to the line it ends), or nil at or past the
/// end of the file. Reads backward and forward from `offset` a piece at a
/// time; the line is at most the call-bytes cap. One store read per call.
fn lineAt(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const b: B = .line_at;
    const p = try pathArg(run, pos, b, args[0], false);
    const o = try bi.intArg(run, pos, b, args[1], "its offset");
    if (o < 0) return run.fail(pos, .argument, "`line_at`: offset {d} is before the file's start", .{o});
    const offset: u64 = @intCast(o);
    try countRead(run, pos);
    const size = try fileSize(run, pos, b, p);
    if (offset >= size) return .nil;
    const cap = run.budgets.call_bytes;
    const piece_most = @min(line_piece, cap);
    var lo = offset;
    while (lo > 0) {
        const take = @min(lo, piece_most);
        const piece = (try readSized(run, pos, b, p, size, lo - take, take)).string;
        if (std.mem.lastIndexOfScalar(u8, piece, '\n')) |i| {
            lo = lo - take + i + 1;
            break;
        }
        lo -= take;
        if (offset - lo >= cap) return lineTooLong(run, pos, b, offset, cap);
    }
    var hi = offset;
    while (hi < size) {
        const piece = (try readSized(run, pos, b, p, size, hi, @min(piece_most, size - hi))).string;
        if (std.mem.indexOfScalar(u8, piece, '\n')) |i| {
            hi += i;
            break;
        }
        hi += piece.len;
        if (hi - lo > cap) return lineTooLong(run, pos, b, offset, cap);
    }
    if (hi - lo > cap) return lineTooLong(run, pos, b, offset, cap);
    return readSized(run, pos, b, p, size, lo, hi - lo);
}

/// How much of a file a scan holds at once.
const scan_chunk = 64 * 1024;

const Scan = enum { count, find };

fn scanner(comptime how: Scan) bi.Impl {
    return struct {
        fn f(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
            const b: B = if (how == .count) .count_in else .find_in;
            const p = try pathArg(run, pos, b, args[0], false);
            const needle = try bi.stringArg(run, pos, b, args[1], "what it looks for");
            if (needle.len == 0) return run.fail(pos, .argument, "`{s}` of \"\" is an error: there is nothing to look for", .{@tagName(b)});
            if (needle.len > scan_chunk) return run.fail(pos, .argument, "`{s}`: what it looks for is longer than {d} bytes", .{ @tagName(b), scan_chunk });
            const r = try scan(run, pos, b, p, needle, how == .find, try rangeArgs(run, pos, b, args[2..]));
            return switch (how) {
                .count => .{ .int = @intCast(r.count) },
                .find => if (r.first) |at| .{ .int = @intCast(at) } else .nil,
            };
        }
    }.f;
}

/// `count_lines(p[, offset[, n]])` returns the number of lines in a file, as
/// `#lines(read(p))` counts them (an unterminated last line counts), without
/// holding the file: the scan uses at most 64 KiB of arena. For a range, counts
/// the newlines in it plus the last line if the range reaches the end, so the
/// counts of the parts add up to the count of the whole.
fn countLines(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const p = try pathArg(run, pos, .count_lines, args[0], false);
    const r = try scan(run, pos, .count_lines, p, "\n", false, try rangeArgs(run, pos, .count_lines, args[1..]));
    return .{ .int = @intCast(r.count + @intFromBool(r.to_end and r.size > 0 and r.last != '\n')) };
}

/// A scan's range: from `offset`, `n` bytes, or to the end.
const Range = struct { offset: u64 = 0, n: ?u64 = null };

fn rangeArgs(run: *Run, pos: ast.Pos, b: B, args: []const Value) Error!Range {
    if (args.len == 0) return .{};
    const offset = try bi.intArg(run, pos, b, args[0], "its offset");
    if (offset < 0) return run.fail(pos, .argument, "`{s}`: offset {d} is before the file's start", .{ @tagName(b), offset });
    const n: ?i64 = if (args.len > 1) try bi.intArg(run, pos, b, args[1], "its count of bytes") else null;
    if (n != null and n.? < 0) return run.fail(pos, .argument, "`{s}`: a count of {d} bytes", .{ @tagName(b), n.? });
    return .{ .offset = @intCast(offset), .n = if (n) |x| @intCast(x) else null };
}

const Scanned = struct { count: u64 = 0, first: ?u64 = null, size: u64 = 0, last: u8 = 0, to_end: bool = false };

/// Reads the file `scan_chunk` bytes at a time into one reused buffer,
/// counting non-overlapping occurrences of `needle` or stopping at the first
/// (`first_only`), across chunk boundaries. One store read per call; every byte
/// is counted against the read budget and charged.
///
/// Only matches that start in `range` count, and `range` is at most
/// `call_bytes` long. A match that starts in it is read to its end, at most
/// `needle.len - 1` bytes past it. So a find over a file in parts finds what
/// one over the whole file would, and each scan is bounded.
fn scan(run: *Run, pos: ast.Pos, b: B, p: Path, needle: []const u8, first_only: bool, range: Range) Error!Scanned {
    try countRead(run, pos);
    const file_size = try fileSize(run, pos, b, p);
    const start = @min(range.offset, file_size);
    const end = if (range.n) |n| start + @min(n, file_size - start) else file_size;
    if (end - start > run.budgets.call_bytes) return run.fail(pos, .argument, "`{s}` scans at most {d} bytes in one call, and this would be {d}: scan it in parts, `{s}(p, {s}offset, n)`", .{ @tagName(b), run.budgets.call_bytes, end - start, @tagName(b), if (b == .count_lines) "" else "s, " });
    const keep_most = needle.len - 1;
    // Reading at most `needle.len - 1` past the range guarantees every match
    // found starts in it.
    const size = @min(file_size, end + keep_most);
    var r: Scanned = .{ .size = file_size, .to_end = end == file_size };
    if (end == start) return r;
    const finder = try text.Finder.init(run, pos, needle);
    // One buffer serves every scan of the run: a fresh one per call costs
    // 64 KiB of arena per file, and scanning many files would exhaust a
    // small arena. It is sized once for the longest needle and a full chunk.
    if (run.scan_buf.len == 0) run.scan_buf = run.alloc.alloc(u8, 2 * scan_chunk) catch return run.oom(pos);
    const buf = run.scan_buf[0..@intCast(@min(size - start, scan_chunk) + keep_most)];
    // `buf[0..kept]` holds the last window's tail, from file offset `base`.
    var kept: usize = 0;
    var base: u64 = start;
    var at: u64 = start;
    while (at < size) {
        const want: usize = @intCast(@min(size - at, buf.len - kept));
        if (want > run.budgets.read_bytes -| run.read_bytes) return run.budgetStop(pos, .read_bytes);
        const got = store.readAt(run.io, run.alloc, p.full(), at, buf[kept..][0..want]) catch |e| return storeFailed(run, pos, b, p.rel(), e);
        run.read_bytes += got;
        try bi.chargeBytes(run, pos, got);
        run.tally(got);
        if (got == 0) break; // the file shrank under the scan
        at += got;
        const window = buf[0 .. kept + got];
        r.last = window[window.len - 1];
        var from: usize = 0;
        while (try finder.find(window, from)) |i| {
            if (r.first == null) r.first = base + i;
            r.count += 1;
            if (first_only) return r;
            from = i + needle.len;
        }
        // Keep the tail that could still start a match: the last `keep_most`
        // bytes not already consumed by a match.
        const next = @max(from, window.len -| keep_most);
        kept = window.len - next;
        std.mem.copyForwards(u8, buf[0..kept], window[next..]);
        base += next;
    }
    return r;
}
