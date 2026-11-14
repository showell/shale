//! Tests `go_tree` through the real go-ast (`~/tools/go-ast/bin/go-ast`,
//! mounted as `tools`, as `zig build cookbook-test` mounts it): a small Go
//! file's tree, and a file go-ast refuses between two it reads. The dump's
//! reading alone is tested in src/shale_host_flat_tree.zig.

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");

/// `tools` over `~/tools`; a box without go-ast fails here, named.
fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{host_linux.linux_prelude};
    const home = testing.environ.getPosix("HOME") orelse return error.NoHome;
    const a = t.arena.allocator();
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "tools = {s}/tools ro tools\n", .{home}), home, &why);
    const exe = try std.fmt.allocPrint(a, "{s}/tools/go-ast/bin/go-ast", .{home});
    std.Io.Dir.cwd().access(t.threaded.io(), exe, .{}) catch {
        std.debug.print("go_tree's tests need {s}: Shale's `shale tools/go_ast/install.shale` builds it\n", .{exe});
        return error.NoGoAst;
    };
}

test "go_tree: a small file: kinds, depths, spans, parents and kids, role, name, op and value" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    // gofmt's tab, which a multiline literal cannot hold.
    try t.put("a.go", "package a\n\nfunc f(s string) int {\n\treturn len(s) + 1\n}\n");
    try t.expectOutput(
        \\const got = go_tree({"a.go"})
        \\print(#got .. " " .. got[1].path .. " [" .. got[1].err .. "]")
        \\local i = 0
        \\for n in got[1].nodes do
        \\  i = i + 1
        \\  local parts = {i, n.kind, n.depth, n.parent, join(n.kids, ","), n.line .. "." .. n.col .. "-" .. n.end_line .. "." .. n.end_col}
        \\  for k in {"role", "name", "op", "lit", "value"} do if has_key(n, k) then push(parts, k .. "=" .. n[k]) end end
        \\  print(join(parts, " "))
        \\end
    ,
        \\1 a.go []
        \\1 File 0 0 2,3 1.1-5.2
        \\2 Ident 1 1  1.9-1.10 role=Name name=a
        \\3 FuncDecl 1 1 4,5,13 3.1-5.2 role=Decls
        \\4 Ident 2 3  3.6-3.7 role=Name name=f
        \\5 FuncType 2 3 6,10 3.1-3.21 role=Type
        \\6 FieldList 3 5 7 3.7-3.17 role=Params
        \\7 Field 4 6 8,9 3.8-3.16 role=List
        \\8 Ident 5 7  3.8-3.9 role=Names name=s
        \\9 Ident 5 7  3.10-3.16 role=Type name=string
        \\10 FieldList 3 5 11 3.18-3.21 role=Results
        \\11 Field 4 10 12 3.18-3.21 role=List
        \\12 Ident 5 11  3.18-3.21 role=Type name=int
        \\13 BlockStmt 2 3 14 3.22-5.2 role=Body
        \\14 ReturnStmt 3 13 15 4.2-4.19 role=List
        \\15 BinaryExpr 4 14 16,19 4.9-4.19 role=Results op=+
        \\16 CallExpr 5 15 17,18 4.9-4.15 role=X
        \\17 Ident 6 16  4.9-4.12 role=Fun name=len
        \\18 Ident 6 16  4.13-4.14 role=Args name=s
        \\19 BasicLit 5 15  4.18-4.19 role=Y lit=INT value=1
        \\
    );
}

test "go_tree: a file go-ast refuses is its err, and the files after it are still read; a missing file and a folder" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("ok.go", "package a\n\nvar x = 1\n");
    try t.put("bad.go", "package a\nfunc (\n");
    try t.expectOutput(
        \\if not exists("dir.go") then mkdir("dir.go") end
        \\for f in go_tree({"ok.go", "bad.go", "ok.go", "dir.go", "gone.go", "ok.go"}) do
        \\  print(f.path .. " " .. #f.nodes .. " [" .. f.err .. "]")
        \\end
        \\print(#go_tree({}))
    ,
        \\ok.go 6 []
        \\bad.go 0 [bad.go: 1 parse problem(s), the first at 2:8: expected ')', found 'EOF']
        \\ok.go 6 []
        \\dir.go 0 [dir.go: is a directory]
        \\gone.go 0 [no such file]
        \\ok.go 6 []
        \\0
        \\
    );
}
