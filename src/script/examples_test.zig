//! The example scripts, run against a store shaped like angry-gopher's
//! data (its zig-server/src chat_paths.zig, chat_store.zig, chat_retire.zig).

const std = @import("std");
const script = @import("script.zig");
const testing = std.testing;

const topics = @embedFile("examples/topics.shale");
const big_files = @embedFile("examples/big_files.shale");
const delete_topic = @embedFile("examples/delete_topic.shale");
const where_the_bytes_are = @embedFile("examples/where_the_bytes_are.shale");
const chat_topics = @embedFile("examples/chat_topics.shale");
const first_change = @embedFile("examples/first_change.shale");
const stale_caches = @embedFile("examples/stale_caches.shale");
const find_message = @embedFile("examples/find_message.shale");
const stale_topics = @embedFile("examples/stale_topics.shale");
const delete_stale_topics = @embedFile("examples/delete_stale_topics.shale");
const bad_utf8 = @embedFile("examples/bad_utf8.shale");

const sep = "\n\n-------------\n";

fn transcript(alloc: std.mem.Allocator, sid: []const u8, n: usize) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    for (1..n + 1) |i| {
        if (i > 1) try out.appendSlice(alloc, sep);
        try out.print(alloc, "MSG_{s}_{d}\nfrom: steve\ndate: 2026-10-13T12:00:00Z\n\nmessage {d}, a line of it\nand another", .{ sid, i, i });
    }
    return out.items;
}

const Fixture = struct {
    tmp: std.testing.TmpDir,
    threaded: std.Io.Threaded,
    arena: std.heap.ArenaAllocator,
    root: []const u8,

    fn init(self: *Fixture) !void {
        self.tmp = testing.tmpDir(.{});
        self.threaded = std.Io.Threaded.init(testing.allocator, .{});
        self.arena = std.heap.ArenaAllocator.init(testing.allocator);
        const a = self.arena.allocator();
        self.root = try std.fs.path.join(a, &.{ ".zig-cache", "tmp", &self.tmp.sub_path, "data" });
        const io = self.threaded.io();
        const d = self.tmp.dir;
        for ([_][]const u8{ "data/chat/1_2/sessions", "data/chat/channels/general/sessions/plans.uploads", "data/chat/channels/random/sessions", "data/games" }) |p| try d.createDirPath(io, p);
        const files = [_]struct { path: []const u8, sid: []const u8, n: usize }{
            .{ .path = "data/chat/1_2/sessions/hello.md", .sid = "hello", .n = 3 },
            .{ .path = "data/chat/channels/general/sessions/plans.md", .sid = "plans", .n = 40 },
            .{ .path = "data/chat/channels/general/sessions/lunch.md", .sid = "lunch", .n = 1 },
            .{ .path = "data/chat/channels/random/sessions/misc.md", .sid = "misc", .n = 2 },
        };
        for (files) |f| {
            const text = try transcript(a, f.sid, f.n);
            try d.writeFile(io, .{ .sub_path = f.path, .data = text });
            // The count sidecar as the site writes it (angry-gopher
            // chat_count.zig, writeCount): "<count> <size>", the transcript's
            // size when counted, then "<offset> <uid>": where its last
            // message's block begins, at the separator before it (0 for a
            // first message: chat_store.zig's `new_size - stored.len`), and
            // who sent it. misc has none, like a topic not yet read since the
            // server started.
            if (std.mem.eql(u8, f.sid, "misc")) continue;
            const last = if (f.n > 1) std.mem.lastIndexOf(u8, text, sep).? else 0;
            const sidecar = try std.fmt.allocPrint(a, "{d} {d}\n{d} 7\n", .{ f.n, text.len, last });
            try d.writeFile(io, .{ .sub_path = try std.fmt.allocPrint(a, "{s}.count", .{f.path[0 .. f.path.len - 3]}), .data = sidecar });
        }
        try d.writeFile(io, .{ .sub_path = "data/chat/channels/general/sessions/plans.reactions.jsonl", .data = "{}\n" });
        try d.writeFile(io, .{ .sub_path = "data/chat/channels/general/sessions/plans.uploads/1.png", .data = "png" });
        try d.writeFile(io, .{ .sub_path = "data/chat/channels/general.channel", .data = "1\n2\n" });
        try d.writeFile(io, .{ .sub_path = "data/games/big.json", .data = &@as([5000]u8, @splat('x')) });
    }

    fn deinit(self: *Fixture) void {
        self.arena.deinit();
        self.threaded.deinit();
        self.tmp.cleanup();
    }

    fn run(self: *Fixture, src: []const u8, out: []u8) script.Result {
        return self.runAt(src, 0, out);
    }

    fn runAt(self: *Fixture, src: []const u8, now: i64, out: []u8) script.Result {
        return script.run(self.threaded.io(), testing.allocator, self.root, src, .{}, now, out);
    }
};

