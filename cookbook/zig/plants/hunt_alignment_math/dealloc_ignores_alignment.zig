// hunt_alignment_math plant: a dealloc callback that throws the alignment away.
fn rocDealloc(_: *RocOps, ptr: *anyopaque, _: usize) callconv(.c) void {
    std.c.free(ptr);
}
