//! Shale's Linux host, a module of its own so the portable `shale` module never
//! carries it. The `shale` CLI and a site's Linux `main` import this, and
//! nothing else does. Its files reach the portable half as the `shale` module,
//! and webbox as `webbox`, never by path.

/// What Shale's `run` keeps of a program's output; pure.
pub const shale_keep = @import("shale_keep.zig");
/// One program run under `run`'s rules: the raw spawn and its poll loop.
pub const shale_spawn = @import("shale_spawn.zig");
/// Shale's Linux host: `run`, `real`, mounts, behind the core's seam.
pub const shale_host_linux = @import("shale_host_linux.zig");
/// What a Linux run is set up with: mounts, preludes, its programs' environment.
pub const shale_host_setup = @import("shale_host_setup.zig");
/// `run` on Linux: its options, a program under its deadline, its output kept.
pub const shale_host_run = @import("shale_host_run.zig");
/// The host's Linux system calls outside spawn and jobs: the clock, /proc,
/// a pidfd, `stdout_secret`'s file.
pub const shale_os_linux = @import("shale_os_linux.zig");
/// Jobs: programs that outlive the run, under a detached reaper.
pub const shale_jobs = @import("shale_jobs.zig");
/// A job's record, `<log>.job`: plain text, no system call.
pub const shale_job_record = @import("shale_job_record.zig");
/// The host's job calls, over `shale_jobs`.
pub const shale_host_jobs = @import("shale_host_jobs.zig");
/// The host's Zig tools, read by zig-ast (tools/zig_ast), a program of its own.
pub const shale_host_zig = @import("shale_host_zig.zig");
/// The wire between the host and zig-ast: plain bytes.
pub const shale_zig_wire = @import("shale_zig_wire.zig");

test {
    _ = shale_keep;
    _ = shale_spawn;
    _ = shale_os_linux;
    _ = shale_host_linux;
    _ = shale_host_setup;
    _ = shale_host_run;
    _ = shale_jobs;
    _ = shale_job_record;
    _ = shale_host_jobs;
    _ = shale_host_zig;
    _ = shale_zig_wire;
    _ = @import("shale_test_host.zig");
    _ = @import("shale_prelude_linux_test.zig");
    _ = @import("shale_prelude_git_test.zig");
    _ = @import("shale_jobs_test.zig");
    _ = @import("shale_procs_test.zig");
    _ = @import("shale_cli_test.zig");
    _ = @import("shale_doc_linux_test.zig");
    // The Zig tools' tests are src/shale_zig_tools_test.zig's, a root of their own.
    _ = @import("shale_tokens_test.zig");
    _ = @import("shale_ro_links_test.zig");
    _ = @import("shale_abs_path_test.zig");
    _ = @import("shale_use_test.zig");
    _ = @import("shale_undo_test.zig");
    _ = @import("shale_cookbook_test.zig");
    _ = @import("shale_host_flat_tree.zig");
    _ = @import("shale_roc_tree_test.zig");
    _ = @import("shale_go_tree_test.zig");
}