var out_buf: [16 * 1024]u8 = undefined;

fn finished(r: *const script.Result) !void {
    if (r.stop) |s| {
        std.debug.print("stopped {d}:{d} {s}: {s}\n", .{ s.line, s.column, @tagName(s.class), s.message });
        return error.TestUnexpectedResult;
    }
}

test "example: every topic with its message count" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    var r = f.run(topics, &out_buf);
    defer r.deinit();
    try finished(&r);
    try testing.expectEqualStrings("1_2\thello\t3\n#general\tlunch\t1\n#general\tplans\t40\n#random\tmisc\t2\ntopics\t4\n", r.output);
    try testing.expectEqual(@as(usize, 0), r.effects.len);
}

test "example: files over a size" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    var r = f.run(big_files, &out_buf);
    defer r.deinit();
    try finished(&r);
    errdefer std.debug.print("{s}", .{r.output});
    try testing.expect(std.mem.indexOf(u8, r.output, "chat/channels/general/sessions/plans.md") != null);
    try testing.expect(std.mem.indexOf(u8, r.output, "5000\tgames/big.json") != null);
    try testing.expect(std.mem.indexOf(u8, r.output, "lunch.md") == null);
    try testing.expect(std.mem.endsWith(u8, r.output, "files over 1000 bytes:\t2\n"));
}

test "example: delete one topic, its sidecars and uploads with it, every change in the effects log" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    var r = f.run(delete_topic, &out_buf);
    defer r.deinit();
    try finished(&r);
    const want = [_][2][]const u8{
        .{ "remove", "chat/channels/general/sessions/plans.md" },
        .{ "remove", "chat/channels/general/sessions/plans.count" },
        .{ "remove", "chat/channels/general/sessions/plans.reactions.jsonl" },
        .{ "remove_tree", "chat/channels/general/sessions/plans.uploads" },
    };
    try testing.expectEqual(want.len, r.effects.len);
    for (r.effects, want) |e, w| {
        try testing.expectEqualStrings(w[0], e.op);
        try testing.expectEqualStrings(w[1], e.path);
        try testing.expectEqualStrings("ok", e.outcome);
    }
    // The other topics stay; the topics script now sees three.
    var again = f.run(topics, &out_buf);
    defer again.deinit();
    try finished(&again);
    try testing.expect(std.mem.endsWith(u8, again.output, "topics\t3\n"));
    // And run twice, the second finds nothing to delete, and says so.
    var twice = f.run(delete_topic, &out_buf);
    defer twice.deinit();
    try finished(&twice);
    try testing.expectEqual(@as(usize, 0), twice.effects.len);
    try testing.expect(std.mem.startsWith(u8, twice.output, "no such topic"));
}

test "example: where the bytes are, folder by folder, then the largest files" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    var r = f.run(where_the_bytes_are, &out_buf);
    defer r.deinit();
    try finished(&r);
    errdefer std.debug.print("{s}", .{r.output});
    try testing.expectEqual(@as(usize, 0), r.effects.len);
    // The folders, the biggest first, then all of them.
    try testing.expect(std.mem.startsWith(u8, r.output, "folder  files  bytes  size     empty\n"));
    try testing.expect(std.mem.indexOf(u8, r.output, "\nchat       10   4780  4.7 KiB      0\n") != null);
    try testing.expect(std.mem.indexOf(u8, r.output, "\ngames       1   5000  4.9 KiB      0\n") != null);
    try testing.expect(std.mem.indexOf(u8, r.output, "\nall        11   9780  9.6 KiB      0\n") != null);
    // The largest: games/big.json, then the 40-message transcript.
    try testing.expect(std.mem.indexOf(u8, r.output, "the largest files:\nbytes  size     path\n 5000  4.9 KiB  games/big.json\n") != null);
    try testing.expect(std.mem.indexOf(u8, r.output, "chat/channels/general/sessions/plans.md") != null);
}

