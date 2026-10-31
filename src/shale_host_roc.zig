//! `roc_tree`'s reading: roc-ast's dump (roc-webbox's tools/roc_ast, Roc's
//! own parser printing `AST.toSExprStr`) turned into nodes, so a Roc recipe
//! walks a list rather than re-parsing tabs, spans and `(raw "...")` itself.
//! The prelude's `roc_tree(paths)` runs roc-ast and calls `host("roc_tree",
//! out)` with what it printed.
//!
//! **THE DUMP'S SHAPE**: `== <path>` starts a file; each line below it is a
//! node, its tabs its depth, `(kind @l1.c1-l2.c2 (attr "value") ...` with the
//! span and attributes optional and the node's closing `)`s at the end of its
//! last line. A node may hold a child on its own line (`(mapper (e-tag @...
//! (raw "X")))`): that child is a node too, one deeper. A positional string
//! (`(unary "!"`, `(text "f")` on its own line) is the attribute `value`;
//! every line's first `(` is a node. A string is printed raw, unescaped, so
//! it ends at the first `"` followed by `)`, a space and `(`, or the line's end.
//! A line that is none of these ends the trees: roc-ast's refusal, given back
//! as `said`.
//!
//! Reading is pure (`parse`); `call` makes Shale's values and charges a step
//! per node.

const std = @import("std");
const shale = @import("shale");
const script = shale.script;

const Run = script.interp.Run;
const Value = script.value.Value;
const Error = script.interp.Error;
const Pos = script.ast.Pos;

pub const Attr = struct { name: []const u8, value: []const u8 };

pub const Node = struct {
    kind: []const u8,
    depth: usize,
    /// 1-based within its file; 0 for a root.
    parent: usize,
    kids: std.ArrayList(usize) = .empty,
    /// 0 for a node the dump gives no span (`(args`, `(statements`).
    line: usize = 0,
    col: usize = 0,
    end_line: usize = 0,
    end_col: usize = 0,
    attrs: std.ArrayList(Attr) = .empty,
};

pub const File = struct { path: []const u8, nodes: std.ArrayList(Node) = .empty };

pub const Dump = struct {
    files: []File,
    /// The first line past the trees (roc-ast's refusal), if any.
    said: ?[]const u8,
};

/// Where the dump does not read as a tree: its line and why.
pub const Bad = struct { line: usize, why: []const u8 };

pub const Outcome = union(enum) { ok: Dump, bad: Bad };

/// The names a node's own fields take, which no attribute may.
const reserved = [_][]const u8{ "kind", "depth", "parent", "kids", "line", "col", "end_line", "end_col" };

const Line = struct {
    s: []const u8,
    i: usize = 0,

    fn at(l: *Line, c: u8) bool {
        return l.i < l.s.len and l.s[l.i] == c;
    }

    fn word(l: *Line) []const u8 {
        const from = l.i;
        while (l.i < l.s.len and l.s[l.i] != ' ' and l.s[l.i] != ')' and l.s[l.i] != '(' and l.s[l.i] != '"') l.i += 1;
        return l.s[from..l.i];
    }

    fn int(l: *Line) ?usize {
        const from = l.i;
        while (l.i < l.s.len and std.ascii.isDigit(l.s[l.i])) l.i += 1;
        return std.fmt.parseInt(usize, l.s[from..l.i], 10) catch null;
    }

    /// A raw string at `"`: to the first `"` followed by the line's end, or by
    /// `)`s then the end or ` (` (the next attribute or node). A raw string
    /// holds quotes and `)` (`getElementById("said"), x`), so a `")` alone
    /// does not end it.
    fn string(l: *Line) ?[]const u8 {
        const from = l.i + 1;
        var j = from;
        while (j < l.s.len) : (j += 1) {
            if (l.s[j] != '"') continue;
            const rest = l.s[j + 1 ..];
            const past = std.mem.trimStart(u8, rest, ")");
            if (past.len == 0 or std.mem.startsWith(u8, past, " (")) {
                l.i = j + 1;
                return l.s[from..j];
            }
        }
        return null;
    }

    /// `(word "value")` or `(word bare)` at `(`: an attribute, not a node.
    fn attrAhead(l: *Line) bool {
        var k = l.i + 1;
        while (k < l.s.len and l.s[k] != ' ' and l.s[k] != ')' and l.s[k] != '(') k += 1;
        if (k >= l.s.len or l.s[k] != ' ') return false;
        k += 1;
        if (k < l.s.len and l.s[k] == '"') return true;
        // A bare value (`(rest false)`): a word, then `)`.
        const from = k;
        while (k < l.s.len and l.s[k] != ' ' and l.s[k] != ')' and l.s[k] != '(' and l.s[k] != '@' and l.s[k] != '"') k += 1;
        return k > from and k < l.s.len and l.s[k] == ')';
    }
};

