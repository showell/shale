//! Tests how the host finds and talks to zig-ast (src/shale_host_zig.zig's
//! `Proc`): where it looks for the program, and what a zig-ast gone wrong
//! stops with: none there, one that does not start, one that is not zig-ast,
//! an answer no host knows, a refused request, and one that ends mid-call.
//! The wrong ones are played by the fixture's `--serve`.

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");
const options = @import("shale_test_options");

fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{host_linux.linux_prelude};
    try t.put("a.zig", "const a = 1;\n");
}

/// A test option's path, absolute: the tests run from the build's root.
fn abs(t: *T, p: []const u8) ![]const u8 {
    if (std.fs.path.isAbsolute(p)) return p;
    return std.fmt.allocPrint(t.arena.allocator(), "{s}/{s}", .{ t.host.cwd_abs, p });
}

test "zig-ast: none anywhere stops, saying how to install it; SHALE_ZIG_AST in the host's environment names one; so does a `tools` mount holding zig-ast/bin/zig-ast" {
    {
        var t: T = undefined;
        try init(&t);
        defer t.deinit();
        t.host.zig_ast_exe = null;
        try t.expectStop("zig_decls(\"a.zig\")", .atomic, .host, "`zig_decls`: no tools:zig-ast/bin/zig-ast, the program that reads Zig: Shale's `shale tools/zig_ast/install.shale` builds it");
    }
    {
        var t: T = undefined;
        try init(&t);
        defer t.deinit();
        t.host.zig_ast_exe = null;
        const a = t.arena.allocator();
        t.host.env = try a.dupe([]const u8, &.{ t.path_env, try std.fmt.allocPrint(a, "SHALE_ZIG_AST={s}", .{try abs(&t, options.zig_ast)}) });
        try t.expectOutput("print(zig_decls(\"a.zig\")[1].name)", "a\n");
    }
    {
        var t: T = undefined;
        try init(&t);
        defer t.deinit();
        t.host.zig_ast_exe = null;
        const a = t.arena.allocator();
        const io = t.threaded.io();
        // A mount that is `tools` but holds no zig-ast is passed by, to the stop.
        try t.tmp.dir.createDirPath(io, "empty");
        t.host.mounts = &.{.{ .name = "tools", .dir = try std.fmt.allocPrint(a, "{s}/empty", .{t.root}), .tools = true }};
        try t.expectStop("zig_tokens(\"a.zig\")", .atomic, .host, "`zig_tokens`: no tools:zig-ast/bin/zig-ast");
    }
    {
        var t: T = undefined;
        try init(&t);
        defer t.deinit();
        t.host.zig_ast_exe = null;
        const a = t.arena.allocator();
        const io = t.threaded.io();
        try t.tmp.dir.createDirPath(io, "tools/zig-ast/bin");
        try t.tmp.dir.symLink(io, try abs(&t, options.zig_ast), "tools/zig-ast/bin/zig-ast", .{});
        t.host.mounts = &.{.{ .name = "tools", .dir = try std.fmt.allocPrint(a, "{s}/tools", .{t.root}), .tools = true }};
        try t.expectOutput("print(#zig_tokens(\"a.zig\"))", "5\n");
    }
}

test "zig-ast gone wrong: one that does not start, one whose first line is not zig-ast's, an answer no host knows, a refused request, and one that ends mid-call, each a host stop naming the builtin" {
    {
        var t: T = undefined;
        try init(&t);
        defer t.deinit();
        t.host.zig_ast_exe = "/nonexistent/zig-ast";
        try t.expectStop("zig_decls(\"a.zig\")", .atomic, .host, "`zig_decls`: zig-ast (/nonexistent/zig-ast) did not start");
    }
    {
        var t: T = undefined;
        try init(&t);
        defer t.deinit();
        // A program that says nothing and ends: once, since the run that
        // started it is the one that finds it gone.
        t.host.zig_ast_exe = "/usr/bin/true";
        var r = t.runOnce("zig_decls(\"a.zig\")", .atomic);
        defer r.deinit();
        try testing.expectEqualStrings("`zig_decls`: zig-ast gave no `zig-ast serve 1 <zig version>` line first", r.stop.?.message);
    }
    {
        var t: T = undefined;
        try init(&t);
        defer t.deinit();
        t.host.zig_ast_exe = try abs(&t, options.fixture);
        try t.expectStop("zig_decls(\"a.zig\")", .atomic, .host, "`zig_decls`: zig-ast's answer has a line this host does not know");
        try t.expectStop("zig_tokens(\"a.zig\")", .atomic, .host, "`zig_tokens`: zig-ast refused the request: BadRequest");
        var r = t.runOnce("zig_imports(\"a.zig\")", .atomic);
        defer r.deinit();
        try testing.expectEqualStrings("`zig_imports`: zig-ast ended before it answered: fixture: no answer to this", r.stop.?.message);
    }
}
