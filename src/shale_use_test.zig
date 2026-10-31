//! Tests `use("mount:path.shale")`: a library of functions and `const`s,
//! loaded through a mount and charged as a read; its names refused where
//! they are already something, either way round; anything but functions and
//! literal `const`s in it refused; a missing file; a name used above the
//! `use`; and a stop inside it named by the library's file and line.

const std = @import("std");
const testing = std.testing;
const script = @import("shale").script;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");

const lib =
    \\-- a library
    \\const GREETING = "hi"
    \\const NAMES = {"a", "b"}
    \\
    \\function greet(who)
    \\  return GREETING .. " " .. who .. " " .. how_many()
    \\end
    \\
    \\function how_many()
    \\  return #NAMES .. ""
    \\end
    \\
    \\function boom()
    \\  fail("inside")
    \\end
    \\
;

fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{host_linux.linux_prelude};
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(t.threaded.io(), "libs");
    try t.put("libs/lib.shale", lib);
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "m = {s}/libs ro\n", .{t.root}), "/home/x", &why);
}

test "use: a library's functions and consts are the script's below its line, a function calling one declared after it; the file is one read" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.expectOutput(
        \\local x = 1
        \\use("m:lib.shale")
        \\print(greet("you"), GREETING, NAMES[2], x)
        \\function f() return GREETING end
        \\print(f())
    , "hi you 2\thi\tb\t1\nhi\n");
    var r = t.run("use(\"m:lib.shale\")\nprint(how_many())", .atomic);
    defer r.deinit();
    try testing.expect(r.stop == null);
    try testing.expectEqual(@as(u64, 1), r.reads);
    try testing.expectEqual(@as(u64, lib.len), r.read_bytes);
}

test "use: a name already the script's, a builtin's, a prelude's or another library's is refused at the use, naming both places; a script's function after it too" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("libs/other.shale", "function greet(x)\n  return x\nend\n");
    try t.put("libs/builtin.shale", "function split(x)\n  return x\nend\n");
    try t.put("libs/prelude.shale", "function must(x)\n  return x\nend\n");
    try t.put("libs/twice.shale", "const one = 1\nfunction one()\n  return 1\nend\n");
    try t.expectStop("function greet(x) return x end\nuse(\"m:lib.shale\")", .atomic, .name, "`use(\"m:lib.shale\")`: its function `greet` (m:lib.shale:5) is the script's function at line 1 already");
    try t.expectStop("use(\"m:lib.shale\")\nfunction greet(x) return x end", .atomic, .name, "a function `greet` is already `use`d: m:lib.shale's (its line 5), by the `use` at line 1");
    try t.expectStop("use(\"m:lib.shale\")\nuse(\"m:other.shale\")", .atomic, .name, "`use(\"m:other.shale\")`: its function `greet` (m:other.shale:1) is m:lib.shale's, by the `use` at line 1 already");
    try t.expectStop("use(\"m:builtin.shale\")", .atomic, .name, "its function `split` (m:builtin.shale:1) is a builtin already");
    try t.expectStop("use(\"m:prelude.shale\")", .atomic, .name, "its function `must` (m:prelude.shale:1) is the Linux prelude's already");
    try t.expectStop("use(\"m:twice.shale\")", .atomic, .name, "is m:twice.shale's own, twice already");
    try t.expectStop("local GREETING = 1\nuse(\"m:lib.shale\")", .atomic, .name, "its const `GREETING` (m:lib.shale:2) is a local or `const` of the script above this line already");
    try t.expectStop("use(\"m:lib.shale\")\nuse(\"m:lib.shale\")", .atomic, .name, "`use(\"m:lib.shale\")` again: it is used at line 1");
}

test "use: a library holds only functions and literal consts, each from the start of its line, and uses no other library" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("libs/stmt.shale", "function f()\n  return 1\nend\nprint(1)\n");
    try t.put("libs/local.shale", "local x = 1\n");
    try t.put("libs/call.shale", "const X = arg(\"x\", \"\")\n");
    try t.put("libs/nested.shale", "use(\"m:lib.shale\")\n");
    try t.put("libs/indented.shale", "  function f()\n  return 1\n  end\n");
    try t.put("libs/broken.shale", "function f(\n");
    try t.expectStop("use(\"m:stmt.shale\")", .atomic, .syntax, "`use(\"m:stmt.shale\")`: m:stmt.shale:4:1: a library holds only functions and `const`s, found the name `print`");
    try t.expectStop("use(\"m:local.shale\")", .atomic, .syntax, "a library holds only functions and `const`s, found `local`");
    try t.expectStop("use(\"m:call.shale\")", .atomic, .refused, "m:call.shale:1:1: a library's `const X` is a literal");
    try t.expectStop("use(\"m:nested.shale\")", .atomic, .refused, "m:nested.shale:1:1: a library uses no other library");
    try t.expectStop("use(\"m:indented.shale\")", .atomic, .syntax, "each begin at the start of a line");
    try t.expectStop("use(\"m:broken.shale\")", .atomic, .syntax, "`use(\"m:broken.shale\")`: m:broken.shale:");
}

test "use: a missing library, a path not through a mount, a use not at the top level, and a name used above the use line, each refused" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.expectStop("use(\"m:gone.shale\")", .atomic, .store, "`use(\"m:gone.shale\")`: `read(\"m:gone.shale\")`: not found");
    try t.expectStop("use(\"nomount:lib.shale\")", .atomic, .store, "`use(\"nomount:lib.shale\")`:");
    try t.expectStop("use(\"libs/lib.shale\")", .atomic, .argument, "a library is named through a mount");
    try t.expectStop("use(\"/abs/lib.shale\")", .atomic, .argument, "a library is named through a mount");
    try t.expectStop("if true then use(\"m:lib.shale\") end", .atomic, .refused, "`use` is a top-level statement only");
    try t.expectStop("function f()\n  use(\"m:lib.shale\")\nend", .atomic, .refused, "`use` is a top-level statement only");
    try t.expectStop("local u = use(\"m:lib.shale\")", .atomic, .refused, "`use` is a statement of its own at the top level");
    try t.expectStop("print(greet(\"x\"))\nuse(\"m:lib.shale\")", .atomic, .name, "no function `greet` above this line");
    try t.expectStop("print(GREETING)\nuse(\"m:lib.shale\")", .atomic, .name, "no local `GREETING`");
    // A host that loads no library (metal's parse, or one given no loader).
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    var stop: ?script.diag.Stop = null;
    try testing.expectError(error.Parse, script.parse.parse(arena.allocator(), "use(\"m:lib.shale\")", 64, &stop));
    try testing.expectEqualStrings("`use`: this host loads no library", stop.?.message);
}

test "use: a stop inside a library's function names the library's file and line" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    var r = t.run("use(\"m:lib.shale\")\nboom()", .atomic);
    defer r.deinit();
    const s = r.stop orelse return error.TestUnexpectedResult;
    try testing.expectEqualStrings("m:lib.shale's boom", s.function);
    try testing.expectEqual(@as(u32, 14), s.line);
    try testing.expectEqual(@as(usize, 1), s.trail.len);
    try testing.expectEqual(@as(u32, 2), s.trail[0].line);
    var w: std.Io.Writer.Allocating = .init(testing.allocator);
    defer w.deinit();
    try script.cli.printReport(&r, "s.shale", t.preludes, .{}, &w.writer);
    try testing.expect(std.mem.startsWith(u8, w.written(), "── stopped at m:lib.shale:14:3 (in `m:lib.shale's boom`): fail: inside"));
    try testing.expect(std.mem.indexOf(u8, w.written(), "   called from s.shale:2:1\n") != null);
}