const Reader = struct {
    a: std.mem.Allocator,
    file: *File,
    n: usize,
    bad: ?Bad = null,

    fn fail(r: *Reader, why: []const u8) error{Bad} {
        r.bad = .{ .line = r.n, .why = why };
        return error.Bad;
    }

    fn addAttr(r: *Reader, at: usize, name: []const u8, value: []const u8) error{ Bad, OutOfMemory }!void {
        const nd = &r.file.nodes.items[at - 1];
        const shown = try r.a.dupe(u8, name);
        std.mem.replaceScalar(u8, shown, '-', '_');
        for (reserved) |w| if (std.mem.eql(u8, w, shown)) return r.fail("an attribute takes a node field's name");
        for (nd.attrs.items) |x| if (std.mem.eql(u8, x.name, shown)) return r.fail("an attribute twice in one node");
        try nd.attrs.append(r.a, .{ .name = shown, .value = value });
    }

    /// One node at `(`; returns its index and whether it closed on this line.
    fn node(r: *Reader, l: *Line, depth: usize, parent: usize) error{ Bad, OutOfMemory }!struct { index: usize, closed: bool } {
        l.i += 1;
        const kind = l.word();
        if (kind.len == 0) return r.fail("a node with no kind");
        try r.file.nodes.append(r.a, .{ .kind = kind, .depth = depth, .parent = parent });
        const me = r.file.nodes.items.len;
        if (parent > 0) try r.file.nodes.items[parent - 1].kids.append(r.a, me);
        while (true) {
            while (l.at(' ')) l.i += 1;
            if (l.i >= l.s.len) return .{ .index = me, .closed = false };
            switch (l.s[l.i]) {
                ')' => {
                    l.i += 1;
                    return .{ .index = me, .closed = true };
                },
                '@' => {
                    l.i += 1;
                    const nd = &r.file.nodes.items[me - 1];
                    nd.line = l.int() orelse return r.fail("a span's line");
                    if (!l.at('.')) return r.fail("a span without `.`");
                    l.i += 1;
                    nd.col = l.int() orelse return r.fail("a span's column");
                    if (!l.at('-')) return r.fail("a span without `-`");
                    l.i += 1;
                    nd.end_line = l.int() orelse return r.fail("a span's end line");
                    if (!l.at('.')) return r.fail("a span's end without `.`");
                    l.i += 1;
                    nd.end_col = l.int() orelse return r.fail("a span's end column");
                },
                '"' => {
                    const v = l.string() orelse return r.fail("a string that does not end");
                    try r.addAttr(me, "value", v);
                },
                '(' => if (l.attrAhead()) {
                    l.i += 1;
                    const name = l.word();
                    l.i += 1;
                    const v = if (l.at('"')) l.string() orelse return r.fail("a string that does not end") else l.word();
                    if (!l.at(')')) return r.fail("an attribute not closed");
                    l.i += 1;
                    try r.addAttr(me, name, v);
                } else {
                    const kid = try r.node(l, depth + 1, me);
                    if (!kid.closed) return r.fail("a node left open on another's line");
                },
                else => return r.fail("an unexpected byte in a node"),
            }
        }
    }
};

