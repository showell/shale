//! Tests of the budgets a script's `-- budget:` line raises, run whole: the
//! kept budget and the machine budget (metal-vmm QUEUE 307), and the machine's
//! memory given back where a run makes and drops it over and over.

const std = @import("std");
const script = @import("script.zig");
const cli = @import("cli.zig");
const testing = std.testing;

/// A run of `source` with the budgets its own `-- budget:` line sets.
fn runByHeader(source: []const u8, out: []u8) !script.Result {
    var why: [256]u8 = undefined;
    var args: [8]script.Arg = undefined;
    var c = switch (cli.parse(&.{"a.shale"}, null, &args, &why)) {
        .command => |got| got,
        .refused => return error.Refused,
    };
    if (cli.headerBudgets(source, &c, &why) != null) return error.Refused;
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    return script.run(threaded.io(), testing.allocator, ".zig-cache/no-root", source, c.budgets, 1_700_000_000, out);
}

test "budgets: a script that keeps more than the default kept budget at a yield stops, saying how to raise it, and runs once its header asks (metal-vmm QUEUE 307)" {
    var out: [1024]u8 = undefined;
    const body = "local l = {}\nfor i = 1, 60000 do push(l, \"abcdefghijklmnop\" .. i) end\nyield()\nprint(#l)\n";
    var short = try runByHeader("-- budget: steps=20000000\n" ++ body, &out);
    defer short.deinit();
    try testing.expectEqual(script.diag.Budget.kept, short.stop.?.budget.?);
    try testing.expect(std.mem.indexOf(u8, short.stop.?.message, "--budget kept_bytes=N") != null);
    var asked = try runByHeader("-- budget: steps=20000000 kept_bytes=16777216\n" ++ body, &out);
    defer asked.deinit();
    try testing.expect(asked.stop == null);
    try testing.expectEqualStrings("60000\n", asked.output);
}

test "budgets: the machine budget is the header's to raise, and its stop says how (metal-vmm QUEUE 307)" {
    var out: [1024]u8 = undefined;
    const body = "local t = {}\nfor i = 1, 10 do t[i] = i end\nprint(#t)\n";
    var short = try runByHeader("-- budget: machine_bytes=1\n" ++ body, &out);
    defer short.deinit();
    try testing.expectEqual(script.diag.Budget.machine, short.stop.?.budget.?);
    try testing.expect(std.mem.indexOf(u8, short.stop.?.message, "machine_bytes=N") != null);
    var asked = try runByHeader("-- budget: machine_bytes=33554432\n" ++ body, &out);
    defer asked.deinit();
    try testing.expect(asked.stop == null);
    try testing.expectEqualStrings("10\n", asked.output);
}

test "budgets: a per-line loop over more patterns than the regex cache holds runs in the default machine budget: an evicted pattern's memory is given back (metal-vmm QUEUE 307)" {
    // 25 patterns over 745 lines, the box's inventory's shape: each line
    // recompiles every pattern the cache of 8 evicted. Before the machine's
    // memory gave back what the cache freed, this stopped at `budget machine`
    // after about 540,000 steps.
    var out: [1024]u8 = undefined;
    var r = try runByHeader(
        \\-- budget: steps=900000000
        \\local pats = {}
        \\for k = 0, 24 do pats[#pats + 1] = "w" .. tostring(k) .. "[a-z]+" end
        \\local n = 0
        \\for line = 1, 745 do
        \\  for p in pats do
        \\    if re_find("some text w3abc here", p) ~= nil then n = n + 1 end
        \\  end
        \\end
        \\print(n)
        \\
    , &out);
    defer r.deinit();
    try testing.expect(r.stop == null);
    try testing.expectEqualStrings("745\n", r.output);
}
