//! Tests `zig_move` through the gate and zig-ast, on fixtures and on a
//! historical webbox split. The pure mover's tests, over fixtures and over the
//! repo's own files there and back, and that a rename and `uses_left` agree,
//! are zig-ast's (tools/zig_ast/zig_ast_test.zig).

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const webbox_root = @import("shale_test_options").webbox_root;
const host_linux = @import("shale_host_linux.zig");

fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{host_linux.linux_prelude};
    // As the CLI prints: a change is one line.
    t.diffs = .summary;
}

/// A move's report, a row each: `list name line`.
const report =
    \\function report(r)
    \\  print("moved", r.moved)
    \\  for k in {"needs_pub", "needs_import", "needs_from_from", "uses_left"} do
    \\    for x in r[k] do print(k, x.name, x.line, get(x, "target", "")) end
    \\  end
    \\end
    \\
;

const fixture =
    \\const std = @import("std");
    \\const Mem = std.mem;
    \\
    \\const a = 1;
    \\
    \\/// doc of X
    \\const X = struct {
    \\    y: u8,
    \\    fn hidden() void {}
    \\    pub fn shown() void {}
    \\};
    \\
    \\fn helper() void {}
    \\
    \\// plain, above w
    \\pub fn w() void {
    \\    _ = std.mem;
    \\    _ = Mem;
    \\    helper();
    \\    _ = a;
    \\}
    \\
    \\const b = X.hidden;
    \\const c = X.shown;
    \\test "x" {
    \\    _ = X{ .y = 1 };
    \\}
    \\
;

test "zig_move: cut and put, its doc comment with it, and what it broke: pub, imports, from's own, uses left" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("from.zig", fixture);
    try t.expectOutput(
        report ++ "report(zig_move(\"from.zig\", \"to.zig\", {\"X\", \"w\"}))",
        "edit to.zig: 1 hunk, -1 +14 lines\n" ++
            "edit from.zig: 2 hunks, -16 +2 lines\n" ++
            "moved\t2\n" ++
            "needs_pub\tX\t7\t\n" ++
            "needs_pub\tX.hidden\t23\t\n" ++
            "needs_import\tstd\t17\t@import(\"std\")\n" ++
            "needs_import\tMem\t18\tstd.mem\n" ++
            "needs_from_from\thelper\t19\t\n" ++
            "needs_from_from\ta\t20\t\n" ++
            "uses_left\tX\t23\t\n" ++
            "uses_left\tX\t24\t\n" ++
            "uses_left\tX\t26\t\n",
    );
    try testing.expectEqualStrings(
        \\const std = @import("std");
        \\const Mem = std.mem;
        \\
        \\const a = 1;
        \\
        \\fn helper() void {}
        \\
        \\// plain, above w
        \\const b = X.hidden;
        \\const c = X.shown;
        \\test "x" {
        \\    _ = X{ .y = 1 };
        \\}
        \\
    , try t.file("from.zig"));
    try testing.expectEqualStrings(
        \\/// doc of X
        \\const X = struct {
        \\    y: u8,
        \\    fn hidden() void {}
        \\    pub fn shown() void {}
        \\};
        \\
        \\pub fn w() void {
        \\    _ = std.mem;
        \\    _ = Mem;
        \\    helper();
        \\    _ = a;
        \\}
        \\
    , try t.file("to.zig"));
}

test "zig_move: doc_and_plain takes the comment above; after puts it after a declaration; the last one leaves one newline" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("from.zig", fixture);
    try t.put("to.zig", "const one = 1;\n\nconst two = 2;\n");
    try t.expectOutput("local r = zig_move(\"from.zig\", \"to.zig\", {\"w\"}, {comment = \"doc_and_plain\", after = \"one\"}) print(r.moved)", "edit to.zig: 1 hunk, -1 +9 lines\nedit from.zig: 1 hunk, -9 +1 lines\n1\n");
    try testing.expectEqualStrings("const one = 1;\n\n// plain, above w\npub fn w() void {\n    _ = std.mem;\n    _ = Mem;\n    helper();\n    _ = a;\n}\n\nconst two = 2;\n", try t.file("to.zig"));
    try testing.expect(std.mem.indexOf(u8, try t.file("from.zig"), "plain, above w") == null);
    // The file's last declaration: one newline at its end, after what is left.
    try t.put("end.zig", "const p = 1;\n\nconst q = 2;\n");
    try t.expectOutput("local r = zig_move(\"end.zig\", \"q.zig\", {\"q\"}) print(r.moved)", "edit q.zig: 1 hunk, -1 +2 lines\nedit end.zig: 1 hunk, -3 +1 lines\n1\n");
    try testing.expectEqualStrings("const p = 1;\n", try t.file("end.zig"));
    try testing.expectEqualStrings("const q = 2;\n", try t.file("q.zig"));
}

