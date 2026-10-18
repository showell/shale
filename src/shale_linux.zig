//! shale_linux: Shale's Linux host, its own module, so the portable half
//! (the `shale` module) never carries it: the `shale` CLI and a site's Linux
//! `main` import this, and nothing else does. Its files reach the portable
//! half as the `shale` module, and webbox as `webbox`, never by path.

/// What Shale's `run` keeps of a program's output, pure (metal-vmm QUEUE 233).
pub const shale_keep = @import("shale_keep.zig");
/// One program run under `run`'s rules: the raw spawn and its poll loop.
pub const shale_spawn = @import("shale_spawn.zig");
/// Shale's Linux host: `run`, `real`, mounts, behind the core's seam.
pub const shale_host_linux = @import("shale_host_linux.zig");
/// Jobs: programs that outlive the run, under a detached reaper.
pub const shale_jobs = @import("shale_jobs.zig");
/// The host's job calls, over `shale_jobs`.
pub const shale_host_jobs = @import("shale_host_jobs.zig");
pub const shale_host_zig = @import("shale_host_zig.zig");
pub const shale_zig_ast = @import("shale_zig_ast.zig");
pub const shale_zig_move = @import("shale_zig_move.zig");

test {
    _ = shale_keep;
    _ = shale_spawn;
    _ = shale_host_linux;
    _ = shale_jobs;
    _ = shale_host_jobs;
    _ = shale_host_zig;
    _ = shale_zig_ast;
    _ = shale_zig_move;
    _ = @import("shale_test_host.zig");
    _ = @import("shale_prelude_linux_test.zig");
    _ = @import("shale_prelude_git_test.zig");
    _ = @import("shale_jobs_test.zig");
    _ = @import("shale_cli_test.zig");
    _ = @import("shale_zig_rename_test.zig");
    _ = @import("shale_doc_linux_test.zig");
    _ = @import("shale_zig_decls_test.zig");
    _ = @import("shale_zig_move_test.zig");
    _ = @import("shale_zig_imports_test.zig");
    _ = @import("shale_ro_links_test.zig");
}
