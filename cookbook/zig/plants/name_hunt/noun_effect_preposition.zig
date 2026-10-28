// A plant for name_hunt's `noun_effect`: an effect that starts with a
// preposition.
const Io = @import("std").Io;

fn byFileTime(io: Io, path: []const u8) u64 {
    _ = io;
    return path.len;
}
