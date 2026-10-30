//! Tests read-only mounts and a store that follows no link: every change
//! through an `ro` mount is refused before anything is written while reads
//! work; the mounts file's ` ro`; and a symbolic link anywhere in a path (to a
//! file, to a folder, in the middle, in a mount) is refused for reads, changes,
//! `exists`/`stat` and listings.

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");

fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{host_linux.linux_prelude};
}

test "ro mounts: every change builtin through one is refused before anything is written; a read works" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(t.threaded.io(), "frozen/sub");
    try t.put("frozen/a.zig", "const x = 1;\n\nconst y = x;\n");
    try t.put("mine.zig", "const z = 1;\n");
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "ro = {s}/frozen ro\n", .{t.root}), "/home/x", &why);
    try t.expectOutput("print(read(\"ro:a.zig\"), exists(\"ro:sub\"), #ls(\"ro:\"))", "const x = 1;\n\nconst y = x;\n\ttrue\t2\n");
    // Every change builtin, through the mount, so a new one cannot skip it.
    for ([_][]const u8{
        "replace(\"ro:a.zig\", \"x\")",
        "append(\"ro:a.zig\", \"x\")",
        "create(\"ro:new.zig\", \"x\")",
        "edit(\"ro:a.zig\", \"const x\", \"const w\")",
        "edits_all({{\"ro:a.zig\", {{old = \"const x\", new = \"const w\"}}}})",
        "remove(\"ro:a.zig\")",
        "mkdir(\"ro:d\")",
        "remove_tree(\"ro:sub\")",
        "zig_rename(\"ro:a.zig\", \"x\", \"w\")",
        "zig_move(\"ro:a.zig\", \"mine2.zig\", {\"y\"})",
        "zig_move(\"mine.zig\", \"ro:a.zig\", {\"z\"})",
        // A root path into the mount's folder is the mount's.
        "replace(\"frozen/a.zig\", \"x\")",
        // A set with one file that may not change writes none.
        "edits_all({{\"mine.zig\", {{old = \"const z\", new = \"const q\"}}}, {\"ro:a.zig\", {{old = \"const x\", new = \"const w\"}}}})",
    }) |src| {
        try t.expectStop(src, .atomic, .refused, "mount `ro` is read-only");
        try testing.expectEqualStrings("const x = 1;\n\nconst y = x;\n", try t.file("frozen/a.zig"));
        try testing.expectEqualStrings("const z = 1;\n", try t.file("mine.zig"));
    }
    // `zig_move` out of the mount wrote nothing anywhere.
    try testing.expectError(error.FileNotFound, t.file("mine2.zig"));
}

test "ro mounts: a writable mount inside a read-only one decides for its own folder; named by the read-only one, it stays read-only" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(t.threaded.io(), "all/open");
    try t.put("all/a.txt", "a\n");
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "all = {s}/all ro\nopen = {s}/all/open\n", .{ t.root, t.root }), "/home/x", &why);
    try t.expectOutput("replace(\"open:b.txt\", \"b\") print(read(\"all:open/b.txt\"))", "b\n");
    try t.expectStop("replace(\"all:open/b.txt\", \"x\")", .atomic, .refused, "mount `all` is read-only");
    try t.expectStop("replace(\"all:a.txt\", \"x\")", .atomic, .refused, "mount `all` is read-only");
    try testing.expectEqualStrings("a\n", try t.file("all/a.txt"));
}

