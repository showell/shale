//! Tests `roc_tree` through the real roc-ast (`~/tools/roc-ast/bin/roc-ast`,
//! mounted as `tools`, as `zig build cookbook-test` mounts it): a small Roc
//! file's tree, a file roc-ast refuses between two it reads, and an empty
//! file. The dump's reading alone is tested in src/shale_host_roc.zig.

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");

/// `tools` over `~/tools`; a box without roc-ast fails here, named.
fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{host_linux.linux_prelude};
    const home = testing.environ.getPosix("HOME") orelse return error.NoHome;
    const a = t.arena.allocator();
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "tools = {s}/tools ro tools\n", .{home}), home, &why);
    const exe = try std.fmt.allocPrint(a, "{s}/tools/roc-ast/bin/roc-ast", .{home});
    std.Io.Dir.cwd().access(t.threaded.io(), exe, .{}) catch {
        std.debug.print("roc_tree's tests need {s}: roc-webbox's `shale tools/roc_ast/install.shale` builds it\n", .{exe});
        return error.NoRocAst;
    };
}

test "roc_tree: a small file: kinds, depths, spans, parents and kids, raw and name" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("a.roc",
        \\module [f]
        \\
        \\f : Str -> Str
        \\f = |s| Str.trim(s)
        \\
    );
    try t.expectOutput(
        \\const got = roc_tree({"a.roc"})
        \\print(#got .. " " .. got[1].path .. " [" .. got[1].err .. "]")
        \\local i = 0
        \\for n in got[1].nodes do
        \\  i = i + 1
        \\  local parts = {i, n.kind, n.depth, n.parent, join(n.kids, ","), n.line .. "." .. n.col .. "-" .. n.end_line .. "." .. n.end_col}
        \\  for k in {"raw", "name", "value"} do if has_key(n, k) then push(parts, k .. "=" .. n[k]) end end
        \\  print(join(parts, " "))
        \\end
    ,
        \\1 a.roc []
        \\1 file 0 0 2,6 1.1-4.20
        \\2 module 1 1 3 1.1-1.11
        \\3 exposes 2 2 4 1.8-1.11
        \\4 exposed-lower-ident 3 3 5 1.9-1.10
        \\5 text 4 4  0.0-0.0 value=f
        \\6 statements 1 1 7,11 0.0-0.0
        \\7 s-type-anno 2 6 8 3.1-3.15 name=f
        \\8 ty-fn 3 7 9,10 3.5-3.15
        \\9 ty 4 8  3.5-3.8 name=Str
        \\10 ty 4 8  3.12-3.15 name=Str
        \\11 s-decl 2 6 12,13 4.1-4.20
        \\12 p-ident 3 11  4.1-4.2 raw=f
        \\13 e-lambda 3 11 14,16 4.5-4.20
        \\14 args 4 13 15 0.0-0.0
        \\15 p-ident 5 14  4.6-4.7 raw=s
        \\16 e-apply 4 13 17,18 4.9-4.20
        \\17 e-ident 5 16  4.9-4.17 raw=Str.trim
        \\18 e-ident 5 16  4.18-4.19 raw=s
        \\
    );
}

test "roc_tree: a file roc-ast refuses is its err, and the files after it are still read; a missing file, and a stop roc-ast leaves unflushed" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("ok.roc", "module [x]\n\nx = 1\n");
    try t.put("bad.roc", "x = (\n");
    // A folder roc-ast cannot read stops it before it flushes the trees
    // ahead of it: the last of them runs again alone.
    try t.expectOutput(
        \\if not exists("dir.roc") then mkdir("dir.roc") end
        \\for f in roc_tree({"ok.roc", "bad.roc", "ok.roc", "ok.roc", "dir.roc", "ok.roc", "gone.roc", "ok.roc"}) do
        \\  print(f.path .. " " .. #f.nodes .. " [" .. f.err .. "]")
        \\end
    ,
        \\ok.roc 9 []
        \\bad.roc 0 [bad.roc: 1 parse problem(s): a file that does not parse has no tree to hunt]
        \\ok.roc 9 []
        \\ok.roc 9 []
        \\dir.roc 0 [error: IsDir]
        \\ok.roc 9 []
        \\gone.roc 0 [no such file]
        \\ok.roc 9 []
        \\
    );
}

test "roc_tree: an empty file is a tree of three nodes; no paths is no files" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("empty.roc", "");
    try t.expectOutput(
        \\const got = roc_tree({"empty.roc"})
        \\for n in got[1].nodes do print(n.kind .. " " .. n.depth .. " " .. n.line .. "." .. n.col .. "-" .. n.end_line .. "." .. n.end_col) end
        \\print(#roc_tree({}))
    ,
        \\file 0 1.1-1.1
        \\type-mod 1 1.1-1.1
        \\statements 1 0.0-0.0
        \\0
        \\
    );
}
