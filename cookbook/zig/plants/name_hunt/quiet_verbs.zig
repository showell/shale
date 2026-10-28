// Quiet for every check: effects named for their acts, by verbs of
// cookbook/words/verbs.txt (angry-gopher's `allocateId`, `resolveUser`).
const std = @import("std");

pub fn allocateId(io: std.Io, next: u32) !u32 {
    _ = io;
    return next + 1;
}

pub fn resolveUser(io: std.Io, name: []const u8) !void {
    _ = io;
    _ = name;
}
