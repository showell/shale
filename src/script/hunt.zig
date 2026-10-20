//! **THE HUNT** (`zig build fuzz`, metal-vmm QUEUE 241): the fuzzes and
//! sweeps, too long for `zig build test`, which only compiles them. Its
//! imports are exactly build.zig's `hunt_files` (checked there at comptime),
//! and tools/hunt_runner.zig runs every test they hold, failing on any that
//! does not pass, so a hunt cannot leave the hunt unseen.

// A `comptime` block, not a `test`: the hunt counts its tests, and a
// `test {}` here would be one.
comptime {
    _ = @import("fuzz_test.zig");
    _ = @import("lex_fuzz_test.zig");
    _ = @import("props_test.zig");
    _ = @import("builtins_hunt_test.zig");
}