test "zig_move with from_as: what the moved code uses of `from` reached through it, those names `pub` in `from`, the import in `to`" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("g.zig", "const std = @import(\"std\");\nvar served: u64 = 0;\n/// Open now.\nconst open_now = 1;\npub const shown = 2;\n\nfn facts() u64 {\n    return served + open_now + shown;\n}\n");
    try t.expectOutput("local r = zig_move(\"g.zig\", \"f.zig\", {\"facts\"}, {from_as = \"gopher\"}) print(r.moved, r.qualified, join(r.made_pub, \",\"))", "edit f.zig: 1 hunk, -1 +4 lines\nedit g.zig: 1 hunk, -5 +1 lines\nedit f.zig: 1 hunk, -1 +1 lines\nedit f.zig: 1 hunk, -1 +3 lines\nedit g.zig: 2 hunks, -2 +2 lines\n1\t3\topen_now,served\n");
    try testing.expectEqualStrings("const gopher = @import(\"g.zig\");\n\nfn facts() u64 {\n    return gopher.served + gopher.open_now + gopher.shown;\n}\n", try t.file("f.zig"));
    try testing.expectEqualStrings("const std = @import(\"std\");\npub var served: u64 = 0;\n/// Open now.\npub const open_now = 1;\npub const shown = 2;\n", try t.file("g.zig"));
    // A `from_as` the moved code already names is refused, nothing written.
    try t.put("h.zig", "var x: u8 = 0;\nfn gopher() u8 {\n    return x;\n}\nfn y() u8 {\n    return gopher() + x;\n}\n");
    try t.expectStop("zig_move(\"h.zig\", \"i.zig\", {\"y\"}, {from_as = \"gopher\"})", .atomic, .argument, "`gopher` is a name in the moved code");
    try testing.expectError(error.FileNotFound, t.file("i.zig"));
}

test "zig_move: with the change budget room for one of its two writes, it writes neither" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.put("from.zig", "const a = 1;\n\nconst b = 2;\n");
    // 999 of the 1,000 changes spent: one write left, and a move is two.
    try t.expectStop("for i = 1, 999 do replace(\"spent.txt\", \"x\") end zig_move(\"from.zig\", \"to.zig\", {\"b\"})", .atomic, .budget, "change budget");
    try testing.expectEqualStrings("const a = 1;\n\nconst b = 2;\n", try t.file("from.zig"));
    try testing.expectError(error.FileNotFound, t.file("to.zig"));
}

