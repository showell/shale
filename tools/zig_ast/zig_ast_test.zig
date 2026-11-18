//! zig-ast's own tests: the reader (`zig_ast.decls`), the rename planner
//! (`plan.zig`) and the mover (`zig_move.move`), pure, over fixtures and over
//! a corpus of real Zig (build.zig's `corpus`: zig-ast's own files and
//! webbox's src/); and the identifier rule the host checks names by
//! (wire.zig), held to std.zig's. What Shale's host does with an answer is
//! tested in Shale's repo, by its C build.

const std = @import("std");
const testing = std.testing;
const corpus = @import("zig_ast_test_options").corpus;
const zig_ast = @import("zig_ast.zig");
const za = zig_ast;
const zm = @import("zig_move.zig");
const zig = @import("plan.zig");
const wire = @import("wire.zig");

test "zig-ast: the host's identifier rule is std.zig's: its keywords are std.zig.Token's, and each name it takes or refuses, std.zig.isValidId does" {
    try testing.expectEqual(std.zig.Token.keywords.keys().len, wire.keywords.len);
    for (wire.keywords) |k| try testing.expect(std.zig.Token.getKeyword(k) != null);
    for ([_][]const u8{ "a", "_", "_a1", "A_b", "1a", "", "a.b", "a-b", "const", "fn", "while", "élan", "a b" }) |s| try testing.expectEqual(std.zig.isValidId(s), wire.isValidId(s));
}

test "zig_decls over the corpus's **/*.zig: in file order, never overlapping, and between them only whitespace and comments" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var files: usize = 0;
    var listed: usize = 0;
    for (corpus) |root| {
        var dir = try std.Io.Dir.cwd().openDir(io, root, .{ .iterate = true });
        defer dir.close(io);
        var walker = try dir.walk(a);
        while (try walker.next(io)) |e| {
            if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".zig")) continue;
            const src = try dir.readFileAllocOptions(io, e.path, a, .limited(16 << 20), .of(u8), 0);
            const got = switch (try zig_ast.decls(a, src)) {
                .ok => |ok| ok.decls,
                else => {
                    std.debug.print("{s}/{s} does not parse\n", .{ root, e.path });
                    return error.TestUnexpectedResult;
                },
            };
            var at: usize = 0;
            for (got) |d| {
                if (d.start < at or d.stop <= d.start) {
                    std.debug.print("{s}/{s}: `{s}` at {d}..{d} overlaps or is out of order (at {d})\n", .{ root, e.path, d.name, d.start, d.stop, at });
                    return error.TestUnexpectedResult;
                }
                try onlyComments(a, e.path, src[at..d.start]);
                at = d.stop;
            }
            try onlyComments(a, e.path, src[at..]);
            files += 1;
            listed += got.len;
        }
    }
    try testing.expect(files > 40);
    try testing.expect(listed > 900);
}

/// Asserts a gap between declarations holds no token but a container doc
/// comment (`//!`).
fn onlyComments(a: std.mem.Allocator, path: []const u8, gap: []const u8) !void {
    const z = try a.dupeSentinel(u8, gap, 0);
    var tz = std.zig.Tokenizer.init(z);
    while (true) {
        const t = tz.next();
        if (t.tag == .eof) return;
        if (t.tag == .container_doc_comment) continue;
        std.debug.print("{s}: a gap holds `{s}` ({s})\n", .{ path, z[t.loc.start..t.loc.end], @tagName(t.tag) });
        return error.TestUnexpectedResult;
    }
}