test "example: chat topics from their count caches, the quiet ones, the caches stale or missing, and no transcript read" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    // A cache taken before the last messages: stale, as the site's can be.
    try f.tmp.dir.writeFile(f.threaded.io(), .{ .sub_path = "data/chat/channels/general/sessions/plans.count", .data = "39 4000\n3900 7\n" });
    // Far ahead of the fixture's files' times: every topic is quiet.
    var r = f.runAt(chat_topics, 253402300000, &out_buf);
    defer r.deinit();
    try finished(&r);
    errdefer std.debug.print("{s}", .{r.output});
    try testing.expectEqual(@as(usize, 0), r.effects.len);
    // Counts from the caches: 3 + 39 + 1, and misc has none.
    try testing.expect(std.mem.startsWith(u8, r.output, "topics\t4\tmessages\t43\tbytes\t4.6 KiB\n"));
    try testing.expect(std.mem.indexOf(u8, r.output, "topic           messages  size     changed\n#general plans        39  4.1 KiB  ") != null);
    try testing.expect(std.mem.indexOf(u8, r.output, "unchanged since 9999-10-02T23:46:40Z): 4\n") != null);
    try testing.expect(std.mem.indexOf(u8, r.output, "): 4\n") != null);
    try testing.expect(std.mem.indexOf(u8, r.output, "count caches that are stale or missing (the server recounts them): 2\ntopic           bytes  cache\n#general plans   4166  39 at 4000\n#random misc      188  none\n") != null);
    // No transcript is read: only the walk's folders and the caches.
    try testing.expect(r.steps < 20_000);
    // At the fixture's date, the files (written then or later) are not quiet.
    var today = f.runAt(chat_topics, 1791936000, &out_buf);
    defer today.deinit();
    try finished(&today);
    try testing.expect(std.mem.indexOf(u8, today.output, "): 0\n") != null);
}

test "example: the first change makes one folder and one file, and says what the last run wrote" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    var r = f.runAt(first_change, 1791936000, &out_buf);
    defer r.deinit();
    try finished(&r);
    errdefer std.debug.print("{s}", .{r.output});
    try testing.expectEqual(@as(usize, 2), r.effects.len);
    try testing.expectEqualStrings("mkdir", r.effects[0].op);
    try testing.expectEqualStrings("shale-check", r.effects[0].path);
    try testing.expectEqualStrings("replace", r.effects[1].op);
    try testing.expectEqualStrings("shale-check/first-run.txt", r.effects[1].path);
    try testing.expect(std.mem.startsWith(u8, r.output, "no shale-check/first-run.txt yet\n"));
    try testing.expect(std.mem.indexOf(u8, r.output, "at 2026-10-14T00:00:00Z\nconversations 1, channels 2\n") != null);
    // Again: the folder is there, only the file is replaced, and the
    // last run's text is printed first.
    var again = f.runAt(first_change, 1791936060, &out_buf);
    defer again.deinit();
    try finished(&again);
    try testing.expectEqual(@as(usize, 1), again.effects.len);
    try testing.expect(std.mem.startsWith(u8, again.output, "the last run wrote:\nwritten by a signed Shale script at 2026-10-14T00:00:00Z\n"));
}

test "example: every count cache checked against its transcript's own separators, the stale and missing named, none read whole" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    // A cache taken before the last message, as a crash can leave it.
    try f.tmp.dir.writeFile(f.threaded.io(), .{ .sub_path = "data/chat/channels/general/sessions/plans.count", .data = "39 4000\n3900 7\n" });
    var r = f.run(stale_caches, &out_buf);
    defer r.deinit();
    try finished(&r);
    errdefer std.debug.print("{s}", .{r.output});
    try testing.expectEqualStrings("topics checked\t4\tstale or missing\t2\n" ++
        \\topic           cache  counted  verdict
        \\#general lunch      1        1  ok
        \\#general plans     39       40  stale
        \\#random misc       -1        2  no cache
        \\1_2 hello           3        3  ok
        \\
    , r.output);
    // Every transcript scanned, none held: the arena stays small.
    try testing.expect(r.arena_bytes < 512 * 1024);
}