/// Reads roc-ast's output: each `== path` and its tree, and the first line past
/// the trees, if any. Strings point into `text`.
pub fn parse(a: std.mem.Allocator, text: []const u8) error{OutOfMemory}!Outcome {
    var files: std.ArrayList(File) = .empty;
    // The open line heads, by depth: a line's parent is the nearest shallower.
    var stack: std.ArrayList(usize) = .empty;
    var it = std.mem.splitScalar(u8, text, '\n');
    var n: usize = 0;
    while (it.next()) |raw| {
        n += 1;
        if (raw.len == 0) continue;
        if (std.mem.startsWith(u8, raw, "== ")) {
            try files.append(a, .{ .path = raw[3..] });
            stack.clearRetainingCapacity();
            continue;
        }
        var depth: usize = 0;
        while (depth < raw.len and raw[depth] == '\t') depth += 1;
        if (depth >= raw.len or raw[depth] != '(') {
            if (depth > 0) return .{ .bad = .{ .line = n, .why = "an indented line that is no node" } };
            return .{ .ok = .{ .files = files.items, .said = raw } };
        }
        if (files.items.len == 0) return .{ .bad = .{ .line = n, .why = "a node before any `== path`" } };
        const file = &files.items[files.items.len - 1];
        while (stack.items.len > 0 and file.nodes.items[stack.items[stack.items.len - 1] - 1].depth >= depth) _ = stack.pop();
        const parent: usize = if (stack.items.len > 0) stack.items[stack.items.len - 1] else 0;
        var l: Line = .{ .s = raw, .i = depth };
        var r: Reader = .{ .a = a, .file = file, .n = n };
        const got = r.node(&l, depth, parent) catch |e| switch (e) {
            error.Bad => return .{ .bad = r.bad.? },
            error.OutOfMemory => return error.OutOfMemory,
        };
        // What follows a closed head is its ancestors' `)`s, nothing else.
        if (got.closed and std.mem.trimEnd(u8, raw[l.i..], ")").len != 0) return .{ .bad = .{ .line = n, .why = "more after a node closed" } };
        try stack.append(a, got.index);
    }
    return .{ .ok = .{ .files = files.items, .said = null } };
}

fn put(run: *Run, pos: Pos, t: *script.value.Table, k: []const u8, v: Value) Error!void {
    t.map.put(run.alloc, k, v) catch return run.oom(pos);
}

fn int(n: usize) Value {
    return .{ .int = @intCast(n) };
}

/// `host("roc_tree", out)`: `{files = {{path, nodes}, ...}[, said]}`, each
/// node `{kind, depth, parent, kids, line, col, end_line, end_col}` and its
/// attributes. Charges the text's bytes and a step per node.
pub fn call(backing: std.mem.Allocator, run: *Run, pos: Pos, args: []const Value) Error!Value {
    if (args.len != 1) return run.fail(pos, .argument, "`roc_tree`'s reading takes roc-ast's output, given {d} arguments", .{args.len});
    const text = switch (args[0]) {
        .string => |s| s,
        else => |v| return run.fail(pos, .type, "`roc_tree`'s reading takes a text, not {s}", .{v.typeName()}),
    };
    try script.builtins.chargeBytes(run, pos, text.len);
    var arena = std.heap.ArenaAllocator.init(backing);
    defer arena.deinit();
    const dump = switch (parse(arena.allocator(), text) catch return run.oom(pos)) {
        .ok => |d| d,
        .bad => |b| return run.fail(pos, .argument, "`roc_tree`: roc-ast's output line {d} is not a tree's: {s}", .{ b.line, b.why }),
    };
    const a = run.alloc;
    const files = try run.newTable(pos);
    files.kind = .list;
    for (dump.files) |f| {
        try run.charge(pos, f.nodes.items.len);
        const nodes = try run.newTable(pos);
        nodes.kind = .list;
        for (f.nodes.items) |nd| {
            const m = try run.newTable(pos);
            m.kind = .map;
            try put(run, pos, m, "kind", .{ .string = a.dupe(u8, nd.kind) catch return run.oom(pos) });
            try put(run, pos, m, "depth", int(nd.depth));
            try put(run, pos, m, "parent", int(nd.parent));
            const kids = try run.newTable(pos);
            kids.kind = .list;
            for (nd.kids.items) |k| kids.list.append(a, int(k)) catch return run.oom(pos);
            try put(run, pos, m, "kids", .{ .table = kids });
            try put(run, pos, m, "line", int(nd.line));
            try put(run, pos, m, "col", int(nd.col));
            try put(run, pos, m, "end_line", int(nd.end_line));
            try put(run, pos, m, "end_col", int(nd.end_col));
            for (nd.attrs.items) |x| try put(run, pos, m, a.dupe(u8, x.name) catch return run.oom(pos), .{ .string = a.dupe(u8, x.value) catch return run.oom(pos) });
            nodes.list.append(a, .{ .table = m }) catch return run.oom(pos);
        }
        const fm = try run.newTable(pos);
        fm.kind = .map;
        try put(run, pos, fm, "path", .{ .string = a.dupe(u8, f.path) catch return run.oom(pos) });
        try put(run, pos, fm, "nodes", .{ .table = nodes });
        files.list.append(a, .{ .table = fm }) catch return run.oom(pos);
    }
    const out = try run.newTable(pos);
    out.kind = .map;
    try put(run, pos, out, "files", .{ .table = files });
    if (dump.said) |s| try put(run, pos, out, "said", .{ .string = a.dupe(u8, s) catch return run.oom(pos) });
    return .{ .table = out };
}

