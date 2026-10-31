//! Parses each `.shale` recipe under cookbook/ as the CLI reads a script, with
//! the Linux and git preludes, so a recipe that calls a builtin or prelude
//! function that is gone, or calls it wrongly, fails here and not in the hands
//! of whoever copies it next.

const std = @import("std");
const testing = std.testing;
const script = @import("shale").script;
const host_linux = @import("shale_host_linux.zig");

/// Reads a `use`d library as the cookbook-test's mounts would, `cookbook:`
/// being this tree's `cookbook/`; any other mount is refused.
const CookbookLoader = struct {
    io: std.Io,
    dir: std.Io.Dir,

    fn load(ctx: *anyopaque, into: std.mem.Allocator, path: []const u8, why: *[]const u8) error{OutOfMemory}!?[]const u8 {
        const l: *CookbookLoader = @ptrCast(@alignCast(ctx));
        if (!std.mem.startsWith(u8, path, "cookbook:")) {
            why.* = "a recipe's library is the cookbook's own (`cookbook:`)";
            return null;
        }
        return l.dir.readFileAlloc(l.io, path["cookbook:".len..], into, .limited(1 << 20)) catch |e| {
            why.* = @errorName(e);
            return null;
        };
    }
};

test "cookbook: every recipe parses, with the Linux and git preludes" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var dir = try std.Io.Dir.cwd().openDir(io, "cookbook", .{ .iterate = true });
    defer dir.close(io);
    var walker = try dir.walk(a);
    var loader: CookbookLoader = .{ .io = io, .dir = dir };
    var recipes: usize = 0;
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".shale")) continue;
        const source = try dir.readFileAlloc(io, e.path, a, .limited(1 << 20));
        var stop: ?script.diag.Stop = null;
        _ = script.parse.parseWith(a, a, source, 64, &stop, null, &.{ host_linux.linux_prelude, host_linux.git_prelude }, .{ .ctx = &loader, .load = CookbookLoader.load }) catch {
            if (stop) |s| std.debug.print("cookbook/{s}:{d}:{d}: {s}\n", .{ e.path, s.line, s.column, s.message });
            return error.TestUnexpectedResult;
        };
        recipes += 1;
    }
    // A walk that found nothing would pass, so require at least the zig recipes.
    try testing.expect(recipes >= 9);
}

/// Whether `text` calls `name`: `name(` with no letter, digit or `_` before it, or
/// `attempt(name,`, the language's call by name.
fn calls(text: []const u8, name: []const u8) bool {
    var at: usize = 0;
    while (std.mem.indexOfPos(u8, text, at, name)) |i| : (at = i + 1) {
        const before_ok = i == 0 or !(std.ascii.isAlphanumeric(text[i - 1]) or text[i - 1] == '_');
        const after = i + name.len;
        if (!before_ok or after >= text.len) continue;
        if (text[after] == '(') return true;
        if (text[after] == ',' and std.mem.endsWith(u8, text[0..i], "attempt(")) return true;
    }
    return false;
}