test "example: a phrase found in the chat, each topic's first message with it, and the run stops at its read budget saying how far it got" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    var r = f.run(find_message, &out_buf);
    defer r.deinit();
    try finished(&r);
    errdefer std.debug.print("{s}", .{r.output});
    try testing.expect(std.mem.startsWith(u8, r.output, "\"line of it\" in 4 of 4 topics\n"));
    try testing.expect(std.mem.indexOf(u8, r.output, "#general plans  MSG_plans_1  steve  2026-10-13T12:00:00Z") != null);
    // With a read budget for about one topic: it stops, and says so.
    var short = script.run(f.threaded.io(), testing.allocator, f.root, find_message, .{ .read_bytes = 3000 }, 0, &out_buf);
    defer short.deinit();
    try finished(&short);
    try testing.expect(std.mem.indexOf(u8, short.output, "stopped at the read budget, after ") != null);
}

/// The budgets the live route gives a signed script (angry-gopher
/// `zig-server/src/admin_script.zig`, `budgets`).
const site_budgets: script.Budgets = .{
    .steps = 2_000_000,
    .arena_bytes = 8 * 1024 * 1024,
    .reads = 500,
    .read_bytes = 16 * 1024 * 1024,
    .changes = 100,
};

test "every read-only script of the cookbook finishes on the site-shaped store inside the site's own budgets, changing nothing" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    const read_only = [_]struct { name: []const u8, src: []const u8 }{
        .{ .name = "topics", .src = topics },
        .{ .name = "big_files", .src = big_files },
        .{ .name = "where_the_bytes_are", .src = where_the_bytes_are },
        .{ .name = "chat_topics", .src = chat_topics },
        .{ .name = "stale_caches", .src = stale_caches },
        .{ .name = "find_message", .src = find_message },
        .{ .name = "bad_utf8", .src = bad_utf8 },
    };
    for (read_only) |ex| {
        var r = script.run(f.threaded.io(), testing.allocator, f.root, ex.src, site_budgets, 1791936000, &out_buf);
        defer r.deinit();
        errdefer std.debug.print("{s}\n", .{ex.name});
        try finished(&r);
        try testing.expectEqual(@as(usize, 0), r.effects.len);
    }
}

test "every cookbook script that walks gives the same answer walking in parts of three entries as in parts of 256" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    var whole_buf: [64 * 1024]u8 = undefined;
    for ([_][]const u8{ where_the_bytes_are, chat_topics, stale_caches, find_message, bad_utf8 }) |src| {
        var whole = script.run(f.threaded.io(), testing.allocator, f.root, src, .{}, 1791936000, &whole_buf);
        defer whole.deinit();
        try finished(&whole);
        var parts = script.run(f.threaded.io(), testing.allocator, f.root, src, .{ .call_entries = 3 }, 1791936000, &out_buf);
        defer parts.deinit();
        try finished(&parts);
        try testing.expectEqualStrings(whole.output, parts.output);
    }
}

/// The fixture's files are written as the test runs, so "now" is the clock's,
/// and 40 days on is past every topic's 30.
fn clockNow(f: *Fixture) i64 {
    return @intCast(@divFloor(std.Io.Clock.now(.real, f.threaded.io()).nanoseconds, std.time.ns_per_s));
}

test "example: the topics untouched for 30 days, none when all are fresh" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    var fresh = f.runAt(stale_topics, clockNow(&f), &out_buf);
    defer fresh.deinit();
    try finished(&fresh);
    try testing.expectEqualStrings("topics\t0\n", fresh.output);
    var later = f.runAt(stale_topics, clockNow(&f) + 40 * 86400, &out_buf);
    defer later.deinit();
    try finished(&later);
    errdefer std.debug.print("{s}", .{later.output});
    for ([_][]const u8{ "1_2\thello\t3\t", "#general\tlunch\t1\t", "#general\tplans\t40\t", "#random\tmisc\t2\t" }) |row|
        try testing.expect(std.mem.indexOf(u8, later.output, row) != null);
    try testing.expect(std.mem.endsWith(u8, later.output, "topics\t4\n"));
    try testing.expectEqual(@as(usize, 0), later.effects.len);
}

