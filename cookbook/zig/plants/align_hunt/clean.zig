// align_hunt plant: each shape done right; no check may fire here.
fn stringBacking(self: *Machine, id: Idx) !Id {
    const backing = self.store().getString(id);
    const alignment = @max(self.word_size, self.store().strings.alignment(id));
    const allocation = try self.addAllocation(.u8, backing.len, alignment, false, null);
    return allocation.id;
}
fn rocDealloc(_: *RocOps, ptr: *anyopaque, alignment: usize) callconv(.c) void {
    freeAligned(ptr, alignment);
}
fn header(base: [*]u8, offset: usize) *u64 {
    std.debug.assert((@intFromPtr(base) + offset) % @alignOf(u64) == 0);
    return @ptrCast(@alignCast(base + offset));
}
fn listSize(target: Target, len: u32) u32 {
    return len * target.ptrBitWidth() / 8;
}
fn dataOffset(len: usize, alignment: u32) usize {
    return std.mem.alignForward(usize, len, alignment);
}
// A comment's shape never counts: addAllocation(.u8, n, self.word_size, false, null)
