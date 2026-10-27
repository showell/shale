//! The Zig tools' tests (zig_rename, zig_decls, zig_move, zig_imports), a
//! test root of their own beside src/shale_linux.zig's, so their binary runs
//! beside the Linux host's rather than after it in one process (metal-vmm
//! QUEUE 252): their properties walk every Zig file of this repo and
//! webbox's, and are the gate's longest.

test {
    _ = @import("shale_zig_rename_test.zig");
    _ = @import("shale_zig_decls_test.zig");
    _ = @import("shale_zig_move_test.zig");
    _ = @import("shale_zig_imports_test.zig");
}
