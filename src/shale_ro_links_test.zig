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
        .{ "a = /x rw\n", "line 1: only `ro`, `tools` and `secrets` may follow `a`'s path (and a path has no space)" },
        .{ "a = /x secrets tools\n", "line 1: `a` is `secrets` and `tools`: a toolchain's programs are not tokens" },
        .{ "a = /x secrets secrets\n", "line 1: `secrets` twice after `a`'s path" },
        .{ "a = /x y z\n", "line 1: only `ro`, `tools` and `secrets` may follow `a`'s path (and a path has no space)" },
        .{ "a = /x ro ro\n", "line 1: `ro` twice after `a`'s path" },
    }) |c| {
        try testing.expectError(error.Malformed, host_linux.parseMounts(a, c[0], "/home/x", &why));
        errdefer std.debug.print("{s}: {s}\n", .{ c[0], why });
        try testing.expectEqualStrings(c[1], why);
    }
}

test "secrets mounts: the mounts file's mistakes name the line and the mount, never the line's text, a path or a word after one" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    var why: []const u8 = "";
    // Each line holds a planted folder, `.planted-keys`, where a secrets
    // mount's would be. Every case is judged before the test fails, so a
    // planted echo names each message that leaks.
    var wrong: usize = 0;
    for ([_]struct { []const u8, []const u8 }{
        // No `=`: no mount can be named, so the line number alone.
        .{ "# c\n/home/x/.planted-keys secrets\n", "line 2: the line is not `name = value`" },
        // A path where the name goes: not a name, so not quoted.
        .{ "/home/x/.planted-keys = /y\n", "line 1: the name before `=` is not a mount's name: letters, digits, `_` and `-`" },
        // The name is judged before the words, so a bad one is never quoted
        // by a later message.
        .{ "~/.planted-keys = /y rw\n", "line 1: the name before `=` is not a mount's name: letters, digits, `_` and `-`" },
        .{ "~/.planted-keys = /y ro ro\n", "line 1: the name before `=` is not a mount's name: letters, digits, `_` and `-`" },
        // A path with a space: its tail is a word after the path.
        .{ "keys = /home/x/my .planted-keys secrets\n", "line 1: only `ro`, `tools` and `secrets` may follow `keys`'s path (and a path has no space)" },
        .{ "keys = home/x/.planted-keys secrets\n", "line 1: `keys`'s value is relative: an absolute path, or one from `~/`" },
        .{ "keys = /home/x/.planted-keys secrets secrets\n", "line 1: `secrets` twice after `keys`'s path" },
        .{ "keys = /home/x/.planted-keys secrets tools\n", "line 1: `keys` is `secrets` and `tools`: a toolchain's programs are not tokens" },
        .{ "keys = /home/x/.planted-keys secrets\nkeys = /home/x/.planted-keys secrets\n", "line 2: the mount `keys` is named twice" },
        // An ordinary mount's mistake still names its mount and its line.
        .{ "runs = /r\n\nwork = work/.planted-keys\n", "line 3: `work`'s value is relative: an absolute path, or one from `~/`" },
    }) |c| {
        try testing.expectError(error.Malformed, host_linux.parseMounts(a, c[0], "/home/x", &why));
        if (!std.mem.eql(u8, c[1], why) or std.mem.indexOf(u8, why, "planted") != null) {
            std.debug.print("{s}  said: {s}\n  want: {s}\n", .{ c[0], why, c[1] });
            wrong += 1;
        }
    }
    try testing.expectEqual(@as(usize, 0), wrong);
}

