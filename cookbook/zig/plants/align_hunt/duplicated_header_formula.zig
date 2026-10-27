// align_hunt plant: the refcount header rule, written out again.
fn dataOffset(alignment: u32) usize {
    const word_size = 8;
    return std.mem.alignForward(usize, word_size, alignment);
}
