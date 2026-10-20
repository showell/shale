//! The portable `shale` module: the script language for a site's own data, as
//! an application imports it (`@import("shale")`). Its Linux host is the
//! `shale_linux` module, for a Linux `main` only. Every effect goes through
//! webbox's `store`, so the same sources run on Linux and on metal.

/// `script.run` parses and runs one script inside its budgets, every effect
/// through `store`.
pub const script = @import("script/script.zig");
/// A signed script run on the live site: the envelope, its signature, the
/// sequence number, the log and the reply behind an application's
/// `POST /admin/script`.
pub const script_route = @import("script_route.zig");
/// The installed suite: an application's scripts, run on a schedule as
/// background runs, their last output kept.
pub const shale_suite = @import("shale_suite.zig");
/// Shale's number parses that no request reaches, for an application's
/// request-edge check. It follows webbox's edge_sites.zig but has a name of
/// its own, since an application's lints read webbox's and Shale's sources as
/// one folder.
pub const edge_sites = @import("shale_edge_sites.zig");

test {
    _ = script;
    _ = script_route;
    _ = shale_suite;
    _ = edge_sites;
    _ = @import("script_route_test.zig");
}