test "example: stale topics deleted with their sidecars and uploads; a fresh one, a missing one or none given removes nothing" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const given = "const TOPICS = split(arg(\"topics\", \"\"), \",\")";
    try testing.expect(std.mem.indexOf(u8, delete_stale_topics, given) != null);
    const two = try std.mem.replaceOwned(u8, a, delete_stale_topics, given, "const TOPICS = split(arg(\"topics\", \"chat/channels/general/sessions/plans,chat/1_2/sessions/hello\"), \",\")");
    const missing = try std.mem.replaceOwned(u8, a, delete_stale_topics, given, "const TOPICS = split(arg(\"topics\", \"chat/1_2/sessions/hello,chat/1_2/sessions/nope\"), \",\")");
    {
        var f: Fixture = undefined;
        try f.init();
        defer f.deinit();
        var r = f.runAt(two, clockNow(&f) + 40 * 86400, &out_buf);
        defer r.deinit();
        try finished(&r);
        try testing.expectEqualStrings("deleted\tchat/channels/general/sessions/plans\t.md .count .reactions.jsonl .uploads/\ndeleted\tchat/1_2/sessions/hello\t.md .count\ntopics deleted\t2\n", r.output);
        try testing.expectEqual(@as(usize, 6), r.effects.len);
    }
    for ([_]struct { src: []const u8, ahead: i64 }{ .{ .src = two, .ahead = 0 }, .{ .src = missing, .ahead = 40 * 86400 }, .{ .src = delete_stale_topics, .ahead = 40 * 86400 } }) |c| {
        var f: Fixture = undefined;
        try f.init();
        defer f.deinit();
        var r = f.runAt(c.src, clockNow(&f) + c.ahead, &out_buf);
        defer r.deinit();
        try testing.expectEqualStrings("fail", @tagName(r.stop.?.class));
        try testing.expectEqual(@as(usize, 0), r.effects.len);
    }
}

/// Silences `complain`. A passing run prints nothing that says "failed": the
/// planted cases below expect complaints, so they set this, and complaints
/// are printed only when a real doc check fails.
var quiet = false;

fn complain(comptime fmt: []const u8, args: anytype) void {
    if (!quiet) std.debug.print(fmt, args);
}

/// One ```shale block of the reference and what it must do: the ```output
/// block after it (blank lines between) holds exactly what it prints, tabs as
/// tabs; a block that stops ends instead with the line `-- stops: <class>`.
pub const Snippet = struct { line: usize, source: []const u8, prints: ?[]const u8, stops: ?[]const u8 };

pub fn snippets(a: std.mem.Allocator, name: []const u8, doc: []const u8) ![]Snippet {
    var out: std.ArrayList(Snippet) = .empty;
    var lines = std.mem.splitScalar(u8, doc, '\n');
    var n: usize = 0;
    var in_block: ?[]const u8 = null;
    var start: usize = 0;
    var body: std.ArrayList(u8) = .empty;
    var prev_blank = true;
    // A shale block that prints, until its output block is read.
    var awaiting: ?Snippet = null;
    // The language of the block before, so an output block after another
    // language's (`shale-linux`, read by its own test) is that block's.
    var before: []const u8 = "";
    while (lines.next()) |line| {
        n += 1;
        if (in_block) |lang| {
            const kept = std.mem.eql(u8, lang, "shale") or std.mem.eql(u8, lang, "output");
            if (std.mem.eql(u8, line, "```")) {
                in_block = null;
                if (std.mem.eql(u8, lang, "shale")) {
                    const sn = try parseSnippet(name, start, body.items);
                    if (sn.stops != null) try out.append(a, sn) else awaiting = sn;
                } else if (std.mem.eql(u8, lang, "output")) {
                    if (awaiting) |pending| {
                        var sn = pending;
                        sn.prints = body.items;
                        try out.append(a, sn);
                        awaiting = null;
                    } else if (before.len == 0 or std.mem.eql(u8, before, "shale") or std.mem.eql(u8, before, "output")) {
                        complain("{s}:{d}: an output block with no program block before it\n", .{ name, start });
                        return error.TestUnexpectedResult;
                    }
                }
                before = lang;
                body = .empty;
            } else if (kept) {
                try body.appendSlice(a, line);
                try body.append(a, '\n');
            }
            continue;
        }
        if (awaiting) |sn| if (std.mem.trim(u8, line, " ").len != 0 and !std.mem.eql(u8, line, "```output")) {
            complain("{s}:{d}: a shale block with no output block after it and no `-- stops:`: say what it does\n", .{ name, sn.line });
            return error.TestUnexpectedResult;
        };
        if (std.mem.startsWith(u8, line, "```")) {
            const lang = line[3..];
            if (lang.len == 0) {
                complain("{s}:{d}: a code block with no language: say what it is (shale, zig, sh, ebnf)\n", .{ name, n });
                return error.TestUnexpectedResult;
            }
            in_block = lang;
            start = n;
            continue;
        }
        // A fence indented (inside a list) is never read as one here, so
        // its block would never run: fences start their line.
        if (std.mem.startsWith(u8, std.mem.trimStart(u8, line, " "), "```")) {
            complain("{s}:{d}: an indented fence: start it at the line's start, outside the list\n", .{ name, n });
            return error.TestUnexpectedResult;
        }
        // An indented code block would never run: every block is fenced.
        if (prev_blank and std.mem.startsWith(u8, line, "    ") and !std.mem.startsWith(u8, std.mem.trimStart(u8, line, " "), "-")) {
            complain("{s}:{d}: an indented code block: fence it, with its language\n", .{ name, n });
            return error.TestUnexpectedResult;
        }
        prev_blank = std.mem.trim(u8, line, " ").len == 0;
    }
    if (awaiting) |sn| {
        complain("{s}:{d}: a shale block with no output block after it and no `-- stops:`: say what it does\n", .{ name, sn.line });
        return error.TestUnexpectedResult;
    }
    return out.items;
}