test "preludes: every function of the Linux and git preludes is called by some test, or by a function a test calls" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    // Every test file's text, one string.
    var tests: std.ArrayList(u8) = .empty;
    var dir = try std.Io.Dir.cwd().openDir(io, "src", .{ .iterate = true });
    defer dir.close(io);
    var walker = try dir.walk(a);
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, "_test.zig")) continue;
        try tests.appendSlice(a, try dir.readFileAlloc(io, e.path, a, .limited(4 << 20)));
        try tests.append(a, '\n');
    }
    // Each function's name and its text, to its next `function` line.
    const Function = struct { path: []const u8, name: []const u8, body: []const u8, reached: bool = false };
    var functions: std.ArrayList(Function) = .empty;
    for ([_][]const u8{ "src/shale_prelude_linux.shale", "src/shale_prelude_git.shale" }) |path| {
        const source = try std.Io.Dir.cwd().readFileAlloc(io, path, a, .limited(1 << 20));
        var at: usize = 0;
        while (std.mem.indexOfPos(u8, source, at, "\nfunction ")) |i| {
            const head = i + "\nfunction ".len;
            const open = std.mem.indexOfScalarPos(u8, source, head, '(') orelse break;
            const next = std.mem.indexOfPos(u8, source, head, "\nfunction ") orelse source.len;
            try functions.append(a, .{ .path = path, .name = source[head..open], .body = source[open..next] });
            at = next;
        }
    }
    // Reached: called by a test, or by a function reached.
    for (functions.items) |*f| f.reached = calls(tests.items, f.name);
    var grew = true;
    while (grew) {
        grew = false;
        for (functions.items) |caller| {
            if (!caller.reached) continue;
            for (functions.items) |*f| {
                if (!f.reached and calls(caller.body, f.name)) {
                    f.reached = true;
                    grew = true;
                }
            }
        }
    }
    var uncalled: usize = 0;
    for (functions.items) |f| {
        if (f.reached) continue;
        std.debug.print("{s}: `{s}` is called by no test, nor by a function a test calls\n", .{ f.path, f.name });
        uncalled += 1;
    }
    try testing.expect(functions.items.len >= 40);
    try testing.expectEqual(@as(usize, 0), uncalled);
}

test "preludes: each one compiled whole, its lines with code written for tools/coverage.shale" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var out: std.ArrayList(u8) = .empty;
    // Each prelude parsed as a script, after those it calls, so every function is
    // compiled; a function's lines are its file's, as when it loads as a prelude.
    const files = [_]struct { path: []const u8, after: []const script.parse.Prelude }{
        .{ .path = "src/shale_prelude_linux.shale", .after = &.{} },
        .{ .path = "src/shale_prelude_git.shale", .after = &.{host_linux.linux_prelude} },
    };
    for (files) |f| {
        const source = try std.Io.Dir.cwd().readFileAlloc(io, f.path, a, .limited(1 << 20));
        var stop: ?script.diag.Stop = null;
        const program = try script.parse.parseMeasured(a, a, source, 200, &stop, null, f.after);
        const code = try script.compile.compile(a, a, &program, script.builtins.most_args, 2000);
        // Each function's ops run from its entry to the next function's.
        var entries: std.ArrayList(u32) = .empty;
        for (code.functions) |fun| try entries.append(a, fun.entry);
        try entries.append(a, code.main.entry);
        try entries.append(a, @intCast(code.ops.len));
        std.mem.sort(u32, entries.items, {}, std.sort.asc(u32));
        var lines: std.AutoArrayHashMapUnmanaged(u32, void) = .empty;
        for (code.functions) |fun| {
            // A function of a prelude this one calls is that prelude's.
            if (std.mem.startsWith(u8, fun.name, "the ")) continue;
            const end = for (entries.items) |e| (if (e > fun.entry) break e) else code.ops.len;
            for (code.ops[fun.entry..end]) |op| if (op.pos.line > 0) try lines.put(a, op.pos.line, {});
        }
        try testing.expect(lines.count() > 100);
        for (lines.keys()) |l| try out.print(a, "{s} {d}\n", .{ f.path, l });
    }
    try std.Io.Dir.cwd().createDirPath(io, ".zig-cache/coverage");
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = ".zig-cache/coverage/prelude_code.txt", .data = out.items });
}