test "secrets mounts: no read, listing, change, path or argument reaches a token, by the mount or another way; run's secrets takes one, scrubbed" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(t.threaded.io(), "keys");
    try t.put("keys/tok", "s3cret\n");
    try t.tmp.dir.symLink(t.threaded.io(), "keys", "alias", .{ .is_directory = true });
    try t.tmp.dir.symLink(t.threaded.io(), ".", "up", .{ .is_directory = true });
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
        // A link to a folder that holds it.
        .{ "run({\"shale-fixture\", \"lines\", \"1\", \"up\"}, {timeout_ms = 10000, tail = 5})", "`run`'s argument 4 holds the mount `k`'s folder" },
    }) |c| try t.expectStop(c[0], .atomic, .refused, c[1]);
    // An `env` value that is no string names no folder; the guard passes it
    // by, and `run`'s own reading of `env` refuses it.
    try t.expectStop("run({\"shale-fixture\", \"env\", \"F\"}, {timeout_ms = 10000, tail = 5, env = {F = 1}})", .atomic, .type, "");
    try testing.expectEqualStrings("s3cret\n", try t.file("keys/tok"));
    // `secrets` takes one, the value scrubbed; the mount itself is no secret.
    try t.expectOutput("print(run({\"shale-fixture\", \"env\", \"TOK\"}, {timeout_ms = 10000, tail = 5, secrets = {TOK = \"k:tok\"}}).out)", "<secret TOK>\n\n");
    try t.expectStop("run({\"shale-fixture\", \"env\", \"TOK\"}, {timeout_ms = 10000, tail = 5, secrets = {TOK = \"k:\"}})", .atomic, .argument, "a secret is a file in the mount, not the mount");
    // Names are not secrets: the folder above it lists.
    try t.expectOutput("print(contains(join(ls(\"\"), \",\"), \"keys\"))", "true\n");
    // Nor is a stop for a mount's missing folder a way to its folder.
    try folderStopsHideSecrets();
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
    // the result, the output nor the run's records (`ms` and `cpu_ms` set
    // aside: their digits would make the gate's engines differ in work).
    try t.expectOutput("local r = run({\"shale-fixture\", \"cat\"}, {timeout_ms = 10000, stdin = \"tok-7f3a9c\\n\", stdout_secret = \"k:new\"}) r.ms = 0 r.cpu_ms = 0 print(r.code, r.secret_written, r.secret_bytes, r.out, contains(json_write(r), \"7f3a9c\"))", "0\ttrue\t11\t\tfalse\n");
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
        .{ "stdout_secret = \"k:..\"", .argument, "climbs out of its mount with `..`" },
        .{ "stdout_secret = \"k:\"", .argument, "a secret is a file in the mount, not the mount" },
        .{ "stdout_secret = \"k:ln\", secret_replace = true", .refused, "is a symbolic link" },
        .{ "stdout_secret = \"k:x\", tail = 5", .argument, "takes no keep, filter or log" },
        .{ "stdout_secret = \"k:x\", whole = 100", .argument, "takes no keep, filter or log" },
        .{ "stdout_secret = \"k:x\", re = \"t\"", .argument, "takes no keep, filter or log" },
        .{ "stdout_secret = \"k:x\", log = \"out.log\"", .argument, "takes no keep, filter or log" },
        .{ "tail = 5, secret_replace = true", .argument, "`secret_replace` is `stdout_secret`'s" },
        .{ "stdout_secret = 3", .type, "`run`'s `stdout_secret` is a path in a `secrets` mount, `\"keys:NAME\"`, not " },
        .{ "stdout_secret = \"k:x\", secret_replace = 1", .type, "`run`: its `secret_replace` must be true or false, not " },
    }) |c| try t.expectStop(try std.fmt.allocPrint(a, "run({{\"shale-fixture\", \"both\", \"tok-x\", \"0\"}}, {{timeout_ms = 10000, {s}}})", .{c[0]}), .atomic, c[1], c[2]);
    try testing.expectError(error.FileNotFound, t.tmp.dir.statFile(io, "keys/x", .{}));

    // Reads stay refused; `secrets` hands the stored value to a program.
    try t.expectStop("read(\"k:new\")", .atomic, .refused, "the mount `k` holds secrets");
    try t.expectOutput("print(run({\"shale-fixture\", \"env\", \"X\"}, {timeout_ms = 10000, tail = 5, secrets = {X = \"k:new\"}}).out)", "<secret X>\n\n");
    // A secret is named by a path; a value of another type is refused.
    try t.expectStop("run({\"shale-fixture\", \"env\", \"X\"}, {timeout_ms = 10000, tail = 5, secrets = {X = 3}})", .atomic, .type, "");
}

/// One run outside the gate, which must stop with `class` and `says`.
fn onceStop(t: *T, src: []const u8, class: script_class, says: []const u8) !void {
    var r = t.runOnce(src, .atomic);
    defer r.deinit();
    const s = r.stop orelse return error.TestUnexpectedResult;
    errdefer std.debug.print("{s}: {s}: {s}\n", .{ src, @tagName(s.class), s.message });
    try testing.expectEqual(class, s.class);
    try testing.expect(std.mem.indexOf(u8, s.message, says) != null);
}