test "zig_move: what stops it, with nothing written: a name not there, a name that is two, a name twice, an unnamed one, an `after` not there" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const two = "const foo = 1;\n\ntest foo {}\n\ntest {}\n";
    try t.put("f.zig", two);
    try t.put("g.zig", "const g = 1;\n");
    const cases = [_]struct { []const u8, []const u8 }{
        .{ "{\"nope\"}", "`nope` is not a top-level declaration of `from`" },
        .{ "{\"foo\"}", "`foo` names 2 declarations of `from`: say which" },
        .{ "{{kind = \"const\", name = \"foo\"}, {kind = \"const\", name = \"foo\"}}", "`foo` is named twice" },
        .{ "{\"\"}", "cannot be named, so it cannot be moved" },
        .{ "{{kind = \"fn\", name = \"foo\"}}", "`foo` of that kind is not" },
    };
    for (cases) |c| {
        try t.expectStop(try std.fmt.allocPrint(t.arena.allocator(), "zig_move(\"f.zig\", \"g.zig\", {s})", .{c[0]}), .atomic, .argument, c[1]);
        try testing.expectEqualStrings(two, try t.file("f.zig"));
        try testing.expectEqualStrings("const g = 1;\n", try t.file("g.zig"));
    }
    try t.expectStop("zig_move(\"f.zig\", \"g.zig\", {{kind = \"const\", name = \"foo\"}}, {after = \"h\"})", .atomic, .argument, "`after`: `h` is not");
    try t.expectStop("zig_move(\"f.zig\", \"f.zig\", {\"foo\"})", .atomic, .argument, "one file");
    try t.put("bad.zig", "const = ;\n");
    try t.expectStop("zig_move(\"bad.zig\", \"g.zig\", {\"x\"})", .atomic, .argument, "does not parse as Zig: line 1");
    // By kind, a name that is two moves the one named.
    try t.expectOutput("print(zig_move(\"f.zig\", \"g.zig\", {{kind = \"test\", name = \"foo\"}}).moved)", "edit g.zig: 1 hunk, -1 +3 lines\nedit f.zig: 1 hunk, -3 +1 lines\n1\n");
    try testing.expectEqualStrings("const foo = 1;\n\ntest {}\n", try t.file("f.zig"));
}

test "zig_move: a use without a call counts; the known limits, pinned: a method through a value, a name through an @This() alias" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    // angry-gopher router.zig's shape: a function given, not called.
    try t.put("u.zig",
        \\const store = @import("store.zig");
        \\
        \\fn scriptDurable() bool {
        \\    return true;
        \\}
        \\
        \\test "durable" {
        \\    store.durable_now = scriptDurable;
        \\}
        \\
    );
    try t.expectOutput(report ++ "report(zig_move(\"u.zig\", \"h.zig\", {\"scriptDurable\"}))", "edit h.zig: 1 hunk, -1 +4 lines\nedit u.zig: 1 hunk, -5 +1 lines\nmoved\t1\nneeds_pub\tscriptDurable\t3\t\nuses_left\tscriptDurable\t8\t\n");
    // Known limits: `p.end()` through a value names no `P.end`, and
    // `self_file.P` through an alias is no use of `P`: the compiler says.
    try t.put("l.zig",
        \\const self_file = @This();
        \\
        \\const P = struct {
        \\    fn end(p: P) void {
        \\        _ = p;
        \\    }
        \\};
        \\
        \\fn f() void {
        \\    var p: P = .{};
        \\    p.end();
        \\    _ = self_file.P;
        \\}
        \\
    );
    try t.expectOutput(report ++ "report(zig_move(\"l.zig\", \"p.zig\", {\"P\"}))", "edit p.zig: 1 hunk, -1 +6 lines\nedit l.zig: 1 hunk, -7 +1 lines\nmoved\t1\nneeds_pub\tP\t3\t\nuses_left\tP\t10\t\n");
}

test "zig_move and zig_rename: `const` that qualifies a pointer declares nothing: `[]const X` is a use of `X`" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const src = "const X = u8;\n\nfn f(a: []const X, b: *const X, c: *align(1) const X) void {\n    _ = .{ a, b, c };\n}\n";
    try t.put("k.zig", src);
    try t.expectOutput(report ++ "report(zig_move(\"k.zig\", \"x.zig\", {\"X\"}))", "edit x.zig: 1 hunk, -1 +2 lines\nedit k.zig: 1 hunk, -3 +1 lines\nmoved\t1\nneeds_pub\tX\t1\t\nuses_left\tX\t3\t\n");
    // And a qualified rename of those uses does not stop as at a declaration.
    try t.put("k.zig", "fn f(a: []const X, b: *const X, c: *align(1) const X) void {\n    _ = .{ a, b, c };\n}\n");
    try t.expectOutput("print(zig_rename(\"k.zig\", \"X\", \"m.X\").renamed)", "edit k.zig: 1 hunk, -1 +1 lines\n3\n");
}

