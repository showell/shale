//! `roc_tree`'s reading: `roc-ast --flat` (Shale's tools/roc_ast, Roc's own
//! parser's tree, walked in memory and written for a program) turned into
//! nodes, so a Roc recipe walks a list. The prelude's `roc_tree(paths)` runs
//! roc-ast and calls `host("roc_tree", out)` with what it wrote.
//!
//! **THE FORMAT** (tools/roc_ast/main.zig says how it is made): first
//! `roc-ast flat 1 <roc-version>`, its number the one this reader knows
//! (`flat_format`), else refused, naming the fix; or `roc-parser flat 1
//! <roc-parser version>`, the same format from roc-parser's tree
//! (tools/roc_parser_flat, an experiment: `roc_tree` reads it under the
//! `parser=roc-parser` argument); `== <path>` starts a file;
//! then one fact a line, tab-separated:
//!   `n<TAB>parent<TAB>kind<TAB>line<TAB>col<TAB>end_line<TAB>end_col`
//!   `a<TAB>node<TAB>name<TAB>value`
//! Nodes are numbered from 1 in each file in the order they come; `parent`
//! 0 is a root's, and an attribute names its node. A kind, name or value
//! escapes `\`, tab, newline and carriage return as `\\`, `\t`, `\n`, `\r`.
//! A line that is none of these ends the trees: roc-ast's refusal, given
//! back as `said`.
//!
//! Reading is pure (`parseFlat`); `call` makes Shale's values and charges a
//! step per node.

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
    /// 0 for a node Roc gives no span (`args`, `statements`).
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

/// Where the text does not read as trees: its line and why.
pub const Bad = struct { line: usize, why: []const u8 };

pub const Outcome = union(enum) { ok: Dump, bad: Bad };

/// The names a node's own fields take, which no attribute may.
const reserved = [_][]const u8{ "kind", "depth", "parent", "kids", "line", "col", "end_line", "end_col" };

/// An attribute on node `at` (1-based) of `file`: `-` in its name read as
/// `_`, never a node field's name, never twice on one node.
fn addAttr(a: std.mem.Allocator, file: *File, at: usize, name: []const u8, value: []const u8) error{OutOfMemory}!?[]const u8 {
    const nd = &file.nodes.items[at - 1];
    const shown = try a.dupe(u8, name);
    std.mem.replaceScalar(u8, shown, '-', '_');
    for (reserved) |w| if (std.mem.eql(u8, w, shown)) return "an attribute takes a node field's name";
    for (nd.attrs.items) |x| if (std.mem.eql(u8, x.name, shown)) return "an attribute twice in one node";
    try nd.attrs.append(a, .{ .name = shown, .value = value });
    return null;
}

fn unescape(a: std.mem.Allocator, s: []const u8) error{ OutOfMemory, Bad }![]const u8 {
    if (std.mem.indexOfScalar(u8, s, '\\') == null) return s;
    var out: std.ArrayList(u8) = .empty;
    var i: usize = 0;
    while (i < s.len) : (i += 1) {
        if (s[i] != '\\') {
            try out.append(a, s[i]);
            continue;
        }
        i += 1;
        if (i >= s.len) return error.Bad;
        try out.append(a, switch (s[i]) {
            '\\' => '\\',
            't' => '\t',
            'n' => '\n',
            'r' => '\r',
            else => return error.Bad,
        });
    }
    return out.items;
}

/// The `--flat` format this reader knows, as roc-ast's header says it.
const flat_format = "1";

fn num(s: []const u8) ?usize {
    return std.fmt.parseInt(usize, s, 10) catch null;
}

