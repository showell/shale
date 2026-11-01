//! **A ROC FILE'S PARSE TREE, AS ROC'S OWN PARSER SEES IT**: for each path
//! on stdin (one a line), `== <path>` and then the file's tree as the
//! compiler's snapshot tests print it (`AST.toSExprStr`, with line and
//! column). The parser is roc-lang/roc's, at the commit roc-version pins, so
//! the tree is what our compiler accepts. Shale hunts over the output.
//!
//! A file that does not parse cleanly is a failure, named, never a tree.
//!
//! **`--flat`: THE SAME TREE, WRITTEN FLAT FOR A PROGRAM** (Shale's `roc_tree` reads it):
//! the tree Roc's printer would print, walked in memory and written one fact
//! a line, so nothing is guessed back out of a layout made for eyes. The first
//! line says the format and whose parser: `roc-ast flat 1 <roc-version>`; a
//! change a reader must know of is a new number. Then:
//!   `n<TAB>parent<TAB>kind<TAB>line<TAB>col<TAB>end_line<TAB>end_col`
//!   `a<TAB>node<TAB>name<TAB>value`
//! Nodes are numbered from 1 in each file, in the order the printer prints
//! them (a node, then its head items, then the children on lines of their
//! own); `parent` 0 is the root's, and a node without a span has 0s. A
//! value escapes `\`, tab, newline and carriage return as `\\`, `\t`, `\n`,
//! `\r`. As the printer lays it out (a node's line is its head items, or with
//! none its first child): on a node's own line, `(name value)` is
//! an attribute and any other `(...)` a child node; on a line of its own,
//! every `(...)` is a child node; a string or atom anywhere is the
//! node's `value`.

const std = @import("std");
const base = @import("base");
const parse = @import("parse");
const build_options = @import("build_options");

/// The `--flat` format's number, in its first line.
const flat_format = 1;

const SExprTree = base.SExprTree;
const Node = @typeInfo(@FieldType(@FieldType(SExprTree, "children"), "items")).pointer.child;

const Tree = struct {
    t: *const SExprTree,
    out: *std.Io.Writer,
    count: usize = 0,

    fn scalar(w: Tree, n: Node) ?[]const u8 {
        return switch (n) {
            .StaticAtom => |s| s,
            .DynamicAtom => |r| w.t.data.items[r.begin..r.end],
            .String => |r| w.t.data.items[r.begin..r.end],
            else => null,
        };
    }

    /// A scalar's text: an atom's or a string's bytes, a number, a boolean.
    fn value(w: Tree, n: Node, buf: []u8) ?[]const u8 {
        if (w.scalar(n)) |s| return s;
        return switch (n) {
            .Boolean => |b| if (b) "true" else "false",
            .UnsignedInt => |u| std.fmt.bufPrint(buf, "{d}", .{u}) catch unreachable, // a u64's digits fit the 24 bytes
            .NodeIdx => |i| std.fmt.bufPrint(buf, "node_{d}", .{i}) catch unreachable, // as above
            else => null,
        };
    }

    fn escaped(w: Tree, s: []const u8) !void {
        for (s) |c| switch (c) {
            '\\' => try w.out.writeAll("\\\\"),
            '\t' => try w.out.writeAll("\\t"),
            '\n' => try w.out.writeAll("\\n"),
            '\r' => try w.out.writeAll("\\r"),
            else => try w.out.writeByte(c),
        };
    }

    fn attr(w: Tree, at: usize, name: []const u8, v: []const u8) !void {
        try w.out.print("a\t{d}\t", .{at});
        try w.escaped(name);
        try w.out.writeByte('\t');
        try w.escaped(v);
        try w.out.writeByte('\n');
    }

    /// `(name value)`, alone: what the printer's reader takes for an attribute.
    fn attrShaped(w: Tree, n: Node) bool {
        const l = switch (n) {
            .List => |l| l,
            else => return false,
        };
        if (l.end - l.begin != 2 or l.attrs_marker != l.end) return false;
        const k = w.t.children.items[l.begin];
        if (k != .StaticAtom and k != .DynamicAtom) return false;
        const v = w.t.children.items[l.begin + 1];
        return v != .List and v != .BytesRange;
    }

    fn node(w: *Tree, n: Node, parent: usize) !void {
        const l = switch (n) {
            .List => |l| l,
            else => return error.NotANode,
        };
        const items = w.t.children.items;
        if (l.end == l.begin) return error.NoKind;
        // The printer's head line: the head items, or with none, the first
        // child, printed right after the `(` as the head would be.
        const head_end = @max(l.attrs_marker, l.begin + 1);
        const kind = w.scalar(items[l.begin]) orelse return error.NoKind;
        var span: [4]u32 = .{ 0, 0, 0, 0 };
        for (items[l.begin + 1 .. head_end]) |c| switch (c) {
            .BytesRange => |r| span = .{ r.region.start_line_idx + 1, r.region.start_col_idx + 1, r.region.end_line_idx + 1, r.region.end_col_idx + 1 },
            else => {},
        };
        w.count += 1;
        const me = w.count;
        try w.out.print("n\t{d}\t", .{parent});
        try w.escaped(kind);
        try w.out.print("\t{d}\t{d}\t{d}\t{d}\n", .{ span[0], span[1], span[2], span[3] });
        var buf: [24]u8 = undefined;
        for (items[l.begin + 1 .. l.end], l.begin + 1..) |c, at| {
            const head = at < head_end;
            switch (c) {
                .BytesRange => {},
                .List => |cl| if (head and w.attrShaped(c)) {
                    try w.attr(me, w.scalar(items[cl.begin]).?, w.value(items[cl.begin + 1], &buf).?);
                } else try w.node(c, me),
                else => try w.attr(me, "value", w.value(c, &buf).?),
            }
        }
    }
};

