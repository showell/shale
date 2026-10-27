// A plant for name_hunt's `question`: it asks, but gives a count back.
pub fn isFull(slots: []const u8) usize {
    return slots.len;
}

// And a bool named as a state: does it mark, or ask? (`marked`, now
// angry-gopher's `isSigned`.)
pub fn sealed(path: []const u8) bool {
    return path.len > 0;
}