/// Reads `roc-ast --flat`'s output: each `== path` and its nodes, and the
/// first line past the trees, if any. Strings point into `text` or `a`.
pub fn parseFlat(a: std.mem.Allocator, text: []const u8) error{OutOfMemory}!Outcome {
    var files: std.ArrayList(File) = .empty;
    var it = std.mem.splitScalar(u8, text, '\n');
    var n: usize = 0;
    var header = false;
    while (it.next()) |raw| {
        n += 1;
        if (raw.len == 0) continue;
        if (!header) {
            // Whose format, before anything is read as a tree: roc-ast's,
            // or roc-parser's in the same shape.
            const said = for ([_][]const u8{ "roc-ast flat ", "roc-parser flat " }) |h| {
                if (std.mem.startsWith(u8, raw, h)) break h;
            } else return .{ .bad = .{ .line = n, .why = "no `roc-ast flat 1` header: an older roc-ast, or not its --flat; Shale's `shale tools/roc_ast/install.shale` installs the one this Shale reads" } };
            var words = std.mem.splitScalar(u8, raw[said.len..], ' ');
            if (!std.mem.eql(u8, words.first(), flat_format)) return .{ .bad = .{ .line = n, .why = "roc-ast's --flat is another format than 1, the one this Shale reads: Shale's `shale tools/roc_ast/install.shale` installs the matching roc-ast" } };
            header = true;
            continue;
        }
        if (std.mem.startsWith(u8, raw, "== ")) {
            try files.append(a, .{ .path = raw[3..] });
            continue;
        }
        const node_line = std.mem.startsWith(u8, raw, "n\t");
        if (!node_line and !std.mem.startsWith(u8, raw, "a\t")) return .{ .ok = .{ .files = files.items, .said = raw } };
        const bad = struct {
            fn at(line: usize, why: []const u8) Outcome {
                return .{ .bad = .{ .line = line, .why = why } };
            }
        }.at;
        if (files.items.len == 0) return bad(n, "a node before any `== path`");
        const file = &files.items[files.items.len - 1];
        var f: [6][]const u8 = undefined;
        var count: usize = 0;
        var fit = std.mem.splitScalar(u8, raw[2..], '\t');
        while (fit.next()) |x| {
            if (count == f.len) return bad(n, "a line with too many fields");
            f[count] = x;
            count += 1;
        }
        if (node_line) {
            if (count != 6) return bad(n, "a node line without its six fields");
            const parent = num(f[0]) orelse return bad(n, "a node's parent");
            if (parent > file.nodes.items.len) return bad(n, "a node's parent not yet read");
            const kind = unescape(a, f[1]) catch return bad(n, "a bad escape");
            if (kind.len == 0) return bad(n, "a node with no kind");
            var span: [4]usize = undefined;
            for (f[2..6], 0..) |x, k| span[k] = num(x) orelse return bad(n, "a span's number");
            const depth = if (parent == 0) 0 else file.nodes.items[parent - 1].depth + 1;
            try file.nodes.append(a, .{ .kind = kind, .depth = depth, .parent = parent, .line = span[0], .col = span[1], .end_line = span[2], .end_col = span[3] });
            if (parent > 0) try file.nodes.items[parent - 1].kids.append(a, file.nodes.items.len);
        } else {
            if (count != 3) return bad(n, "an attribute line without its three fields");
            const at = num(f[0]) orelse return bad(n, "an attribute's node");
            if (at == 0 or at > file.nodes.items.len) return bad(n, "an attribute's node not yet read");
            const name = unescape(a, f[1]) catch return bad(n, "a bad escape");
            const value = unescape(a, f[2]) catch return bad(n, "a bad escape");
            if (try addAttr(a, file, at, name, value)) |why| return bad(n, why);
        }
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
    const dump = switch (parseFlat(arena.allocator(), text) catch return run.oom(pos)) {
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

test "parseFlat: nodes, parents, depths, kids, spans and attributes, two files, a refusal after" {
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const text =
        "roc-ast flat 1 nightly-x-abc\n" ++
        "== /x/a.roc\n" ++
        "n\t0\tfile\t1\t1\t5\t14\n" ++
        "n\t1\tstatements\t0\t0\t0\t0\n" ++
        "n\t2\te-binop\t5\t8\t5\t14\n" ++
        "a\t3\top\t==\n" ++
        "n\t3\te-ident\t5\t8\t5\t9\n" ++
        "a\t4\traw\tx\n" ++
        "n\t3\te-tuple-access\t5\t13\t5\t16\n" ++
        "a\t5\tvalue\t.0\n" ++
        "== /x/b.roc\n" ++
        "n\t0\tfile\t1\t1\t1\t2\n" ++
        "a\t1\tline-doc\tkept\n" ++
        "/x/c.roc: 1 parse problem(s): no tree\n";
    const got = (try parseFlat(arena.allocator(), text)).ok;
    try std.testing.expectEqual(@as(usize, 2), got.files.len);
    try std.testing.expectEqualStrings("/x/c.roc: 1 parse problem(s): no tree", got.said.?);
    const ns = got.files[0].nodes.items;
    try std.testing.expectEqual(@as(usize, 5), ns.len);
    try std.testing.expectEqual(@as(usize, 0), ns[1].line);
    try std.testing.expectEqual(@as(usize, 2), ns[2].depth);
    try std.testing.expectEqual(@as(usize, 3), ns[3].depth);
    try std.testing.expectEqual(@as(usize, 3), ns[4].parent);
    try std.testing.expectEqual(@as(usize, 2), ns[2].kids.items.len);
    try std.testing.expectEqual(@as(usize, 5), ns[2].kids.items[1]);
    try std.testing.expectEqualStrings("==", ns[2].attrs.items[0].value);
    try std.testing.expectEqualStrings(".0", ns[4].attrs.items[0].value);
    try std.testing.expectEqual(@as(usize, 16), ns[4].end_col);
    // Numbering starts again in each file, and `-` in a name reads as `_`.
    try std.testing.expectEqual(@as(usize, 0), got.files[1].nodes.items[0].parent);
    try std.testing.expectEqualStrings("line_doc", got.files[1].nodes.items[0].attrs.items[0].name);
}

test "parseFlat: escapes undone, so a string holding quotes, parens, tabs and backslashes reads whole" {
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    // roc-ast's own line for `x = "a\") (b"`: the source's backslash escaped.
    const got = (try parseFlat(arena.allocator(), "roc-ast flat 1 nightly-x-abc\n== /x\nn\t0\te-string-part\t3\t6\t3\t13\na\t1\traw\ta\\\\\") (b\\tc\\nd\\r\n")).ok;
    try std.testing.expectEqualStrings("a\\\") (b\tc\nd\r", got.files[0].nodes.items[0].attrs.items[0].value);
}

test "parseFlat: refused: a node before a path, short or long lines, a parent or node not yet read, a bad escape, no kind, an attribute twice or on a field's name" {
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const cases = [_]struct { text: []const u8, why: []const u8 }{
        .{ .text = "roc-ast flat 1 p\nn\t0\tfile\t1\t1\t1\t2\n", .why = "a node before any `== path`" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t0\tfile\t1\t1\t1\n", .why = "a node line without its six fields" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t0\tfile\t1\t1\t1\t2\t9\n", .why = "a line with too many fields" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t1\tfile\t1\t1\t1\t2\n", .why = "a node's parent not yet read" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t0\tfile\t1\t1\t1\t2\na\t2\traw\tx\n", .why = "an attribute's node not yet read" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t0\tfile\t1\t1\t1\t2\na\t1\traw\n", .why = "an attribute line without its three fields" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t0\tfile\t1\t1\t1\t2\na\t1\traw\tx\\q\n", .why = "a bad escape" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t0\t\t1\t1\t1\t2\n", .why = "a node with no kind" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t0\tfile\tx\t1\t1\t2\n", .why = "a span's number" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t0\tfile\t1\t1\t1\t2\na\t1\traw\tx\na\t1\traw\ty\n", .why = "an attribute twice in one node" },
        .{ .text = "roc-ast flat 1 p\n== /x\nn\t0\tfile\t1\t1\t1\t2\na\t1\tkind\tx\n", .why = "an attribute takes a node field's name" },
    };
    for (cases) |c| try std.testing.expectEqualStrings(c.why, (try parseFlat(a, c.text)).bad.why);
}

test "parseFlat: the header first: format 1 read and passed over; none, or another number, refused naming the install" {
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const ok = (try parseFlat(a, "roc-ast flat 1 nightly-2026-10-30-0602c5f\n== /x\nn\t0\tfile\t1\t1\t1\t2\n")).ok;
    try std.testing.expectEqual(@as(usize, 1), ok.files.len);
    try std.testing.expectEqual(@as(usize, 1), ok.files[0].nodes.items.len);
    // An older roc-ast's --flat, no header: its first line is a file's.
    const old = (try parseFlat(a, "== /x\nn\t0\tfile\t1\t1\t1\t2\n")).bad;
    try std.testing.expect(std.mem.indexOf(u8, old.why, "no `roc-ast flat 1` header") != null);
    try std.testing.expect(std.mem.indexOf(u8, old.why, "install.shale") != null);
    const next = (try parseFlat(a, "roc-ast flat 2 nightly-2027-01-01-abc\n== /x\n")).bad;
    try std.testing.expect(std.mem.indexOf(u8, next.why, "another format than 1") != null);
    try std.testing.expectEqual(@as(usize, 1), next.line);
    // `1` must be the whole word, never a prefix (`10`).
    try std.testing.expect((try parseFlat(a, "roc-ast flat 10 p\n")) == .bad);
    // roc-parser's, the same format, read alike; its other numbers refused.
    const rp = (try parseFlat(a, "roc-parser flat 1 3.0.0-3-gbafa803\n== /x\nn\t0\tfile\t1\t1\t1\t2\na\t1\trp\tFile\n")).ok;
    try std.testing.expectEqualStrings("File", rp.files[0].nodes.items[0].attrs.items[0].value);
    try std.testing.expect((try parseFlat(a, "roc-parser flat 2 x\n")) == .bad);
    try std.testing.expect((try parseFlat(a, "roc-parsers flat 1 x\n")) == .bad);
}
