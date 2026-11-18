//! host_io for Shale in a browser: the build hands the portable sources this
//! file as their `host_io` module, and its `Io` is an in-memory tree
//! (wasm_io.zig).
pub const Io = @import("wasm_io.zig");