test "a walk through a mount: each entry stat'ed where the mount points, its path the mount's; glob and a walk resumed by `after` too" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(t.threaded.io(), "elsewhere/sub");
    try t.put("elsewhere/a.zig", "x");
    try t.put("elsewhere/sub/b.zig", "yy");
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "m = {s}/elsewhere\n", .{t.root}), "/home/x", &why);
    try t.expectOutput("for e in walk(\"m:\", {kind = \"file\"}) do print(e.path, e.size) end", "m:a.zig\t1\nm:sub/b.zig\t2\n");
    try t.expectOutput("for e in walk(\"m:sub\") do print(e.path, e.size) end", "m:sub/b.zig\t2\n");
    try t.expectOutput("print(join(glob(\"m:**/*.zig\"), \" \"))", "m:a.zig m:sub/b.zig\n");
    try t.expectOutput("for e in walk(\"m:\", {after = \"m:a.zig\"}) do print(e.path) end", "m:sub\nm:sub/b.zig\n");
    try t.expectOutput("for e in walk(\"m:sub\", {after = \"m:sub/b.zig\"}) do print(e.path) end", "");
}

test "ro mounts: the mounts file's ` ro` and ` tools`, in either order; any other trailing word, or one twice, refused by line" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    var why: []const u8 = "";
    const ms = try host_linux.parseMounts(a, "a = /x ro\nb = /y\nc = /z ro tools\nd = /w tools ro\ne = /v tools\n", "/home/x", &why);
    try testing.expectEqual(host_linux.Mount.Access.read_only, ms[0].access);
    try testing.expectEqual(host_linux.Mount.Access.read_write, ms[1].access);
    try testing.expectEqualStrings("/x", ms[0].dir);
    for (ms, [_]bool{ false, false, true, true, true }) |m, tools| try testing.expectEqual(tools, m.tools);
    try testing.expectEqual(host_linux.Mount.Access.read_only, ms[3].access);
    try testing.expectEqual(host_linux.Mount.Access.read_write, ms[4].access);
    for ([_]struct { []const u8, []const u8 }{
        .{ "a = /x rw\n", "line 1: `rw` after `a`'s path: only `ro`, `tools` and `secrets` may follow it" },
        .{ "a = /x secrets tools\n", "line 1: `a` is `secrets` and `tools`" },
        .{ "a = /x secrets secrets\n", "line 1: `secrets` twice after `a`'s path" },
        .{ "a = /x y z\n", "line 1: `y` after `a`'s path" },
        .{ "a = /x ro ro\n", "line 1: `ro` twice after `a`'s path" },
    }) |c| {
        try testing.expectError(error.Malformed, host_linux.parseMounts(a, c[0], "/home/x", &why));
        errdefer std.debug.print("{s}: {s}\n", .{ c[0], why });
        try testing.expect(std.mem.indexOf(u8, why, c[1]) != null);
    }
}