test "stdout_secret: a file made there while the program ran, a folder that refuses the rename, and one that cannot be looked at each stop the run, nothing stored" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const io = t.threaded.io();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(io, "keys");
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "k = {s}/keys secrets\n", .{t.root}), "/home/x", &why);
    const keys = try std.fmt.allocPrintSentinel(a, "{s}/keys", .{t.root}, 0);
    // The program makes the file itself (by a path its argv does not
    // spell, which the argv guard cannot see), so the rename finds it.
    try onceStop(&t, "run({\"sh\", \"-c\", \"printf tok; touch ke''ys/raced\"}, {timeout_ms = 10000, stdout_secret = \"k:raced\", want = \"test: a program that makes the secret's file\"})", .refused, "`run`'s `stdout_secret` k:raced was made while `sh");
    try testing.expectEqualStrings("", try t.file("keys/raced"));
    // The program takes the folder's write away, so the rename is refused.
    try onceStop(&t, "run({\"sh\", \"-c\", \"printf tok; chmod 500 ke''ys\"}, {timeout_ms = 10000, stdout_secret = \"k:locked\", want = \"test: a program that locks the folder\"})", .store, "`run`'s `stdout_secret` k:locked cannot be written: ACCES");
    try testing.expectEqual(@as(usize, 0), std.os.linux.chmod(keys, 0o700));
    try testing.expectError(error.FileNotFound, t.tmp.dir.statFile(io, "keys/locked", .{}));
    // A folder that cannot be searched: the target cannot be looked at.
    try testing.expectEqual(@as(usize, 0), std.os.linux.chmod(keys, 0o000));
    defer _ = std.os.linux.chmod(keys, 0o700);
    try onceStop(&t, "run({\"shale-fixture\", \"both\", \"tok\", \"0\"}, {timeout_ms = 10000, stdout_secret = \"k:hidden\"})", .store, "`run`'s `stdout_secret` k:hidden cannot be looked at: AccessDenied");
}

/// One run outside the gate, which must stop with `class` and say exactly
/// `says`, so a folder in the message cannot hide behind a substring.
fn onceSaid(t: *T, src: []const u8, class: script_class, says: []const u8) !void {
    var r = t.runOnce(src, .atomic);
    defer r.deinit();
    const s = r.stop orelse return error.TestUnexpectedResult;
    errdefer std.debug.print("{s}: {s}: {s}\n", .{ src, @tagName(s.class), s.message });
    try testing.expectEqual(class, s.class);
    try testing.expectEqualStrings(says, s.message);
}

/// A mount's folder missing or shut is said by the mount's name alone,
/// through `secrets` and `stdout_secret`; an ordinary mount's stop names its
/// folder, unless its path spells a secrets mount's. A part of the secrets
/// test below, so the C port's count of this file's tests holds.
fn folderStopsHideSecrets() !void {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const io = t.threaded.io();
    const a = t.arena.allocator();
    try t.tmp.dir.createDirPath(io, "shut/keys");
    try t.tmp.dir.createDirPath(io, "shut/plain");
    var why: []const u8 = "";
    t.host.mounts = try host_linux.parseMounts(a, try std.fmt.allocPrint(a, "k = {s}/no-keys secrets\np = {s}/no-plain\nks = {s}/shut/keys secrets\nps = {s}/shut/plain\nin = {s}/no-keys/sub\nnear = {s}/no-keys-old\nkin = {s}/shut/keys/sub\n", .{ t.root, t.root, t.root, t.root, t.root, t.root, t.root }), "/home/x", &why);
    const missing = "the mount `k` is a secrets mount whose folder is missing";
    try onceSaid(&t, "run({\"shale-fixture\", \"env\", \"X\"}, {timeout_ms = 10000, tail = 5, secrets = {X = \"k:tok\"}})", .store, missing);
    try onceSaid(&t, "run({\"shale-fixture\", \"both\", \"tok\", \"0\"}, {timeout_ms = 10000, stdout_secret = \"k:tok\"})", .store, missing);
    try onceSaid(&t, "read(\"p:x\")", .store, try std.fmt.allocPrint(a, "the mount `p` is {s}/no-plain, which is not a folder there", .{t.root}));
    // An ordinary mount whose path spells a secrets mount's folder, inside it
    // or only beginning with it, is not named either.
    try onceSaid(&t, "read(\"in:x\")", .store, "the mount `in`'s folder is missing; its path holds a secrets mount's folder, so it is not named");
    try onceSaid(&t, "read(\"near:x\")", .store, "the mount `near`'s folder is missing; its path holds a secrets mount's folder, so it is not named");
    // A folder that is there but cannot be opened: its parent shut.
    const shut = try std.fmt.allocPrintSentinel(a, "{s}/shut", .{t.root}, 0);
    try testing.expectEqual(@as(usize, 0), std.os.linux.chmod(shut, 0o000));
    defer _ = std.os.linux.chmod(shut, 0o700);
    const shut_said = "the mount `ks` is a secrets mount whose folder cannot be opened: AccessDenied";
    try onceSaid(&t, "run({\"shale-fixture\", \"env\", \"X\"}, {timeout_ms = 10000, tail = 5, secrets = {X = \"ks:tok\"}})", .store, shut_said);
    try onceSaid(&t, "run({\"shale-fixture\", \"both\", \"tok\", \"0\"}, {timeout_ms = 10000, stdout_secret = \"ks:tok\"})", .store, shut_said);
    try onceSaid(&t, "read(\"ps:x\")", .store, try std.fmt.allocPrint(a, "the mount `ps` is {s}/shut/plain, which cannot be opened: AccessDenied", .{t.root}));
    try onceSaid(&t, "read(\"kin:x\")", .store, "the mount `kin`'s folder cannot be opened: AccessDenied; its path holds a secrets mount's folder, so it is not named");
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