test "zig_rename's plan, over the corpus's *.zig: renaming a name to one not in the file and back gives the file it began with" {
    const a = testing.allocator;
    var arena = std.heap.ArenaAllocator.init(a);
    defer arena.deinit();
    const ar = arena.allocator();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var files: usize = 0;
    var renamed: usize = 0;
    for (corpus) |root| {
        var dir = try std.Io.Dir.cwd().openDir(io, root, .{ .iterate = true });
        defer dir.close(io);
        var it = dir.iterate();
        while (try it.next(io)) |e| {
            if (e.kind != .file or !std.mem.endsWith(u8, e.name, ".zig")) continue;
            const src = try dir.readFileAllocOptions(io, e.name, ar, .limited(16 << 20), .of(u8), 0);
            for ([_][]const u8{ "a", "std", "run", "pos", "self" }) |from| {
                const away = "zz_renamed_away";
                if (std.mem.indexOf(u8, src, away) != null) continue;
                const there = switch (try zig.plan(ar, src, from, away, null)) {
                    .done => |p| p,
                    else => return error.TestUnexpectedResult,
                };
                const z = try ar.dupeSentinel(u8, there.now, 0);
                const back = switch (try zig.plan(ar, z, away, from, null)) {
                    .done => |p| p,
                    else => return error.TestUnexpectedResult,
                };
                if (!std.mem.eql(u8, back.now, src)) {
                    std.debug.print("{s}/{s}: `{s}` there and back is not the file\n", .{ root, e.name, from });
                    return error.TestUnexpectedResult;
                }
                try testing.expectEqual(there.renamed, back.renamed);
                renamed += there.renamed;
            }
            files += 1;
        }
    }
    try testing.expect(files > 40);
    try testing.expect(renamed > 1000);
}

test "zig_rename's container: a member's declaration, its bare uses inside, and `C.`, `self.`, `@This().` uses; another container's same name left alone" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const got = try zig.planMember(a, harness, "marked", "isSigned", "UidSite");
    const d = got.done;
    try testing.expectEqual(@as(usize, 6), d.plan.renamed);
    try testing.expect(std.mem.indexOf(u8, d.plan.now, "pub fn isSigned(id: []const u8) bool {\n        return id.len > 0;") != null);
    try testing.expect(std.mem.indexOf(u8, d.plan.now, "self.isSigned(id) and UidSite.isSigned(id) and isSigned(id) and @This().isSigned(id)") != null);
    try testing.expect(std.mem.indexOf(u8, d.plan.now, "_ = UidSite.isSigned(\"a\");\n    _ = Other.marked(\"b\");\n    _ = .marked;") != null);
    try testing.expect(std.mem.indexOf(u8, d.plan.now, "pub fn marked(id: []const u8) bool {\n        return id.len == 0;") != null);
}

test "zig_rename's container refuses what it cannot attribute, no such container, or no such member" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const loose = harness ++ "fn f(s: UidSite) bool {\n    return s.marked(\"x\");\n}\n";
    try testing.expectEqual(@as(usize, 21), (try zig.planMember(a, loose, "marked", "isSigned", "UidSite")).unattributed);
    try testing.expectEqual(@as(usize, 0), (try zig.planMember(a, harness, "marked", "isSigned", "Nowhere")).no_container);
    try testing.expect((try zig.planMember(a, harness, "closeWindow", "x", "UidSite")) == .no_member);
}

test "zig_rename's via: `prefix.from` callers through the given prefixes alone, every other `.from` left and its line said" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const d = (try zig.planVia(a, callers, "ready", "indexOrBuild", &.{"search_index"})).done;
    try testing.expectEqual(@as(usize, 1), d.plan.renamed);
    try testing.expect(std.mem.indexOf(u8, d.plan.now, "_ = search_index.indexOrBuild(1);\n    _ = other.ready;\n    _ = x.search_index.ready;") != null);
    try testing.expectEqualSlices(usize, &.{ 5, 6 }, d.left);
    const m = (try zig.planVia(a, callers, "marked", "isSigned", &.{"harness.UidSite"})).done;
    try testing.expectEqual(@as(usize, 1), m.plan.renamed);
    try testing.expect(std.mem.indexOf(u8, m.plan.now, "harness.UidSite.isSigned(\"a\")") != null);
}

const harness =
    \\pub const UidSite = struct {
    \\    const Self = @This();
    \\    pub fn marked(id: []const u8) bool {
    \\        return id.len > 0;
    \\    }
    \\    fn twice(self: Self, id: []const u8) bool {
    \\        return self.marked(id) and UidSite.marked(id) and marked(id) and @This().marked(id);
    \\    }
    \\};
    \\pub const Other = struct {
    \\    pub fn marked(id: []const u8) bool {
    \\        return id.len == 0;
    \\    }
    \\};
    \\test {
    \\    _ = UidSite.marked("a");
    \\    _ = Other.marked("b");
    \\    _ = .marked;
    \\}
    \\
;