test "secrets mounts: no read, listing, change, path or argument reaches a token, by the mount or another way; run's secrets takes one, scrubbed" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(t.threaded.io(), "keys");
    try t.put("keys/tok", "s3cret\n");
    try t.tmp.dir.symLink(t.threaded.io(), "keys", "alias", .{ .is_directory = true });
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "k = {s}/keys secrets\nall = {s}\n", .{ t.root, t.root }), "/home/x", &why);
    try testing.expectEqual(host_linux.Mount.Access.read_only, t.host.mounts[0].access);
    for ([_]struct { []const u8, []const u8 }{
        .{ "read(\"k:tok\")", "the mount `k` holds secrets" },
        .{ "ls(\"k:\")", "the mount `k` holds secrets" },
        .{ "exists(\"k:tok\")", "the mount `k` holds secrets" },
        .{ "stat(\"k:tok\")", "the mount `k` holds secrets" },
        .{ "real(\"k:tok\")", "the mount `k` holds secrets" },
        .{ "replace(\"k:tok\", \"x\")", "the mount `k` holds secrets" },
        // The same folder by the root, or by a mount above it.
        .{ "read(\"keys/tok\")", "is in the mount `k`, which holds secrets" },
        .{ "ls(\"keys\")", "is in the mount `k`, which holds secrets" },
        .{ "read(\"all:keys/tok\")", "is in the mount `k`, which holds secrets" },
        // A program started in it, or handed its path.
        .{ "run({\"shale-fixture\", \"cat\"}, {timeout_ms = 10000, tail = 5, cwd = \"k:\"})", "the mount `k` holds secrets" },
        .{ "run({\"shale-fixture\", \"lines\", \"1\", real(\"\") .. \"/keys/tok\"}, {timeout_ms = 10000, tail = 5})", "`run`'s argument 4 names the mount `k`'s folder" },
        .{ "run({\"shale-fixture\", \"lines\", \"1\", real(\"\") .. \"/keys/tok\"}, {timeout_ms = 10000, tail = 5, want = \"x: y\"})", "names the mount `k`'s folder" },
        // By any other spelling: relative to the program's folder, with `.`
        // or `..`, after an option's `=`, through a link, in `env`, or to a job.
        .{ "run({\"shale-fixture\", \"lines\", \"1\", \"keys/tok\"}, {timeout_ms = 10000, tail = 5})", "`run`'s argument 4 names the mount `k`'s folder" },
        .{ "run({\"shale-fixture\", \"lines\", \"1\", \"./keys/../keys/tok\"}, {timeout_ms = 10000, tail = 5})", "`run`'s argument 4 names the mount `k`'s folder" },
        .{ "run({\"shale-fixture\", \"lines\", \"1\", \"--file=keys/tok\"}, {timeout_ms = 10000, tail = 5})", "`run`'s argument 4 names the mount `k`'s folder" },
        .{ "run({\"shale-fixture\", \"lines\", \"1\", \"alias/tok\"}, {timeout_ms = 10000, tail = 5})", "`run`'s argument 4 names the mount `k`'s folder" },
        .{ "run({\"shale-fixture\", \"lines\", \"1\", \"tok\"}, {timeout_ms = 10000, tail = 5, cwd = \"all:keys\"})", "the mount `k`" },
        .{ "run({\"shale-fixture\", \"env\", \"F\"}, {timeout_ms = 10000, tail = 5, env = {F = \"keys/tok\"}})", "`run`'s `env` F names the mount `k`'s folder" },
        .{ "job_start({\"shale-fixture\", \"lines\", \"1\", \"keys/tok\"}, {log = \"j.log\"})", "`job_start`'s argument 4 names the mount `k`'s folder" },
        // A folder that holds it: `cp -r . out`, `tar` of a parent.
        .{ "run({\"shale-fixture\", \"lines\", \"1\", \".\"}, {timeout_ms = 10000, tail = 5})", "`run`'s argument 4 holds the mount `k`'s folder" },
        .{ "run({\"shale-fixture\", \"lines\", \"1\", real(\"\")}, {timeout_ms = 10000, tail = 5})", "`run`'s argument 4 holds the mount `k`'s folder" },
    }) |c| try t.expectStop(c[0], .atomic, .refused, c[1]);
    try testing.expectEqualStrings("s3cret\n", try t.file("keys/tok"));
    // `secrets` takes one, the value scrubbed; the mount itself is no secret.
    try t.expectOutput("print(run({\"shale-fixture\", \"env\", \"TOK\"}, {timeout_ms = 10000, tail = 5, secrets = {TOK = \"k:tok\"}}).out)", "<secret TOK>\n\n");
    try t.expectStop("run({\"shale-fixture\", \"env\", \"TOK\"}, {timeout_ms = 10000, tail = 5, secrets = {TOK = \"k:\"}})", .atomic, .argument, "a secret is a file in the mount, not the mount");
    // Names are not secrets: the folder above it lists.
    try t.expectOutput("print(contains(join(ls(\"\"), \",\"), \"keys\"))", "true\n");
}