fn parseSnippet(name: []const u8, line: usize, text: []const u8) !Snippet {
    if (std.mem.indexOf(u8, text, "\n-- stops: ")) |at| {
        const class = std.mem.trim(u8, text[at + "\n-- stops: ".len ..], " \n");
        return .{ .line = line, .source = text[0 .. at + 1], .prints = null, .stops = class };
    }
    if (std.mem.indexOf(u8, text, "-- prints:") != null) {
        complain("{s}:{d}: `-- prints:` is the old form: the output goes in an ```output block after the shale block\n", .{ name, line });
        return error.TestUnexpectedResult;
    }
    return .{ .line = line, .source = text, .prints = null, .stops = null };
}

/// Runs every ```shale block of the reference on the site-shaped store at the
/// fixture's date.
fn checkDoc(doc: []const u8) !usize {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const all = try snippets(a, "docs/SHALE.md", doc);
    var wrong: usize = 0;
    for (all) |sn| {
        var f: Fixture = undefined;
        try f.init();
        defer f.deinit();
        var r = f.runAt(sn.source, 1791936000, &out_buf);
        defer r.deinit();
        if (sn.stops) |class| {
            const s = r.stop orelse {
                complain("docs/SHALE.md:{d}: finished, and should stop with {s}\n", .{ sn.line, class });
                return error.TestUnexpectedResult;
            };
            if (!std.mem.eql(u8, @tagName(s.class), class)) {
                complain("docs/SHALE.md:{d}: stopped {s} ({s}), not {s}\n", .{ sn.line, @tagName(s.class), s.message, class });
                return error.TestUnexpectedResult;
            }
            continue;
        }
        if (r.stop) |s| {
            complain("docs/SHALE.md:{d}: stopped {d}:{d} {s}: {s}\n", .{ sn.line, s.line, s.column, @tagName(s.class), s.message });
            return error.TestUnexpectedResult;
        }
        if (!std.mem.eql(u8, r.output, sn.prints.?)) {
            // Report every block that differs, so a doc is fixed in one pass.
            complain("docs/SHALE.md:{d}: prints\n{s}-- not --\n{s}", .{ sn.line, r.output, sn.prints.? });
            wrong += 1;
        }
    }
    if (wrong > 0) return error.TestUnexpectedResult;
    return all.len;
}

test "docs/SHALE.md: every shale block runs on the site-shaped store and prints what the doc says it prints; no block goes unchecked" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const doc = try std.Io.Dir.cwd().readFileAlloc(threaded.io(), "docs/SHALE.md", testing.allocator, .limited(1 << 20));
    defer testing.allocator.free(doc);
    try testing.expect(try checkDoc(doc) >= 8);
}

test "docs/SHALE.md's check fires: a wrong output, no output block, text between a block and its output, an output block alone, the old `-- prints:`, an unfenced block, a block with no language, a fence indented in a list" {
    const planted = [_][]const u8{
        "```shale\nprint(1 + 1)\n```\n\n```output\n3\n```\n",
        "```shale\nprint(1)\n```\n",
        "```shale\nprint(1)\n```\nprints this:\n```output\n1\n```\n",
        "```output\n1\n```\n",
        "```shale\nprint(1)\n-- prints:\n-- 1\n```\n",
        "text\n\n    print(1)\n",
        "```\nprint(1)\n```\n",
        "```shale\nprint(1)\n-- stops: type\n```\n",
        "- an item\n\n  ```shale\n  print(1)\n  ```\n",
    };
    quiet = true;
    defer quiet = false;
    for (planted) |p| try testing.expectError(error.TestUnexpectedResult, checkDoc(p));
    try testing.expectEqual(@as(usize, 1), try checkDoc("```shale\nprint(1, 2)\n```\n\n```output\n1\t2\n```\n"));
    try testing.expectEqual(@as(usize, 1), try checkDoc("```shale\nlocal x = 1\n```\n\n```output\n```\n"));
}