const callers =
    \\const search_index = @import("search_index.zig");
    \\const harness = @import("router_harness.zig");
    \\fn f() void {
    \\    _ = search_index.ready(1);
    \\    _ = other.ready;
    \\    _ = x.search_index.ready;
    \\    _ = harness.UidSite.marked("a");
    \\}
    \\
;

fn moveFor(a: std.mem.Allocator, from: [:0]const u8, to: [:0]const u8, names: []const []const u8, mode: zm.CommentMode, after: ?[]const u8) !zm.Moved {
    var wants: std.ArrayList(zm.Want) = .empty;
    for (names) |n| try wants.append(a, .{ .name = n });
    return switch (try zm.move(a, from, to, wants.items, mode, after)) {
        .done => |d| d,
        .refused => |why| {
            std.debug.print("refused: {s}\n", .{why});
            return error.TestUnexpectedResult;
        },
    };
}

test "zig_move: moved lines end as `to`'s do, a CRLF block into an LF file and back, so neither file mixes them" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const crlf = "const a = 1;\r\n\r\nfn b() void {\r\n    _ = a;\r\n}\r\n";
    const m = try moveFor(a, crlf, "const x = 1;\n", &.{"b"}, .doc, null);
    try testing.expectEqualStrings("const x = 1;\n\nfn b() void {\n    _ = a;\n}\n", m.to_now);
    // And back: LF lines into the CRLF file end in CRLF there.
    const back = try moveFor(a, "fn b() void {\n    _ = a;\n}\n", "const a = 1;\r\n", &.{"b"}, .doc, null);
    try testing.expectEqualStrings(crlf, back.to_now);
}

test "zig_move: declarations on lines next to each other stay next to each other; a blank line between stays one (metal-vmm main.zig's addresses, under one doc)" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const m = try moveFor(a, "/// The addresses.\nconst a = 1;\nconst b = 2;\n\nconst c = 3;\nconst keep = 4;\n", "", &.{ "a", "b", "c" }, .doc, null);
    try testing.expectEqualStrings("/// The addresses.\nconst a = 1;\nconst b = 2;\n\nconst c = 3;\n", m.to_now);
    // Not next to each other in `from` (one between, left): apart in `to`.
    const n = try moveFor(a, "const a = 1;\nconst x = 2;\nconst b = 3;\n", "", &.{ "a", "b" }, .doc, null);
    try testing.expectEqualStrings("const a = 1;\n\nconst b = 3;\n", n.to_now);
}

test "zig_move: a section's banner goes with its section when every declaration under it moves, and stays when one is left" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const from = "const a = 1;\n\n// ── the timer ──\n\nconst t1 = 2;\nconst t2 = 3;\n\n// ── the rest ──\n\nconst r = 4;\n";
    const m = try moveFor(a, from, "", &.{ "t1", "t2" }, .doc, null);
    try testing.expectEqualStrings("const a = 1;\n\n// ── the rest ──\n\nconst r = 4;\n", m.from_now);
    try testing.expectEqualStrings("// ── the timer ──\n\nconst t1 = 2;\nconst t2 = 3;\n", m.to_now);
    // One of the two moves: the banner stays over what is left.
    const n = try moveFor(a, from, "", &.{"t1"}, .doc, null);
    try testing.expectEqualStrings("const a = 1;\n\n// ── the timer ──\n\nconst t2 = 3;\n\n// ── the rest ──\n\nconst r = 4;\n", n.from_now);
    try testing.expectEqualStrings("const t1 = 2;\n", n.to_now);
}

test "zig_move: an indented comment between two moved declarations joins their cuts (it sliced backwards)" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const m = try moveFor(a, "const a = 1;\n  // b\nconst b = 2;\n", "", &.{ "a", "b" }, .doc_and_plain, null);
    try testing.expectEqualStrings("", m.from_now);
    // b and its comment sat right under a: they stay so.
    try testing.expectEqualStrings("const a = 1;\n  // b\nconst b = 2;\n", m.to_now);
}

