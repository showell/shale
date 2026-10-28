// Quiet for every check: a participle that is its own base form names the
// act (`readAll` reads), so cookbook/words/participles.txt leaves it out.
const std = @import("std");

pub fn readAll(io: std.Io, path: []const u8) !void {
    _ = io;
    _ = path;
}

pub fn setFlag(io: std.Io, flag: []const u8) !void {
    _ = io;
    _ = flag;
}

pub fn splitLines(io: std.Io, text: []const u8) !void {
    _ = io;
    _ = text;
}

// Zig has no `noun_effect`, so a noun first is quiet here; the census
// calls `links` refused.
pub fn links(io: std.Io, path: []const u8) !void {
    _ = io;
    _ = path;
}
