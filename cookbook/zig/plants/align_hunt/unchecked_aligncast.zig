// align_hunt plant: a pointer at base + offset cast to a stricter alignment, never checked.
fn header(base: [*]u8, offset: usize) *u64 {
    return @ptrCast(@alignCast(base + offset));
}