test "zig_move: a comment on a declaration's last line goes with it, and `after` puts a block after the comment" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const m = try moveFor(a, "const a = 1;\nconst b = 2; // why\nconst c = 3;\n", "", &.{"b"}, .doc, null);
    try testing.expectEqualStrings("const a = 1;\nconst c = 3;\n", m.from_now);
    try testing.expectEqualStrings("const b = 2; // why\n", m.to_now);
    const n = try moveFor(a, "const b = 2;\n", "const x = 1; // x's\n\nconst y = 2;\n", &.{"b"}, .doc, "x");
    try testing.expectEqualStrings("const x = 1; // x's\n\nconst b = 2;\n\nconst y = 2;\n", n.to_now);
}

test "zig_move: a CRLF file's separators are CRLF, there and back (found by the repo-wide property)" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const src = "const a = 1;\r\n\r\nconst b = 2;\r\n\r\nconst c = 3;\r\n";
    const m = try moveFor(a, src, "", &.{"b"}, .doc, null);
    try testing.expectEqualStrings("const a = 1;\r\n\r\nconst c = 3;\r\n", m.from_now);
    try testing.expectEqualStrings("const b = 2;\r\n", m.to_now);
    const n = try moveFor(a, "const b = 2;\n", "const x = 1;\r\n\r\nconst y = 2;\r\n", &.{"b"}, .doc, "x");
    try testing.expectEqualStrings("const x = 1;\r\n\r\nconst b = 2;\r\n\r\nconst y = 2;\r\n", n.to_now);
    const o = try moveFor(a, "const b = 2;\n", "const x = 1;\r\n", &.{"b"}, .doc, null);
    try testing.expectEqualStrings("const x = 1;\r\n\r\nconst b = 2;\r\n", o.to_now);
}

/// The corpus's **/*.zig, each read NUL-ended, by path.
fn eachSource(a: std.mem.Allocator, io: std.Io) ![]const struct { path: []const u8, src: [:0]const u8 } {
    const Item = struct { path: []const u8, src: [:0]const u8 };
    var out: std.ArrayList(Item) = .empty;
    for (corpus) |root| {
        var dir = try std.Io.Dir.cwd().openDir(io, root, .{ .iterate = true });
        defer dir.close(io);
        var walker = try dir.walk(a);
        while (try walker.next(io)) |e| {
            if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".zig")) continue;
            try out.append(a, .{ .path = try std.fmt.allocPrint(a, "{s}/{s}", .{ root, e.path }), .src = try dir.readFileAllocOptions(io, e.path, a, .limited(16 << 20), .of(u8), 0) });
        }
    }
    return @ptrCast(out.items);
}

test "zig_move's mover over the corpus's **/*.zig: two declarations moved to an empty file and back give the file it began with, at each boundary" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var tried: usize = 0;
    for (try eachSource(a, threaded.io())) |f| {
        const ds = switch (try za.decls(a, f.src)) {
            .ok => |ok| ok.decls,
            else => continue,
        };
        // The first run of two, neither first nor last, single blank lines
        // around, every name once in the file.
        var i: usize = 1;
        while (i + 2 < ds.len) : (i += 1) {
            if (!(blankApart(f.src, ds[i - 1], ds[i]) and blankApart(f.src, ds[i], ds[i + 1]) and blankApart(f.src, ds[i + 1], ds[i + 2]))) continue;
            if (!(once(ds, ds[i - 1]) and once(ds, ds[i]) and once(ds, ds[i + 1]))) continue;
            break;
        } else continue;
        // The file as it is, and with each hard boundary: a trailing comment
        // on both moved declarations' last lines, an indented comment between
        // them, and every line ended by CRLF.
        for (std.enums.values(Boundary)) |b| try thereAndBack(a, f.path, try withBoundary(a, f.src, ds, i, b), i, b);
        tried += 1;
    }
    try testing.expect(tried > 20);
}

const Boundary = enum { blank, trailing, indented, crlf };

/// `src` with boundary `b` at the declarations `ds[i]` and `ds[i + 1]`.
fn withBoundary(a: std.mem.Allocator, src: [:0]const u8, ds: []const za.Decl, i: usize, b: Boundary) ![:0]const u8 {
    const x = ds[i];
    const y = ds[i + 1];
    return switch (b) {
        .blank => src,
        .trailing => try std.mem.concatWithSentinel(a, u8, &.{ src[0..x.stop], " // x's", src[x.stop..y.stop], " // y's", src[y.stop..] }, 0),
        .indented => try std.mem.concatWithSentinel(a, u8, &.{ src[0 .. x.stop + 2], "    // between\n", src[y.start..] }, 0),
        .crlf => crlf: {
            const n = std.mem.replacementSize(u8, src, "\n", "\r\n");
            const out = try a.allocSentinel(u8, n, 0);
            _ = std.mem.replace(u8, src, "\n", "\r\n", out);
            break :crlf out;
        },
    };
}