pub fn main(init: std.process.Init) !void {
    const io = init.io;
    const gpa = init.gpa;
    var arena_state = std.heap.ArenaAllocator.init(gpa);
    defer arena_state.deinit();
    const arena = arena_state.allocator();

    const args = try init.minimal.args.toSlice(arena);
    const as_flat = args.len == 2 and std.mem.eql(u8, args[1], "--flat");
    if (args.len > 2 or (args.len == 2 and !as_flat)) {
        std.debug.print("roc-ast: the paths come on stdin; the one argument is --flat\n", .{});
        std.process.exit(2);
    }

    var in_buf: [4096]u8 = undefined;
    var stdin = std.Io.File.stdin().reader(io, &in_buf);
    const list = try stdin.interface.allocRemaining(arena, .unlimited);

    var out_buf: [64 * 1024]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &out_buf);
    const out = &stdout.interface;

    if (as_flat) {
        try out.print("roc-ast flat {d} {s}\n", .{ flat_format, build_options.roc_pin });
        // Out at once: a crash later (a path that is a folder) loses what is
        // buffered, and a reader must still see whose format follows.
        try out.flush();
    }

    var it = std.mem.splitScalar(u8, list, '\n');
    while (it.next()) |raw| {
        const path = std.mem.trim(u8, raw, " \t\r");
        if (path.len == 0) continue;
        const source = try std.Io.Dir.cwd().readFileAlloc(io, path, arena, .unlimited);

        var env = try base.CommonEnv.init(gpa, source);
        defer env.deinit(gpa);
        try env.calcLineStarts(gpa);
        const ast = try parse.file(gpa, &env);
        defer ast.deinit();

        const problems = ast.tokenize_diagnostics.items.len + ast.parse_diagnostics.items.len;
        if (problems != 0) {
            try out.flush();
            std.debug.print("{s}: {d} parse problem(s): a file that does not parse has no tree to hunt\n", .{ path, problems });
            std.process.exit(1);
        }

        try out.print("== {s}\n", .{path});
        if (as_flat) {
            // As AST.toSExprStr builds it, walked instead of printed.
            var tree = SExprTree.init(gpa);
            defer tree.deinit();
            try ast.store.getFile().pushToSExprTree(gpa, &env, ast, &tree);
            var w: Tree = .{ .t = &tree, .out = out };
            try w.node(tree.stack.items[tree.stack.items.len - 1], 0);
        } else {
            try ast.toSExprStr(gpa, &env, out);
            try out.writeByte('\n');
        }
    }
    try out.flush();
}