/// `src` with its `const START = ""` set to `start`.
fn startingAt(a: std.mem.Allocator, src: []const u8, start: []const u8) ![]u8 {
    const line = "const START = \"\"";
    const at = std.mem.indexOf(u8, src, line) orelse return error.NoStart;
    return std.fmt.allocPrint(a, "{s}const START = \"{s}\"{s}", .{ src[0..at], start, src[at + line.len ..] });
}

/// Returns what follows "resume after " on its line, or null.
fn resumeAfter(output: []const u8) ?[]const u8 {
    const key = "resume after ";
    const at = std.mem.indexOf(u8, output, key) orelse return null;
    const rest = output[at + key.len ..];
    return rest[0 .. std.mem.indexOfScalar(u8, rest, '\n') orelse rest.len];
}

/// Parses the `all` row's files and bytes, as `where_the_bytes_are` prints it.
fn allRow(output: []const u8) ![2]u64 {
    const at = std.mem.indexOf(u8, output, "\nall ") orelse return error.NoAllRow;
    var it = std.mem.tokenizeScalar(u8, output[at + 5 ..], ' ');
    return .{ try std.fmt.parseInt(u64, it.next().?, 10), try std.fmt.parseInt(u64, it.next().?, 10) };
}

test "example: where the bytes are, past what the site's reads walk, prints what it found and where to resume, and the rest from there completes the totals" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    const a = f.arena.allocator();
    const io = f.threaded.io();
    // 600 files in 20 folders: more than 500 reads can stat.
    for (0..20) |d| {
        try f.tmp.dir.createDirPath(io, try std.fmt.allocPrint(a, "data/bulk/d{d:0>2}", .{d}));
        for (0..30) |k| try f.tmp.dir.writeFile(io, .{ .sub_path = try std.fmt.allocPrint(a, "data/bulk/d{d:0>2}/f{d:0>2}", .{ d, k }), .data = &@as([10]u8, @splat('x')) });
    }
    var whole_buf: [64 * 1024]u8 = undefined;
    var whole = script.run(io, testing.allocator, f.root, where_the_bytes_are, .{}, 1791936000, &whole_buf);
    defer whole.deinit();
    try finished(&whole);
    try testing.expectEqual(@as(?[]const u8, null), resumeAfter(whole.output));
    const total = try allRow(whole.output);

    var first = script.run(io, testing.allocator, f.root, where_the_bytes_are, site_budgets, 1791936000, &out_buf);
    defer first.deinit();
    errdefer std.debug.print("{s}\n", .{first.output});
    try finished(&first);
    const resume_at = resumeAfter(first.output) orelse return error.NoResumeLine;
    const part = try allRow(first.output);
    try testing.expect(part[0] > 0 and part[0] < total[0]);

    var second_buf: [64 * 1024]u8 = undefined;
    var second = script.run(io, testing.allocator, f.root, try startingAt(a, where_the_bytes_are, resume_at), site_budgets, 1791936000, &second_buf);
    defer second.deinit();
    try finished(&second);
    try testing.expectEqual(@as(?[]const u8, null), resumeAfter(second.output));
    const rest = try allRow(second.output);
    try testing.expectEqual(total[0], part[0] + rest[0]);
    try testing.expectEqual(total[1], part[1] + rest[1]);
}

/// Returns the integer after `key` (and a tab or a space) in `output`.
fn numberAfter(output: []const u8, key: []const u8) !u64 {
    const at = std.mem.indexOf(u8, output, key) orelse return error.NoKey;
    var rest = output[at + key.len ..];
    while (rest.len > 0 and (rest[0] == '\t' or rest[0] == ' ')) rest = rest[1..];
    var n: usize = 0;
    while (n < rest.len and std.ascii.isDigit(rest[n])) n += 1;
    return std.fmt.parseInt(u64, rest[0..n], 10);
}