/// A script replaying a webbox linux_host split from its checkout: the declarations the
/// new file took from `commit`'s parent's linux_host.zig, moved by `zig_move`.
/// Its report is a row per list, `name:line` each.
const replay = "const W = \"" ++ webbox_root ++ "\"\n" ++
    \\-- The history is 0.16 source; Zig 0.17 removed `**` on arrays and strings,
    \\-- so its four uses become what cookbook/zig17/rewrite_array_mult.shale writes, each
    \\-- on its own line (line numbers kept).
    \\function to17(src)
    \\  for p in {{"\"x\" ** 100", "&@as([100]u8, @splat('x'))"}, {"\"x\" ** 40", "&@as([40]u8, @splat('x'))"}, {"\"u\" ** 2000000", "&@as([2000000]u8, @splat('u'))"}, {"&([_]u8{'x'} ** n)", "&(@as([n]u8, @splat('x')))"}} do src = substitute(src, p[1], p[2], {missing_ok = true}) end
    \\  return src
    \\end
    \\function show(rev, p) return to17(must(run({"git", "-C", W, "show", rev .. ":src/" .. p}, {timeout_ms = 30000, whole = 2097152})).out) end
    \\function replay(commit, newfile)
    \\  create("lh.zig", show(commit .. "^", "linux_host.zig"))
    \\  create("new_hand.zig", show(commit, newfile))
    \\  create("lh_hand.zig", show(commit, "linux_host.zig"))
    \\  local before = {}
    \\  for d in zig_decls("lh.zig") do before[d.name] = true end
    \\  local after = {}
    \\  for d in zig_decls("lh_hand.zig") do after[d.name] = true end
    \\  local names = {}
    \\  for d in zig_decls("new_hand.zig") do
    \\    if has_key(before, d.name) and not has_key(after, d.name) then push(names, {kind = d.kind, name = d.name}) end
    \\  end
    \\  local r = zig_move("lh.zig", "new.zig", names)
    \\  local rows = {moved = r.moved}
    \\  for k in {"needs_pub", "needs_import", "needs_from_from", "uses_left"} do
    \\    local row = {}
    \\    for x in r[k] do push(row, x.name .. ":" .. x.line) end
    \\    rows[k] = " " .. join(row, " ") .. " "
    \\  end
    \\  return rows
    \\end
    \\-- Every row of a replay's report, so one more or one fewer fails.
    \\function every(rows)
    \\  print("moved", rows.moved)
    \\  for k in {"needs_pub", "needs_import", "needs_from_from", "uses_left"} do print(k .. ":" .. rows[k]) end
    \\end
    \\
;

test "zig_move replays 589837e (linux_writes.zig): every row: the three the hand made pub and WriteWatch.run; std, net and Io imports; sleepMs from's own; the uses left" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.expectOutput(replay ++
        \\every(replay("589837e", "linux_writes.zig"))
    , "edit new.zig: 1 hunk, -1 +240 lines\nedit lh.zig: 1 hunk, -241 +1 lines\nmoved\t22\nneeds_pub: Hold:527 Sent:593 WriteWatch:703 WriteWatch.run:484 \nneeds_import: std:519 net:595 Io:596 \nneeds_from_from: sleepMs:683 \nuses_left: Sent:305 Hold:308 Hold:325 Sent:325 WriteWatch:483 write_deadline_for_test_ms:483 kept_write_deadline_ms:483 WriteWatch:484 writes_ended_by_deadline:1427 kept_write_deadline_ms:1427 write_deadline_for_test_ms:2294 write_deadline_for_test_ms:2295 turn_write_deadline_for_test_ms:2942 turn_write_deadline_for_test_ms:2943 \n");
}

