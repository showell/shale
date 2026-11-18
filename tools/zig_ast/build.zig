//! Builds zig-ast (README.md says what it is): `zig build` installs
//! zig-out/bin/zig-ast, always ReleaseSafe; `zig build test` runs its tests.

const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    // The tests' mode: Debug unless asked, so they compile in seconds.
    const optimize = b.standardOptimizeOption(.{});

    // webbox's `zig_refs.zig` reads a file's `@import`s for `imports`, and
    // webbox's src/ is half of the tests' corpus.
    const webbox = b.dependency("webbox", .{});
    const zig_refs = b.createModule(.{ .root_source_file = webbox.path("src/zig_refs.zig") });

    const module = struct {
        fn of(bb: *std.Build, t: std.Build.ResolvedTarget, mode: std.builtin.Optimize, refs: *std.Build.Module) *std.Build.Module {
            return bb.createModule(.{
                .root_source_file = bb.path("main.zig"),
                .target = t,
                .optimize = mode,
                .imports = &.{.{ .name = "zig_refs", .module = refs }},
            });
        }
    }.of;

    // What `zig build` installs is always ReleaseSafe: Shale's host runs it
    // for every Zig call, so a Debug one would cost every run.
    const exe = b.addExecutable(.{ .name = "zig-ast", .root_module = module(b, target, .ReleaseSafe, zig_refs) });
    b.installArtifact(exe);

    // The corpus the tests read whole: this folder's own Zig and webbox's
    // src/, as absolute paths, so the test binary finds them wherever it
    // runs.
    const options = b.addOptions();
    options.addOption([]const []const u8, "corpus", &.{ absolute(b, b), b.pathJoin(&.{ absolute(b, webbox.builder), "src" }) });

    const unit = b.addTest(.{ .root_module = module(b, target, optimize, zig_refs) });
    unit.root_module.addImport("zig_ast_test_options", options.createModule());
    const fmt_check = b.addFmt(.{ .paths = &.{ b.path("build.zig"), b.path("build.zig.zon"), b.path("main.zig"), b.path("plan.zig"), b.path("wire.zig"), b.path("zig_ast.zig"), b.path("zig_ast_test.zig"), b.path("zig_move.zig") }, .check = true });
    unit.step.dependOn(&fmt_check.step);
    const unit_run = b.addRunArtifact(unit);
    // The tests read the corpus as they run, which the build cannot list
    // as inputs, so they always run.
    unit_run.has_side_effects = true;
    b.step("test", "zig-ast's tests: the format, then the reader, the planner and the mover over fixtures and the corpus").dependOn(&unit_run.step);
}

/// A package's folder as an absolute path: its `root` is relative to the
/// top-level build's.
fn absolute(b: *std.Build, of: *std.Build) []const u8 {
    return of.root.root_dir.handle.realPathFileAlloc(b.graph.io, of.root.subPathOrDot(), b.allocator) catch |e| std.debug.panic("zig-ast: the folder {f}: {t}", .{ of.root, e });
}
