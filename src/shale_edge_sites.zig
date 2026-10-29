//! Shale's number parses that no request reaches, each with why: webbox's
//! edge_sites.zig for this repo's sources, read by the same `parseSites`. An
//! application's request-edge check (angry-gopher router.zig's) walks this
//! repo's `src/` beside webbox's and knows these without listing them.

const std = @import("std");
const Io = @import("host_io").Io;
const webbox_sites = @import("webbox").edge_sites;

pub const Site = webbox_sites.Site;

/// Paths are from this repo's `src/`.
pub const unreachable_from_requests = [_]Site{
    .{ .file = "script_route.zig", .func = "lastSequence", .why = "a sequence file the server wrote" },
    .{ .file = "shale_sign.zig", .func = "main", .why = "the signer, on the box, not the server" },
    .{ .file = "shale_fixture.zig", .func = "main", .why = "the program Shale's run tests spawn, not the server (metal-vmm QUEUE 233)" },
    .{ .file = "shale_jobs.zig", .func = "parseRecord", .why = "a job's record its reaper wrote, read by the Linux shale CLI, not the server (metal-vmm QUEUE 233)" },
    .{ .file = "shale_host_procs.zig", .func = "stat", .why = "the kernel's /proc/<pid>/stat, read by the Linux shale CLI's `procs`, not the server" },
    .{ .file = "shale_host_procs.zig", .func = "uidOf", .why = "the kernel's /proc/<pid>/status, read by the Linux shale CLI's `procs`, not the server" },
    .{ .file = "shale_host_procs.zig", .func = "list", .why = "/proc's folder names, read by the Linux shale CLI's `procs`, not the server" },
    .{ .file = "shale_host_procs.zig", .func = "uidOf", .why = "the kernel's /proc/<pid>/status Uid line, read by the Linux shale CLI's `procs`, not the server" },
    .{ .file = "shale_sign.zig", .func = "lastFor", .why = "the signer, on the box, not the server" },
    .{ .file = "script/lex.zig", .func = "number", .why = "an integer literal in a script's text, which only the admin's signed script brings (no edge request is signed); errors_test sends it the 64-bit edges" },
    .{ .file = "shale_host_json.zig", .func = "hex4", .why = "a `\\u` escape in the JSON text a script hands json_read: Linux host only, so the server never compiles it, and only the admin's signed script brings a script; its tests send the edges" },
    .{ .file = "script/builtins.zig", .func = "parseInteger", .why = "parse_int's text, from a script, which only the admin's signed script brings; builtins_test sends it the edges, past 64 bits among them" },
};

test "edge_sites: every site named is a function of that name in that file, and parses a number inside it" {
    const io = std.testing.io;
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var src = try Io.Dir.cwd().openDir(io, "src", .{});
    defer src.close(io);
    for (unreachable_from_requests) |s| {
        errdefer std.debug.print("edge_sites: {s}'s {s} parses no number\n", .{ s.file, s.func });
        const text = try src.readFileAllocOptions(io, s.file, a, .limited(4 << 20), .of(u8), 0);
        const sites = try webbox_sites.parseSites(a, text);
        const there = for (sites) |p| {
            if (std.mem.eql(u8, p.func, s.func)) break true;
        } else false;
        try std.testing.expect(there);
    }
}