test "cookbook: syntax.shale gives each file one verdict by its kind, a good file ok and a broken one its first error, then PASS or FAIL" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var tmp = testing.tmpDir(.{});
    defer tmp.cleanup();
    const a = testing.allocator;
    const shale_exe = @import("shale_test_options").shale;
    const files = [_][3][]const u8{
        .{ "ok.zig", "const a = 1;\n", "ok" },                              .{ "bad.zig", "const a = ;\n", "FAIL" },
        .{ "fmt.zig", "const a  = 1;\n", "FAIL" },                          .{ "ok.zon", ".{ .a = 1 }\n", "ok" },
        .{ "ok.js", "let x = 1;\n", "ok" },                                 .{ "bad.mjs", "let x = ;\n", "FAIL" },
        .{ "ok.shale", "print(1)\n", "ok" },                                .{ "bad.shale", "print((1)\n", "FAIL" },
        .{ "ok.py", "x = 1\n", "ok" },                                      .{ "bad.py", "x = (\n", "FAIL" },
        .{ "ok.sh", "echo hi\n", "ok" },                                    .{ "bad.sh", "if then\n", "FAIL" },
        .{ "Good.roc", "Good := [].{\n\tone : U64\n\tone = 1\n}\n", "ok" }, .{ "Bad.roc", "Bad := [].{\n\tone = (\n}\n", "FAIL" },
        .{ "a.md", "# a\n", "ok" },
    };
    // `roc` and `node` a machine may lack (GitHub's runner has no roc): the
    // recipe says so of their files, and the test holds it to that.
    const has = struct {
        fn tool(al: std.mem.Allocator, i: std.Io, argv: []const []const u8) bool {
            const r = std.process.run(al, i, .{ .argv = argv }) catch return false;
            al.free(r.stdout);
            al.free(r.stderr);
            return true;
        }
    };
    const has_roc = has.tool(a, io, &.{ "roc", "version" });
    const has_node = has.tool(a, io, &.{ "node", "--version" });
    var list: std.ArrayList(u8) = .empty;
    defer list.deinit(a);
    for (files) |f| {
        try tmp.dir.writeFile(io, .{ .sub_path = f[0], .data = f[1] });
        if (list.items.len > 0) try list.append(a, ',');
        try list.appendSlice(a, f[0]);
    }
    const root = try std.fmt.allocPrint(a, ".zig-cache/tmp/{s}", .{&tmp.sub_path});
    defer a.free(root);
    const files_arg = try std.fmt.allocPrint(a, "files={s}", .{list.items});
    defer a.free(files_arg);
    var exe_buf: [std.fs.max_path_bytes]u8 = undefined;
    const exe_n = try std.Io.Dir.cwd().realPathFile(io, shale_exe, &exe_buf);
    const shale_arg = try std.fmt.allocPrint(a, "shale={s}", .{exe_buf[0..exe_n]});
    defer a.free(shale_arg);
    // The zig that builds this test, never the one on PATH.
    const zig_arg = try std.fmt.allocPrint(a, "zig={s}", .{@import("shale_test_options").zig});
    defer a.free(zig_arg);
    const r = try std.process.run(a, io, .{ .argv = &.{ shale_exe, "--root", root, "cookbook/syntax.shale", files_arg, shale_arg, zig_arg } });
    defer a.free(r.stdout);
    defer a.free(r.stderr);
    errdefer std.debug.print("{s}{s}\n", .{ r.stdout, r.stderr });
    for (files) |f| {
        const roc_file = std.mem.endsWith(u8, f[0], ".roc");
        const js_file = std.mem.endsWith(u8, f[0], ".js") or std.mem.endsWith(u8, f[0], ".mjs");
        if ((roc_file and !has_roc) or (js_file and !has_node)) {
            const skip = try std.fmt.allocPrint(a, "skip {s}: no ", .{f[0]});
            defer a.free(skip);
            try testing.expect(std.mem.indexOf(u8, r.stdout, skip) != null);
            continue;
        }
        const want = try std.fmt.allocPrint(a, "{s} {s}{s}", .{ f[2], if (std.mem.eql(u8, f[2], "ok")) "  " else "", f[0] });
        defer a.free(want);
        try testing.expect(std.mem.indexOf(u8, r.stdout, want) != null);
    }
    if (!has_roc) try testing.expect(std.mem.indexOf(u8, r.stdout, "no roc: .roc files not checked") != null);
    if (!has_node) try testing.expect(std.mem.indexOf(u8, r.stdout, "no node: .js and .mjs files not checked") != null);
    try testing.expectEqual(@as(u8, 1), r.term.exited);
    try testing.expect(std.mem.indexOf(u8, r.stderr, "syntax: FAIL") != null);
}

