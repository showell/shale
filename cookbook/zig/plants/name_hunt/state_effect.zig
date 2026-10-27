// A plant for name_hunt's `state_effect`: it loads a file and serves it,
// but its name reads like a state (the Roc chat's `held_raw!`).
const std = @import("std");

pub fn heldRaw(io: std.Io, path: []const u8) !void {
    _ = io;
    _ = path;
}
