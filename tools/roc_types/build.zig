//! `roc-types` (main.zig), built against roc-lang/roc's own checker, through
//! its embedding API, at the commit its own roc-version pins (beside this
//! file): roc-chat's, the Roc the code Shale's Roc recipes judge is built
//! with. Its own build, not Shale's: Roc's build.zig runs as its dependency's.
//! Built on GitHub (.github/workflows/roc-types.yml), never on the box:
//! linking `compile` and `eval` builds most of the compiler, more than the
//! box's two cores and 8 GB hold beside its other work.

const std = @import("std");

// **BUILT WITH THE ZIG THE PINNED ROC BUILDS WITH** (its build.zig.zon's
// minimum_zig_version), not this repo's: Roc's own build.zig runs as the
// dependency's, and another Zig refuses it. The patched 0.17.0.
comptime {
    const want = std.SemanticVersion{ .major = 0, .minor = 17, .patch = 0 };
    if (@import("builtin").zig_version.order(want) != .eq)
        @compileError("roc-types builds with Zig 0.17.0, the pinned Roc's");
}

pub fn build(b: *std.Build) void {
    const pin = checkPin(b);
    // The pin roc-types says in its header, so a reader knows whose checker
    // wrote the types.
    const options = b.addOptions();
    options.addOption([]const u8, "roc_pin", pin);
    const target = b.standardTargetOptions(.{});
    // ReleaseSafe unless asked: every Roc recipe runs this program, and Debug
    // takes about six times the CPU to print the same bytes.
    const optimize = b.option(std.builtin.Optimize, "optimize", "the mode to build in (default ReleaseSafe)") orelse .safe;
    const roc = b.dependency("roc", .{ .target = target, .optimize = optimize });
    const exe = b.addExecutable(.{
        .name = "roc-types",
        .root_module = b.createModule(.{
            .root_source_file = b.path("main.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    inline for (.{ "base", "can", "eval", "roc_target", "compile", "ctx", "types" }) |name| {
        exe.root_module.addImport(name, roc.module(name));
    }
    exe.root_module.addImport("roc_build_options", roc.module("build_options"));
    exe.root_module.addImport("build_options", options.createModule());
    b.installArtifact(exe);
}

/// **THE CHECKER IS THE COMPILER'S**: build.zig.zon's roc must be the commit
/// the roc-version beside it names, or the types are another compiler's
/// than the one our code is built with. A roc-version bump fails here until
/// the dependency moves too. Gives the pin, the whole `roc-version` line.
fn checkPin(b: *std.Build) []const u8 {
    const io = b.graph.io;
    const dir = b.root.root_dir.handle;
    const here = b.root.subPathOrDot();
    const version = std.mem.trim(u8, dir.readFileAlloc(io, b.pathJoin(&.{ here, "roc-version" }), b.allocator, .limited(256)) catch |e| std.process.fatal("roc-types: cannot read its roc-version ({t})", .{e}), " \n");
    const commit = version[(std.mem.lastIndexOfScalar(u8, version, '-') orelse std.process.fatal("roc-types: roc-version `{s}` names no commit", .{version})) + 1 ..];
    const zon = dir.readFileAlloc(io, b.pathJoin(&.{ here, "build.zig.zon" }), b.allocator, .limited(64 * 1024)) catch |e| std.process.fatal("roc-types: cannot read build.zig.zon ({t})", .{e});
    if (std.mem.indexOf(u8, zon, b.fmt("/archive/{s}", .{commit})) == null)
        std.process.fatal("roc-types: roc-version pins Roc {s}, and build.zig.zon's roc is another commit: `zig fetch --save=roc https://github.com/roc-lang/roc/archive/<that commit, in full>.tar.gz`", .{commit});
    return version;
}