test "cookbook: sealed.shale passes this tree's seals, and a plant that sets one outside its file fails by its line" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const a = testing.allocator;
    const shale_exe = @import("shale_test_options").shale;
    // This tree: a script's `Path` is made by `pathArg` alone (metal-vmm QUEUE 250).
    {
        const r = try std.process.run(a, io, .{ .argv = &.{ shale_exe, "cookbook/sealed.shale", "dir=src", "seals=path_seal:src/script/builtins_store.zig,path_full:src/script/builtins_store.zig,path_rel:src/script/builtins_store.zig" } });
        defer a.free(r.stdout);
        defer a.free(r.stderr);
        errdefer std.debug.print("{s}{s}\n", .{ r.stdout, r.stderr });
        try testing.expectEqual(@as(u8, 0), r.term.exited);
        try testing.expect(std.mem.indexOf(u8, r.stdout, "sealed: PASS") != null);
    }
    // A plant: the seal's own file sets it, its test too; another file, a
    // string and a comment that name it, and a read of it, do not count; one
    // literal and one assignment do, a change to a payload too, and a
    // `location` header set outside its helper's file in any case.
    var tmp = testing.tmpDir(.{});
    defer tmp.cleanup();
    try tmp.dir.createDirPath(io, "sub");
    try tmp.dir.writeFile(io, .{ .sub_path = "id.zig", .data = "const Seal = struct {};\npub const Id = struct { text: []const u8, id_seal: Seal };\npub fn of(s: []const u8) Id {\n    return .{ .text = s, .id_seal = .{} };\n}\ntest {\n    _ = Id{ .text = \"\", .id_seal = .{} };\n}\n" });
    try tmp.dir.writeFile(io, .{ .sub_path = "ok.zig", .data = "const id = @import(\"id.zig\");\n// .id_seal = .{}\nconst s = \".id_seal = .{}\";\nfn f(i: id.Id) void {\n    _ = i.id_seal;\n}\n" });
    try tmp.dir.writeFile(io, .{ .sub_path = "sub/forge.zig", .data = "const id = @import(\"../id.zig\");\nfn f() id.Id {\n    return .{ .text = \"..\", .id_seal = .{} };\n}\nfn g(i: *id.Id) void {\n    i.id_seal = .{};\n    i.text = \"..\";\n}\nconst h: Header = .{ .name = \"Location\", .value = \"//evil\" };\nconst ok = std.ascii.eqlIgnoreCase(n, \"location\");\n" });
    try tmp.dir.writeFile(io, .{ .sub_path = "http.zig", .data = "const h = .{ .name = \"location\", .value = \"/\" };\n" });
    const root = try std.fmt.allocPrint(a, ".zig-cache/tmp/{s}", .{&tmp.sub_path});
    defer a.free(root);
    const r = try std.process.run(a, io, .{ .argv = &.{ shale_exe, "--root", root, "cookbook/sealed.shale", "dir=.", "seals=id_seal:id.zig,text:id.zig", "headers=location:http.zig" } });
    defer a.free(r.stdout);
    defer a.free(r.stderr);
    errdefer std.debug.print("{s}{s}\n", .{ r.stdout, r.stderr });
    try testing.expectEqualStrings("FAIL sub/forge.zig:3: sets `text`, which only id.zig may\nFAIL sub/forge.zig:3: sets `id_seal`, which only id.zig may\nFAIL sub/forge.zig:6: sets `id_seal`, which only id.zig may\nFAIL sub/forge.zig:7: sets `text`, which only id.zig may\nFAIL sub/forge.zig:9: sets a \"Location\" header, which only http.zig may\n", r.stdout);
    try testing.expectEqual(@as(u8, 1), r.term.exited);
}