/// `ds[i]` and `ds[i + 1]` of `src` (read again, for the boundary may have
/// moved them) to an empty file, and back after `ds[i - 1]`: the file it
/// began with.
fn thereAndBack(a: std.mem.Allocator, path: []const u8, src: [:0]const u8, i: usize, b: Boundary) !void {
    const ds = (try za.decls(a, src)).ok.decls;
    const mode: zm.CommentMode = if (b == .indented) .doc_and_plain else .doc;
    const wants = [_]zm.Want{ .{ .name = ds[i].name, .kind = ds[i].kind }, .{ .name = ds[i + 1].name, .kind = ds[i + 1].kind } };
    const there = switch (try zm.move(a, src, "", &wants, mode, null)) {
        .done => |d| d,
        .refused => |why| {
            std.debug.print("{s} ({t}): {s}\n", .{ path, b, why });
            return error.TestUnexpectedResult;
        },
    };
    const back = switch (try zm.move(a, try a.dupeSentinel(u8, there.to_now, 0), try a.dupeSentinel(u8, there.from_now, 0), &wants, mode, ds[i - 1].name)) {
        .done => |d| d,
        .refused => |why| {
            std.debug.print("{s} ({t}), back: {s}\n", .{ path, b, why });
            return error.TestUnexpectedResult;
        },
    };
    if (!std.mem.eql(u8, back.to_now, src)) {
        std.debug.print("{s} ({t}): `{s}` and `{s}` there and back is not the file\n", .{ path, b, ds[i].name, ds[i + 1].name });
        return error.TestUnexpectedResult;
    }
    try testing.expectEqualStrings("", back.from_now);
}

fn blankApart(src: []const u8, x: za.Decl, y: za.Decl) bool {
    return y.start == x.stop + 2 and std.mem.eql(u8, src[x.stop..y.start], "\n\n") and y.comment_above == null;
}

fn once(ds: []const za.Decl, d: za.Decl) bool {
    if (d.name_form == .none) return false;
    var n: usize = 0;
    for (ds) |e| n += @intFromBool(std.mem.eql(u8, e.name, d.name));
    return n == 1;
}

test "zig_rename and zig_move read identifiers alike: a declaration's uses outside it are the lines a rename changes there" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var compared: usize = 0;
    for (try eachSource(a, threaded.io())) |f| {
        const ds = switch (try za.decls(a, f.src)) {
            .ok => |ok| ok.decls,
            else => continue,
        };
        const starts = try za.lineStarts(a, f.src);
        for (ds) |d| {
            if (d.name_form != .identifier or d.kind == .@"test" or !once(ds, d)) continue;
            const moved = switch (try zm.move(a, f.src, "", &.{.{ .name = d.name, .kind = d.kind }}, .doc, null)) {
                .done => |m| m,
                .refused => continue,
            };
            const renamed = switch (try zig.plan(a, f.src, d.name, "zz_one_reading", null)) {
                .done => |p| p,
                else => return error.TestUnexpectedResult,
            };
            // The rename's lines outside the declaration, each once: what
            // it renames there is uses (a declaration of the name is the
            // one, inside it).
            var want: std.ArrayList(usize) = .empty;
            for (renamed.spans) |s| {
                if (s.o0 >= d.start and s.o0 < d.stop) continue;
                const l = za.lineOf(starts, s.o0);
                if (want.items.len == 0 or want.items[want.items.len - 1] != l) try want.append(a, l);
            }
            var got: std.ArrayList(usize) = .empty;
            for (moved.uses_left) |u| try got.append(a, u.line);
            if (!std.mem.eql(usize, want.items, got.items)) {
                std.debug.print("{s}: `{s}`: zig_rename changes lines {any}, uses_left says {any}\n", .{ f.path, d.name, want.items, got.items });
                return error.TestUnexpectedResult;
            }
            compared += 1;
        }
    }
    try testing.expect(compared > 600);
}
