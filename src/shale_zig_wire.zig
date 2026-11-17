//! **THE WIRE BETWEEN SHALE'S HOST AND ZIG-AST** (tools/zig_ast): the one home
//! of the format both sides speak, imported by the host
//! (src/shale_host_zig.zig) and by zig-ast (its build names this file
//! `zig_wire`). Pure bytes, no parser.
//!
//! zig-ast runs as one process for a run (`zig-ast --serve`), started at the
//! run's first Zig call. It first writes `zig-ast serve <format> <zig
//! version>\n`. Then each request is a frame, `<length>\n` and that many
//! bytes, and each answer is a frame the same way. A frame's body is lines of
//! fields joined by a tab, each field escaped (`\\`, `\t`, `\n`, `\r`). A
//! request's first line is its command (`decls`, `tokens`, `imports`,
//! `rename`, `move`); a line `src<TAB><n>` is followed by n raw bytes, a
//! source file, and a newline. An answer's lines are the events the host
//! acts on in order (a charge, a stop, a declaration): zig-ast's main.zig
//! says each command's.

const std = @import("std");

/// The format's number: a change a reader must know of is a new one.
pub const format = 1;

/// The first line's start, before the Zig version.
pub const header = std.fmt.comptimePrint("zig-ast serve {d} ", .{format});

/// Zig's keywords, which no identifier may be: std.zig.Token.keywords, which
/// zig-ast's tests hold this list to.
pub const keywords = [_][]const u8{
    "addrspace", "align", "allowzero",   "and",    "anyframe",    "anytype",  "asm",       "break",  "callconv", "catch",
    "comptime",  "const", "continue",    "defer",  "else",        "enum",     "errdefer",  "error",  "export",   "extern",
    "fn",        "for",   "if",          "inline", "noalias",     "noinline", "nosuspend", "opaque", "or",       "orelse",
    "packed",    "pub",   "resume",      "return", "linksection", "struct",   "suspend",   "switch", "test",     "threadlocal",
    "try",       "union", "unreachable", "var",    "volatile",    "while",
};

/// Whether `s` is a Zig identifier: a letter or `_`, then letters, digits and
/// `_`, and no keyword (std.zig.isValidId). The host checks a call's names by
/// it before anything is read.
pub fn isValidId(s: []const u8) bool {
    if (s.len == 0) return false;
    for (s, 0..) |c, i| switch (c) {
        '_', 'a'...'z', 'A'...'Z' => {},
        '0'...'9' => if (i == 0) return false,
        else => return false,
    };
    for (keywords) |k| if (std.mem.eql(u8, k, s)) return false;
    return true;
}

/// Writes `s` escaped as a field.
pub fn field(w: *std.Io.Writer, s: []const u8) std.Io.Writer.Error!void {
    for (s) |c| switch (c) {
        '\\' => try w.writeAll("\\\\"),
        '\t' => try w.writeAll("\\t"),
        '\n' => try w.writeAll("\\n"),
        '\r' => try w.writeAll("\\r"),
        else => try w.writeByte(c),
    };
}

/// Writes one line: the fields escaped and joined by tabs. An integer field
/// is written in decimal.
pub fn line(w: *std.Io.Writer, fields: anytype) std.Io.Writer.Error!void {
    inline for (fields, 0..) |f, i| {
        if (i > 0) try w.writeByte('\t');
        switch (@typeInfo(@TypeOf(f))) {
            .int, .comptime_int => try w.print("{d}", .{f}),
            else => try field(w, f),
        }
    }
    try w.writeByte('\n');
}

/// Writes a source block: `src<TAB><n>`, the bytes, a newline.
pub fn source(w: *std.Io.Writer, bytes: []const u8) std.Io.Writer.Error!void {
    try w.print("src\t{d}\n", .{bytes.len});
    try w.writeAll(bytes);
    try w.writeByte('\n');
}

/// A field's text, its escapes undone.
pub fn unescape(a: std.mem.Allocator, s: []const u8) error{ OutOfMemory, BadEscape }![]const u8 {
    if (std.mem.indexOfScalar(u8, s, '\\') == null) return s;
    var out: std.ArrayList(u8) = .empty;
    var i: usize = 0;
    while (i < s.len) : (i += 1) {
        if (s[i] != '\\') {
            try out.append(a, s[i]);
            continue;
        }
        i += 1;
        if (i == s.len) return error.BadEscape;
        try out.append(a, switch (s[i]) {
            '\\' => '\\',
            't' => '\t',
            'n' => '\n',
            'r' => '\r',
            else => return error.BadEscape,
        });
    }
    return out.items;
}

/// Reads a body's lines in order, each split into its fields, unescaped;
/// `src` blocks taken whole.
pub const Lines = struct {
    body: []const u8,
    at: usize = 0,

    /// The next line's fields, or null at the end of the body.
    pub fn next(l: *Lines, a: std.mem.Allocator) error{ OutOfMemory, BadEscape, BadFrame }!?[]const []const u8 {
        if (l.at >= l.body.len) return null;
        const end = std.mem.indexOfScalarPos(u8, l.body, l.at, '\n') orelse return error.BadFrame;
        const text = l.body[l.at..end];
        l.at = end + 1;
        var fields: std.ArrayList([]const u8) = .empty;
        var it = std.mem.splitScalar(u8, text, '\t');
        while (it.next()) |f| try fields.append(a, try unescape(a, f));
        return fields.items;
    }

    /// The bytes of the `src` block whose line gave `n`, and the newline after.
    pub fn block(l: *Lines, n: usize) error{BadFrame}![]const u8 {
        if (l.body.len - l.at < n + 1 or l.body[l.at + n] != '\n') return error.BadFrame;
        const got = l.body[l.at .. l.at + n];
        l.at += n + 1;
        return got;
    }
};

/// A field as a count or an offset.
pub fn int(s: []const u8) error{BadFrame}!usize {
    return std.fmt.parseInt(usize, s, 10) catch error.BadFrame;
}

test "zig wire: a line's fields escape tabs, newlines, CRs and backslashes, and read back as written" {
    var buf: [256]u8 = undefined;
    var w: std.Io.Writer = .fixed(&buf);
    try line(&w, .{ "s", 3, "a\tb\nc\r\\d" });
    try source(&w, "x\ty\n");
    try line(&w, .{"end"});
    const body = w.buffered();
    try std.testing.expectEqualStrings("s\t3\ta\\tb\\nc\\r\\\\d\nsrc\t4\nx\ty\n\nend\n", body);
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    var l: Lines = .{ .body = body };
    const first = (try l.next(arena.allocator())).?;
    try std.testing.expectEqual(@as(usize, 3), first.len);
    try std.testing.expectEqual(@as(usize, 3), try int(first[1]));
    try std.testing.expectEqualStrings("a\tb\nc\r\\d", first[2]);
    const src = (try l.next(arena.allocator())).?;
    try std.testing.expectEqualStrings("x\ty\n", try l.block(try int(src[1])));
    try std.testing.expectEqualStrings("end", (try l.next(arena.allocator())).?[0]);
    try std.testing.expect(try l.next(arena.allocator()) == null);
    try std.testing.expectError(error.BadEscape, unescape(arena.allocator(), "a\\x"));
    try std.testing.expectError(error.BadFrame, l.block(1));
}