test "secrets mounts: run's stdout_secret stores stdout in one, 0600, never seen; only directly in a secrets mount, never over a file unasked, never with a keep, never on a failure" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const io = t.threaded.io();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(io, "keys/sub");
    try t.tmp.dir.createDirPath(io, "plain");
    try t.put("keys/old", "was\n");
    try t.tmp.dir.symLink(io, "old", "keys/ln", .{});
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "k = {s}/keys secrets\np = {s}/plain\n", .{ t.root, t.root }), "/home/x", &why);

    // Stored: exactly the bytes printed, mode 0600, and the token in neither
    // the result, the output nor the run's records (`ms` set aside: its
    // digits would make the gate's engines differ in work).
    try t.expectOutput("local r = run({\"shale-fixture\", \"cat\"}, {timeout_ms = 10000, stdin = \"tok-7f3a9c\\n\", stdout_secret = \"k:new\"}) r.ms = 0 print(r.code, r.secret_written, r.secret_bytes, r.out, contains(json_write(r), \"7f3a9c\"))", "0\ttrue\t11\t\tfalse\n");
    try testing.expectEqualStrings("tok-7f3a9c\n", try t.file("keys/new"));
    const st = try t.tmp.dir.statFile(io, "keys/new", .{});
    try testing.expectEqual(@as(u32, 0o600), @as(u32, @intCast(st.permissions.toMode() & 0o777)));
    for (t.host.records.items) |r| try testing.expect(std.mem.indexOf(u8, r.line, "7f3a9c") == null);
    // stderr comes back as `out`; stdout does not.
    try t.expectOutput("local r = run({\"shale-fixture\", \"both\", \"tok-b\", \"0\"}, {timeout_ms = 10000, stdout_secret = \"k:b\"}) print(r.out, r.secret_bytes)", "on stderr\n\t5\n");
    try testing.expectEqualStrings("tok-b", try t.file("keys/b"));

    // A file there is refused, the program not run; `secret_replace` replaces it.
    try t.expectStop("run({\"shale-fixture\", \"both\", \"tok-o\", \"0\"}, {timeout_ms = 10000, stdout_secret = \"k:old\"})", .atomic, .refused, "`run`'s `stdout_secret` k:old is there already: `secret_replace = true` to replace it");
    try testing.expectEqualStrings("was\n", try t.file("keys/old"));
    try t.expectOutput("print(run({\"shale-fixture\", \"both\", \"tok-o\", \"0\"}, {timeout_ms = 10000, stdout_secret = \"k:old\", secret_replace = true}).secret_written)", "true\n");
    try testing.expectEqualStrings("tok-o", try t.file("keys/old"));

    // A failure stores nothing, and leaves no temporary file; so does a
    // program that prints nothing.
    try t.expectOutput("local r = run({\"shale-fixture\", \"both\", \"tok-c\", \"3\"}, {timeout_ms = 10000, stdout_secret = \"k:c\"}) print(r.code, r.secret_written, r.secret_bytes, r.out)", "3\tfalse\t0\ton stderr\n\n");
    try t.expectStop("run({\"shale-fixture\", \"exit\", \"0\"}, {timeout_ms = 10000, stdout_secret = \"k:c\"})", .atomic, .argument, "printed nothing: nothing is stored in k:c");
    var names: std.ArrayList([]const u8) = .empty;
    var d = try t.tmp.dir.openDir(io, "keys", .{ .iterate = true });
    defer d.close(io);
    var it = d.iterate();
    while (try it.next(io)) |e| try names.append(a, try a.dupe(u8, e.name));
    std.mem.sort([]const u8, names.items, {}, struct {
        fn lt(_: void, x: []const u8, y: []const u8) bool {
            return std.mem.lessThan(u8, x, y);
        }
    }.lt);
    try testing.expectEqualStrings("b,ln,new,old,sub", try std.mem.join(a, ",", names.items));

    // Anywhere but directly in a secrets mount, or with a keep, a filter or
    // a log: refused before the program runs.
    for ([_]struct { []const u8, script_class, []const u8 }{
        .{ "stdout_secret = \"p:x\"", .refused, "the mount `p` does not hold secrets" },
        .{ "stdout_secret = \"x\"", .refused, "is not in a mount" },
        .{ "stdout_secret = \"k:sub/x\"", .refused, "is in a folder of the mount" },
        .{ "stdout_secret = \"k:../x\"", .refused, "is in a folder of the mount" },
        .{ "stdout_secret = \"k:..\"", .argument, "is not a name" },
        .{ "stdout_secret = \"k:\"", .argument, "a secret is a file in the mount, not the mount" },
        .{ "stdout_secret = \"k:ln\", secret_replace = true", .refused, "is a symbolic link" },
        .{ "stdout_secret = \"k:x\", tail = 5", .argument, "takes no keep, filter or log" },
        .{ "stdout_secret = \"k:x\", whole = 100", .argument, "takes no keep, filter or log" },
        .{ "stdout_secret = \"k:x\", re = \"t\"", .argument, "takes no keep, filter or log" },
        .{ "stdout_secret = \"k:x\", log = \"out.log\"", .argument, "takes no keep, filter or log" },
        .{ "tail = 5, secret_replace = true", .argument, "`secret_replace` is `stdout_secret`'s" },
    }) |c| try t.expectStop(try std.fmt.allocPrint(a, "run({{\"shale-fixture\", \"both\", \"tok-x\", \"0\"}}, {{timeout_ms = 10000, {s}}})", .{c[0]}), .atomic, c[1], c[2]);
    try testing.expectError(error.FileNotFound, t.tmp.dir.statFile(io, "keys/x", .{}));

    // Reads stay refused; `secrets` hands the stored value to a program.
    try t.expectStop("read(\"k:new\")", .atomic, .refused, "the mount `k` holds secrets");
    try t.expectOutput("print(run({\"shale-fixture\", \"env\", \"X\"}, {timeout_ms = 10000, tail = 5, secrets = {X = \"k:new\"}}).out)", "<secret X>\n\n");
}

