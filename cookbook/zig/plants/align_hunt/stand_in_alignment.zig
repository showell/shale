// align_hunt plant: a blob allocated at the word, not at its own alignment (#12641's shape).
fn stringBacking(self: *Machine, id: Idx) !Id {
    const backing = self.bytes(id);
    const allocation = try self.addAllocation(.u8, backing.len, self.word_size, false, null);
    return allocation.id;
}
