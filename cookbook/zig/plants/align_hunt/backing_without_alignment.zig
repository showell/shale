// align_hunt plant: a string backing copied out with no question of its alignment.
fn literalBytes(self: *Machine, id: Idx) ![]u8 {
    const backing = self.store().getString(id);
    return self.allocator().dupe(u8, backing);
}
