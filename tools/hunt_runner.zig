//! **THE HUNT'S RUNNER** (`zig build fuzz`, src/script/hunt.zig): runs every
//! test as zig's own runner does (a fresh `testing.allocator` and
//! `testing.io` each, leaks counted), fails when any test returns an error
//! of any kind, a skip among them, and prints how many hunts ran, so the
//! count can be held to the count defined. Runs in the build's "simple"
//! mode, as tools/timing_runner.zig.

const std = @import("std");
const builtin = @import("builtin");
const testing = std.testing;

pub fn main(init: std.process.Init.Minimal) !void {
    const fns = builtin.test_functions;
    var failed: usize = 0;
    var leaks: usize = 0;
    for (fns) |t| {
        testing.allocator_instance = .init(std.heap.page_allocator, .{ .canary = 0xc3a701ba, .check_write_after_free = true });
        testing.io_instance = .init(testing.allocator, .{ .argv0 = .init(init.args), .environ = init.environ });
        testing.environ = init.environ;
        if (t.func()) |_| {} else |e| {
            failed += 1;
            std.debug.print("FAIL ({t}) {s}\n", .{ e, t.name });
        }
        testing.io_instance.deinit();
        if (testing.allocator_instance.deinit() != 0) {
            leaks += 1;
            std.debug.print("LEAK {s}\n", .{t.name});
        }
    }
    if (failed != 0 or leaks != 0) {
        std.debug.print("{d} hunts ran: {d} failed, {d} leaked\n", .{ fns.len, failed, leaks });
        std.process.exit(1);
    }
    std.debug.print("{d} hunts ran\n", .{fns.len});
}