test "parse: a same-line child, a positional string, a string-only line's node, a bare attribute, a refusal after" {
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const text =
        "== /x/a.roc\n" ++
        "(file @1.1-2.9\n" ++
        "\t(mapper (e-tag @1.2-1.4 (raw \"Ok\")))\n" ++
        "\t(unary \"!\"\n" ++
        "\t\t(e-ident @2.1-2.2 (raw \"'\"'\") (type \"U8\")))\n" ++
        "\t(exposed-lower-ident @2.3-2.4\n" ++
        "\t\t(text \"f\"))\n" ++
        "\t(field @2.5-2.6 (name \"p\") (rest false)))\n" ++
        "\n" ++
        "/x/b.roc: 1 parse problem(s): no tree\n";
    const got = (try parse(arena.allocator(), text)).ok;
    try std.testing.expectEqual(@as(usize, 1), got.files.len);
    try std.testing.expectEqualStrings("/x/b.roc: 1 parse problem(s): no tree", got.said.?);
    const ns = got.files[0].nodes.items;
    try std.testing.expectEqual(@as(usize, 8), ns.len);
    try std.testing.expectEqualStrings("mapper", ns[1].kind);
    try std.testing.expectEqualStrings("e-tag", ns[2].kind);
    try std.testing.expectEqual(@as(usize, 2), ns[2].depth);
    try std.testing.expectEqual(@as(usize, 2), ns[2].parent);
    try std.testing.expectEqualStrings("Ok", ns[2].attrs.items[0].value);
    try std.testing.expectEqualStrings("value", ns[3].attrs.items[0].name);
    try std.testing.expectEqualStrings("'\"'", ns[4].attrs.items[0].value);
    try std.testing.expectEqualStrings("type", ns[4].attrs.items[1].name);
    try std.testing.expectEqual(@as(usize, 4), ns[4].parent);
    try std.testing.expectEqualStrings("text", ns[6].kind);
    try std.testing.expectEqualStrings("f", ns[6].attrs.items[0].value);
    try std.testing.expectEqual(@as(usize, 6), ns[6].parent);
    try std.testing.expectEqualStrings("false", ns[7].attrs.items[1].value);
    try std.testing.expectEqual(@as(usize, 1), ns[7].parent);
    try std.testing.expectEqual(@as(usize, 4), ns[0].kids.items.len);
}

test "parse: a raw string holding quotes and `)`s ends only where the line's parens do" {
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const got = (try parse(arena.allocator(), "== /x\n(e-string-part @1.1-1.9 (raw \"f(\"a\"), g(\"b\")\"))\n")).ok;
    try std.testing.expectEqualStrings("f(\"a\"), g(\"b\")", got.files[0].nodes.items[0].attrs.items[0].value);
}

test "parse: a node left open on another's line, a node before a path" {
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const open = try parse(arena.allocator(), "== /x\n(file (args\n");
    try std.testing.expectEqualStrings("a node left open on another's line", open.bad.why);
    const early = try parse(arena.allocator(), "(file)\n");
    try std.testing.expectEqualStrings("a node before any `== path`", early.bad.why);
}