/// The site's budgets, with fewer reads: a fixture of a size a test can
/// afford still runs past them.
const parts_budgets: script.Budgets = blk: {
    var b = site_budgets;
    b.reads = 200;
    break :blk b;
};

test "every cookbook script that walks, past what the site's reads reach, goes on from where it says and adds up to the whole run" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    const a = f.arena.allocator();
    const io = f.threaded.io();
    // 130 topics in one channel, each with its count cache, and 130 big
    // files beside them: more reads than `parts_budgets` allows, so every
    // script resumes, and 260 entries in the sessions folder, past
    // `call_entries`' 256, so a walk gives its `more` part too. **TRIMMED,
    // STILL RED** (QUEUE 252): 300 of each under the site's 500 reads was
    // 10 s; a walk resumed one entry late (walkResume's `top.at = i + 2`,
    // clamped to the folder) fails it: the parts add to 132, the whole 131.
    try f.tmp.dir.createDirPath(io, "data/chat/channels/bulk/sessions");
    try f.tmp.dir.createDirPath(io, "data/bulk");
    for (0..130) |k| {
        const sid = try std.fmt.allocPrint(a, "t{d:0>3}", .{k});
        const text = try transcript(a, sid, 2);
        try f.tmp.dir.writeFile(io, .{ .sub_path = try std.fmt.allocPrint(a, "data/chat/channels/bulk/sessions/{s}.md", .{sid}), .data = text });
        try f.tmp.dir.writeFile(io, .{ .sub_path = try std.fmt.allocPrint(a, "data/chat/channels/bulk/sessions/{s}.count", .{sid}), .data = try std.fmt.allocPrint(a, "2 {d}\n0 7\n", .{text.len}) });
        try f.tmp.dir.writeFile(io, .{ .sub_path = try std.fmt.allocPrint(a, "data/bulk/b{d:0>3}", .{k}), .data = &@as([1200]u8, @splat('y')) });
    }
    const cases = [_]struct { name: []const u8, src: []const u8, key: []const u8 }{
        .{ .name = "big_files", .src = big_files, .key = "files over 1000 bytes:" },
        .{ .name = "chat_topics", .src = chat_topics, .key = "topics" },
        .{ .name = "stale_caches", .src = stale_caches, .key = "topics checked" },
        .{ .name = "find_message", .src = find_message, .key = "\" in" },
    };
    var whole_buf: [256 * 1024]u8 = undefined;
    for (cases) |c| {
        errdefer std.debug.print("{s}\n", .{c.name});
        var whole = script.run(io, testing.allocator, f.root, c.src, .{}, 1791936000, &whole_buf);
        defer whole.deinit();
        try finished(&whole);
        try testing.expectEqual(@as(?[]const u8, null), resumeAfter(whole.output));
        const total = try numberAfter(whole.output, c.key);
        var sum: u64 = 0;
        var start: []const u8 = "";
        var runs: usize = 0;
        while (true) : (runs += 1) {
            if (runs == 20) return error.NeverEnds;
            var r = script.run(io, testing.allocator, f.root, try startingAt(a, c.src, start), parts_budgets, 1791936000, &whole_buf);
            defer r.deinit();
            errdefer std.debug.print("run {d} from \"{s}\":\n{s}\n", .{ runs, start, r.output });
            try finished(&r);
            sum += try numberAfter(r.output, c.key);
            const next = resumeAfter(r.output) orelse break;
            try testing.expect(!std.mem.eql(u8, next, start));
            start = try a.dupe(u8, next);
        }
        try testing.expect(runs > 0);
        try testing.expectEqual(total, sum);
    }
}

test "example: a count cache torn into words, or into nothing, is read as no cache: chat_topics and stale_caches report it and go on" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    const io = f.threaded.io();
    try f.tmp.dir.writeFile(io, .{ .sub_path = "data/chat/channels/general/sessions/plans.count", .data = "log-x 4000\n" });
    try f.tmp.dir.writeFile(io, .{ .sub_path = "data/chat/1_2/sessions/hello.count", .data = "\n" });
    for ([_][]const u8{ chat_topics, stale_caches }) |src| {
        var r = f.runAt(src, 1791936000, &out_buf);
        defer r.deinit();
        errdefer std.debug.print("{s}\n", .{r.output});
        try finished(&r);
        try testing.expect(std.mem.indexOf(u8, r.output, "plans") != null);
    }
}