test "zig_move replays 35317cf (linux_reads.zig): every row: the fifteen the hand made pub and Progress.Ended; std, net, Io and body_rule imports; sleepMs from's own; the uses left" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try t.expectOutput(replay ++
        \\every(replay("35317cf", "linux_reads.zig"))
    , "edit new.zig: 1 hunk, -1 +326 lines\nedit lh.zig: 4 hunks, -330 +4 lines\nmoved\t28\nneeds_pub: Got:531 Progress:548 readWhole:568 Raced:623 verdict:771 watch:797 neverRead:819 slots:830 nextAsk:838 takeSlot:856 readDone:887 giveBackSlot:893 small_held:901 unchargeSmall:913 Whole:922 Progress.Ended:1716 \nneeds_import: std:549 net:551 Io:552 body_rule:568 \nneeds_from_from: sleepMs:803 \nuses_left: Progress:222 neverRead:239 takeSlot:242 nextAsk:242 readWhole:250 fail_after_read_for_test:251 readDone:254 Progress:340 Raced:343 Raced:344 watch:347 Progress:402 giveBackSlot:404 unchargeSmall:411 unchargeSmall:463 deadline_for_test_ms:1438 deadline_for_test_ms:1439 deadline_for_test_ms:1530 deadline_for_test_ms:1531 Progress:1560 Whole:1568 small_held:1620 small_held:1623 stall_for_test_ms:1627 stall_for_test_ms:1628 deadline_for_test_ms:1648 deadline_for_test_ms:1649 small_held:1661 slots:1668 slots:1680 slots:1691 nextAsk:1701 Progress:1706 takeSlot:1707 nextAsk:1707 giveBackSlot:1711 slot_wait_for_test_ms:1712 slot_wait_for_test_ms:1713 Progress:1714 takeSlot:1715 Progress:1716 stall_for_test_ms:1720 stall_for_test_ms:1721 slots:1726 small_budget_for_test:1730 small_budget_for_test:1731 small_held:1739 slot_wait_for_test_ms:1761 slot_wait_for_test_ms:1762 slots:1774 slots:1786 deadline_for_test_ms:1805 deadline_for_test_ms:1806 Got:1826 Progress:1827 Got:1829 verdict:1829 Got:1830 verdict:1830 Got:1831 verdict:1831 Got:1834 verdict:1834 Got:1835 verdict:1835 Got:1836 verdict:1836 Got:1837 verdict:1837 fail_after_read_for_test:1841 fail_after_read_for_test:1842 small_held:1847 slots:1853 stall_for_test_ms:1862 stall_for_test_ms:1863 tick_for_test_ms:1864 tick_for_test_ms:1865 slots:1889 small_held:1906 Progress:1947 small_held:1962 unchargeSmall:1964 Whole:1965 tick_for_test_ms:1979 tick_for_test_ms:1980 tick_for_test_ms:2480 tick_for_test_ms:2481 \n");
}

test "zig_move: an import named through another (`Bus = streams.Bus`) brings that one too, and its own in turn" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    // angry-gopher router.zig's shape, which its search tests needed.
    try t.put("c.zig", "const std = @import(\"std\");\nconst streams = std.mem;\nconst Bus = streams.Allocator;\n\nfn f(b: Bus) void {\n    _ = b;\n}\n");
    try t.expectOutput(report ++ "report(zig_move(\"c.zig\", \"d.zig\", {\"f\"}))", "edit d.zig: 1 hunk, -1 +4 lines\nedit c.zig: 1 hunk, -5 +1 lines\nmoved\t1\nneeds_import\tBus\t5\tstreams.Allocator\nneeds_import\tstreams\t5\tstd.mem\nneeds_import\tstd\t5\t@import(\"std\")\n");
}

test "zig_move: a step a token and a node of both files, more for a file four times the size" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    const a = t.arena.allocator();
    var steps: [2]u64 = undefined;
    for ([_]usize{ 200, 800 }, 0..) |n, k| {
        var src: std.ArrayList(u8) = .empty;
        try src.appendSlice(a, "const moved = 1;\n\n");
        for (0..n) |i| try src.print(a, "const v{d} = x;\n", .{i});
        try t.put("n.zig", src.items);
        try t.put("m.zig", "");
        var r = t.run("local r = zig_move(\"n.zig\", \"m.zig\", {\"moved\"})", .atomic);
        defer r.deinit();
        try testing.expect(r.stop == null);
        steps[k] = r.steps;
    }
    if (steps[1] < steps[0] + 7 * 600) {
        std.debug.print("{d} steps at 200 lines, {d} at 800: less than a step a token and a node more\n", .{ steps[0], steps[1] });
        return error.TestUnexpectedResult;
    }
}