const script_class = @import("shale").script.diag.Class;

test "the store follows no link: to a file, to a folder, in the middle of a path, in a mount, out of the root: refused for reads, changes, exists, stat and listings" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const io = t.threaded.io();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(io, "dir");
    try t.tmp.dir.createDirPath(io, "frozen");
    try t.put("real.txt", "real");
    try t.put("dir/inner.txt", "inner");
    try t.tmp.dir.symLink(io, "real.txt", "link_file", .{});
    try t.tmp.dir.symLink(io, "dir", "link_dir", .{});
    try t.tmp.dir.symLink(io, "../real.txt", "dir/up", .{});
    try t.tmp.dir.symLink(io, "../real.txt", "frozen/up", .{});
    try t.tmp.dir.symLink(io, "/etc/hostname", "outside", .{});
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "m = {s}/dir\nro = {s}/frozen ro\n", .{ t.root, t.root }), "/home/x", &why);
    for ([_]struct { []const u8, []const u8 }{
        .{ "read(\"link_file\")", "link_file is a symbolic link" },
        .{ "read(\"link_dir/inner.txt\")", "link_dir is a symbolic link" },
        .{ "exists(\"link_file\")", "is a symbolic link" },
        .{ "stat(\"link_dir\")", "is a symbolic link" },
        .{ "ls(\"link_dir\")", "is a symbolic link" },
        .{ "replace(\"link_file\", \"x\")", "is a symbolic link" },
        .{ "read(\"m:up\")", "is a symbolic link" },
        .{ "read(\"ro:up\")", "is a symbolic link" },
        .{ "read(\"outside\")", "outside is a symbolic link" },
    }) |c| try t.expectStop(c[0], .atomic, .refused, c[1]);
    try testing.expectEqualStrings("real", try t.file("real.txt"));
    // What is no link reads as ever, a listing names the links it holds.
    try t.expectOutput("print(read(\"real.txt\"), read(\"m:inner.txt\"), #ls(\"\"))", "real\tinner\t6\n");
}
