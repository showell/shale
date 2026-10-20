const std = @import("std");

// Shale: what an application imports as modules, never by path; and the
// `shale` CLI, the box's tool.
//
//   zig build test   -> the unit tests
// **THE HUNT FILES** (`zig build fuzz`, metal-vmm QUEUE 241): the files of
// the tests named `expensive ...`, and the only ones. src/script/hunt.zig
// imports exactly these (checked here); src/script/hunt_guard_test.zig,
// handed the list, fails on such a test anywhere else.
const hunt_files = [_][]const u8{
    "src/script/fuzz_test.zig",
    "src/script/lex_fuzz_test.zig",
    "src/script/props_test.zig",
    "src/script/builtins_hunt_test.zig",
};
comptime {
    @setEvalBranchQuota(1_000_000);
    const root = @embedFile("src/script/hunt.zig");
    if (std.mem.count(u8, root, "\n    _ = @import(") != hunt_files.len) @compileError("src/script/hunt.zig imports other than build.zig's hunt_files");
    for (hunt_files) |path| {
        if (std.mem.indexOf(u8, root, "\n    _ = @import(\"" ++ path["src/script/".len..] ++ "\");") == null) @compileError("src/script/hunt.zig does not import " ++ path);
    }
}

// **THE KEY MODULES, MEASURED** (`zig build script-coverage`, metal-vmm
// QUEUE 241 B and C): what the site runs, held to 100% (C1-C6), then the
// Linux host, measured and reported (C7). tools/coverage.shale groups them.
const coverage_files = [_][]const u8{
    // C1: the live site's POST /admin/script.
    "src/script_route.zig",
    // C2: the regex engine.
    "src/script/regex.zig",
    "src/script/builtins_regex.zig",
    // C3: the front end.
    "src/script/lex.zig",
    "src/script/parse.zig",
    "src/script/diag.zig",
    // C4: the compiler and VM.
    "src/script/compile.zig",
    "src/script/vm.zig",
    "src/script/process.zig",
    "src/script/collect.zig",
    // C5: the walker and the budgets.
    "src/script/interp.zig",
    "src/script/value.zig",
    "src/script/builtins_store.zig",
    "src/script/script.zig",
    // C6: the other builtins, and the suite.
    "src/script/builtins_change.zig",
    "src/script/builtins_read.zig",
    "src/script/builtins_text.zig",
    "src/script/builtins_lists.zig",
    "src/script/builtins_walk.zig",
    "src/script/builtins_time.zig",
    "src/script/builtins_format.zig",
    "src/script/builtins_numbers.zig",
    "src/script/builtins.zig",
    "src/shale_suite.zig",
    // C7: the Linux host (measured, not held to 100%).
    "src/shale_spawn.zig",
    "src/shale_keep.zig",
    "src/shale_jobs.zig",
    "src/shale_host_jobs.zig",
    "src/shale_host_run.zig",
    "src/shale_host_linux.zig",
    "src/shale_host_zig.zig",
    "src/shale_zig_ast.zig",
    "src/shale_zig_move.zig",
    "src/shale_host_setup.zig",
    "src/script/cli.zig",
};

/// Every file under `dirs` (relative to the build root), as paths from it.
fn filesUnder(b: *std.Build, dirs: []const []const u8) []const []const u8 {
    const io = b.graph.io;
    var out: std.ArrayList([]const u8) = .empty;
    for (dirs) |d| {
        var dir = b.root.root_dir.handle.openDir(io, b.pathJoin(&.{ b.root.subPathOrDot(), d }), .{ .iterate = true }) catch |e| std.debug.panic("shale: {s}: {t}", .{ d, e });
        defer dir.close(io);
        var walker = dir.walk(b.allocator) catch @panic("OOM");
        defer walker.deinit();
        while (walker.next(io) catch |e| std.debug.panic("shale: walking {s}: {t}", .{ d, e })) |entry| {
            if (entry.kind != .file) continue;
            out.append(b.allocator, b.pathJoin(&.{ d, entry.path })) catch @panic("OOM");
        }
    }
    return out.items;
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // **WEBBOX, UNDER EVERY EFFECT**: a script's effects all go through
    // webbox's `store`, and its sources take their `Io` from webbox's
    // `host_io` (zig-metal-unikernel's build hands the same sources the
    // machine's).
    const webbox_dep = b.dependency("webbox", .{ .target = target, .optimize = optimize });
    const webbox = webbox_dep.module("webbox");
    const host_io = webbox_dep.module("host_io");

    // **TWO MODULES**: `shale`, portable, which a site's code imports; and
    // `shale_linux` (src/shale_linux.zig), the Linux host, which only a
    // Linux `main` imports and which reaches the portable half as `shale`.
    const shale = b.addModule("shale", .{
        .root_source_file = b.path("src/shale.zig"),
        .imports = &.{
            .{ .name = "host_io", .module = host_io },
            .{ .name = "webbox", .module = webbox },
        },
    });
    const shale_linux = b.addModule("shale_linux", .{
        .root_source_file = b.path("src/shale_linux.zig"),
        .imports = &.{
            .{ .name = "shale", .module = shale },
            .{ .name = "webbox", .module = webbox },
            .{ .name = "host_io", .module = host_io },
        },
    });

    // **THE SHALE RUNNER** (src/script_runner.zig): a script against a
    // data folder or a checkout, on Linux.
    //   SHALE_ROOT=<data> zig build script -- <file.shale>
    const runner = b.addExecutable(.{ .name = "shale", .root_module = b.createModule(.{
        .root_source_file = b.path("src/script_runner.zig"),
        .target = target,
        .optimize = optimize,
        .imports = &.{
            .{ .name = "webbox", .module = webbox },
            .{ .name = "shale", .module = shale },
            .{ .name = "shale_linux", .module = shale_linux },
        },
    }) });
    b.installArtifact(runner);
    const run_script = b.addRunArtifact(runner);
    run_script.addPassthruArgs();
    b.step("script", "run a Shale script: zig build script -- [--root DIR] [--verbose] [--budget NAME=N]... [<file.shale> | -e SOURCE] [name=value ...]").dependOn(&run_script.step);

    // **SHALE'S HOT PATHS, TIMED** (src/script_bench.zig): no tier runs it.
    const webbox_rs = b.dependency("webbox", .{ .target = target, .optimize = .safe });
    const shale_release = b.addModule("shale_release", .{
        .root_source_file = b.path("src/shale.zig"),
        .imports = &.{
            .{ .name = "host_io", .module = webbox_rs.module("host_io") },
            .{ .name = "webbox", .module = webbox_rs.module("webbox") },
        },
    });
    const script_bench = b.addExecutable(.{ .name = "script-bench", .root_module = b.createModule(.{
        .root_source_file = b.path("src/script_bench.zig"),
        .target = target,
        .optimize = .safe,
        .imports = &.{.{ .name = "shale", .module = shale_release }},
    }) });
    b.step("script-bench", "time Shale's hot paths (ReleaseSafe)").dependOn(&b.addRunArtifact(script_bench).step);

    // **THE SITE'S REAL SCALE** (src/shale_scale.zig, metal-vmm QUEUE
    // 209(g)): the largest site each cookbook script fits. No tier runs it.
    const scale = b.addExecutable(.{ .name = "shale-scale", .root_module = b.createModule(.{
        .root_source_file = b.path("src/shale_scale.zig"),
        .target = target,
        .optimize = .safe,
        .imports = &.{.{ .name = "shale", .module = shale_release }},
    }) });
    b.step("shale-scale", "the largest site each cookbook script fits, under the site's budgets (ReleaseSafe)").dependOn(&b.addRunArtifact(scale).step);

    // **THE SHALE SIGNER** (src/shale_sign.zig, metal-vmm QUEUE 200): the
    // key pair, made once, and a script signed for a target.
    //   zig build shale-sign -- keygen | pubkey | sign <file.shale> <target> [minutes]
    const signer = b.addExecutable(.{ .name = "shale-sign", .root_module = b.createModule(.{
        .root_source_file = b.path("src/shale_sign.zig"),
        .target = target,
        .optimize = optimize,
        .imports = &.{.{ .name = "shale", .module = shale }},
    }) });
    b.installArtifact(signer);
    const run_signer = b.addRunArtifact(signer);
    run_signer.addPassthruArgs();
    b.step("shale-sign", "the Shale signer: zig build shale-sign -- keygen | pubkey | sign <file.shale> <target> [minutes]").dependOn(&run_signer.step);
    const signer_test = b.addTest(.{ .root_module = signer.root_module });

    // **SHALE-GATE** (tools/shale_gate.zig, metal-vmm QUEUE 233): the
    // PreToolUse hook's classifier, passing a `shale` launch or the
    // escape and refusing the rest. Built here; Steve installs the hook.
    const gate = b.addExecutable(.{ .name = "shale-gate", .root_module = b.createModule(.{
        .root_source_file = b.path("tools/shale_gate.zig"),
        .target = target,
        .optimize = optimize,
    }) });
    b.installArtifact(gate);
    const gate_test = b.addTest(.{ .root_module = b.createModule(.{
        .root_source_file = b.path("tools/shale_gate_test.zig"),
        .target = target,
        .optimize = optimize,
    }) });
    const gate_options = b.addOptions();
    gate_options.addOptionPath("shale_gate", gate.getEmittedBin());
    gate_test.root_module.addImport("shale_gate_options", gate_options.createModule());

    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&b.addRunArtifact(gate_test).step);
    // **THE SCRIPT FUZZER'S COUNT** (src/script/fuzz_test.zig): a few hundred
    // sources in the tests, as many as asked for with -Dscript-fuzz.
    const fuzz_runs = b.option(u32, "script-fuzz", "how many random and mutated sources the script fuzzer runs (default 400)") orelse 400;
    // **THE EXPENSIVE ONES HUNT, NO GATE PAYS FOR THEM** (Steve, 2026-10-17:
    // fuzzers are for bug-hunting expeditions; each bug found becomes a
    // targeted unit test): the tests named `expensive fuzz: ...` or
    // `expensive sweep: ...` live in `hunt_files` alone, reached from a root
    // of their own (src/script/hunt.zig, its imports checked against the
    // list below), which `zig build test` compiles and only `zig build fuzz`
    // runs (metal-vmm QUEUE 241). Nothing is skipped: src/script/
    // hunt_guard_test.zig fails on a skip, or a hunt outside the list.
    const script_options = b.addOptions();
    script_options.addOption(u32, "fuzz_runs", fuzz_runs);
    const fuzz_step = b.step("fuzz", "The expensive fuzzes and sweeps (src/script/hunt.zig), for a bug hunt; no gate runs them");
    // **ONLY THE TESTS NAMED** (-Dtest-filter=<text>): a change's own tests
    // in seconds, before the whole suite.
    const test_filter = b.option([]const u8, "test-filter", "run only the tests whose names contain this");
    // **EACH TEST'S TIME** (-Dtest-timing, tools/timing_runner.zig): every
    // test's wall time, slowest first, so a suite's cost has names.
    const timing: ?std.Build.Step.Compile.TestRunner = if (b.option(bool, "test-timing", "print each test's time, slowest first") orelse false)
        .{ .path = b.path("tools/timing_runner.zig"), .mode = .simple }
    else
        null;
    // **THE FIXTURE PROGRAM** `run`'s tests run (src/shale_fixture.zig,
    // metal-vmm QUEUE 233): never /bin/sh. Its path reaches the tests as
    // `shale_test_options.fixture`.
    const fixture = b.addExecutable(.{ .name = "shale-fixture", .root_module = b.createModule(.{
        .root_source_file = b.path("src/shale_fixture.zig"),
        .target = target,
        .optimize = optimize,
    }) });
    const shale_options = b.addOptions();
    shale_options.addOptionPath("fixture", fixture.getEmittedBin());
    // The jobs' reaper is `shale` itself (`shale --reap`): its tests run it.
    shale_options.addOptionPath("shale", runner.getEmittedBin());
    shale_options.addOption([]const []const u8, "hunt_files", &hunt_files);
    // webbox's checkout: the Zig tools' tests replay splits from its
    // history and read its sources beside this repo's.
    // Absolute, as 0.16's addOptionPath gave it: the tests hand it to
    // scripts that run `git -C` from elsewhere, where Zig 0.17's relative
    // path (`../zig-webbox/.`) would not reach it.
    // Resolved through webbox's own directory: its `root_dir.path` is
    // relative to the top-level build, which is shale only when shale is
    // built alone.
    const webbox_root = webbox_dep.builder.root;
    shale_options.addOption([]const u8, "webbox_root", webbox_root.root_dir.handle.realPathFileAlloc(b.graph.io, webbox_root.subPathOrDot(), b.allocator) catch |e| std.debug.panic("shale: webbox's root {f}: {t}", .{ webbox_root, e }));
    // **WHAT THE TESTS READ AS THEY RUN** (2026-10-20): the cookbook's
    // recipes (shale_cookbook_test.zig parses each), the docs and tools/
    // (the doc and hunt-guard tests): inputs of the unit runs, so a changed
    // recipe runs them again, where a cached run once passed it unread.
    const read_at_run = filesUnder(b, &.{ "cookbook", "docs", "tools" });
    // Each unit binary twice: as `test` runs it, and Debug, whole and
    // under zig's own runner, for `script-coverage` (built only for it).
    const unit_roots = [_][]const u8{ "src/shale.zig", "src/shale_linux.zig" };
    var coverage_units: [unit_roots.len]*std.Build.Step.Compile = undefined;
    for (unit_roots, &coverage_units) |path, *for_coverage| for ([_]bool{ false, true }) |measured| {
        const unit = b.addTest(.{ .root_module = b.createModule(.{
            .root_source_file = b.path(path),
            .target = target,
            .optimize = if (measured) .debug else optimize,
        }), .filters = if (test_filter != null and !measured) b.dupeStrings(&.{test_filter.?}) else &.{}, .test_runner = if (measured) null else timing });
        // A file of the Linux host reaches the portable half as the module;
        // a file of the portable half, by path.
        unit.root_module.addImport("shale", shale);
        unit.root_module.addImport("webbox", webbox);
        unit.root_module.addImport("host_io", host_io);
        unit.root_module.addImport("script_test_options", script_options.createModule());
        unit.root_module.addImport("shale_test_options", shale_options.createModule());
        if (measured) {
            for_coverage.* = unit;
        } else {
            const unit_run = b.addRunArtifact(unit);
            for (read_at_run) |p| unit_run.addFileInput(b.path(p));
            test_step.dependOn(&unit_run.step);
        }
    };
    // **THE LINES NO UNIT TEST RUNS** (`zig build script-coverage`, metal-vmm
    // QUEUE 241 B): the coverage SDK's `linecov` over the two unit binaries,
    // every file of `coverage_files` measured in one run of each (not the
    // hunt: the measure is of the unit tests). tools/coverage.shale reads it.
    // The SDK is resolved only when Shale is the build at the top and the
    // sibling is there, so an application that builds Shale, or a checkout
    // without the SDK, never needs it; the step then says what it lacks.
    const coverage_step = b.step("script-coverage", "the lines of each key module (build.zig's coverage_files) no unit test runs, by the coverage SDK's linecov (needs ../zig-coverage-sdk)");
    const sdk_there = if (b.root.root_dir.handle.openDir(b.graph.io, b.pathJoin(&.{ b.root.subPathOrDot(), "..", "zig-coverage-sdk" }), .{})) |d| blk: {
        d.close(b.graph.io);
        break :blk true;
    } else |_| false;
    if (!b.isRoot()) {} else if (!sdk_there) {
        coverage_step.dependOn(&b.addFail("script-coverage: no ../zig-coverage-sdk beside this checkout").step);
    } else if (b.lazyDependency("zig_coverage_sdk", .{})) |sdk| {
        const measure = b.addRunArtifact(sdk.artifact("linecov"));
        for (coverage_files) |f| {
            measure.addArg(f);
            measure.addFileInput(b.path(f));
        }
        measure.addArg("--");
        for (coverage_units) |u| measure.addArtifactArg(u);
        measure.has_side_effects = true;
        measure.stdio = .inherit;
        coverage_step.dependOn(&measure.step);
    }
    // The hunt, under tools/hunt_runner.zig: every test it holds runs, any
    // error (a skip among them) fails it, and it says how many ran.
    const hunt = b.addTest(.{ .root_module = b.createModule(.{
        .root_source_file = b.path("src/script/hunt.zig"),
        .target = target,
        .optimize = optimize,
    }), .test_runner = .{ .path = b.path("tools/hunt_runner.zig"), .mode = .simple } });
    // The script module as a module: by path, the hunt would hold its tests.
    hunt.root_module.addImport("shale", shale);
    hunt.root_module.addImport("script_test_options", script_options.createModule());
    // Compiled by `test`, so a hunt cannot rot between expeditions.
    test_step.dependOn(&hunt.step);
    fuzz_step.dependOn(&b.addRunArtifact(hunt).step);
    test_step.dependOn(&b.addRunArtifact(signer_test).step);

    // **THE DOCS' CITED PATHS AND NAMES** (metal-vmm QUEUE 237):
    // tools/doc_check.shale, by this tree's own `shale`, from the folder
    // that holds the repos: its fixtures first, then the docs. Fails on
    // any finding. Each repo's gate checks its own (`only=<repo>`, through
    // `zig build doc-check -- only=<repo>` here), never another's.
    const here = b.root.root_dir.path orelse ".";
    const parent = b.pathJoin(&.{ here, ".." });
    const docCheck = struct {
        fn run(bb: *std.Build, bin: std.Build.LazyPath, dir: []const u8) *std.Build.Step.Run {
            const r = bb.addSystemCommand(&.{ "sh", "-c", bb.fmt(
                "bin=\"$(cd \"$(dirname \"$0\")\" && pwd)/$(basename \"$0\")\"; cd '{s}' && " ++
                    "\"$bin\" shale/tools/doc_check.shale self_test=1 && " ++
                    "\"$bin\" --budget arena_bytes=2000000000 --budget steps=1000000000 --budget reads=1000000 --budget read_bytes=2000000000 --budget output_bytes=8000000 shale/tools/doc_check.shale \"$@\"",
                .{dir},
            ) });
            r.addFileArg(bin);
            r.has_side_effects = true;
            r.stdio = .inherit;
            return r;
        }
    }.run;

    // **THE APPLICATIONS, BUILT ON THIS TREE** (the box's essay "checks that
    // stopped checking", 2026-10-17): `zig build consumers` runs each
    // application's own checks against this checkout (they name it by
    // path, `../shale`), one at a time, cheapest first. A sibling that is
    // not there fails the step by name. Not in `test`: it needs the
    // siblings, and minutes. Run before a Shale change is pushed to master.
    const consumers = b.step("consumers", "This tree's own docs checked, then each application's own checks against this tree: metal-vmm's queue tool, dog-park's tests, the kernel's gopher.elf, angry-gopher's ops/check_zig (needs the sibling checkouts)");
    const own_docs = docCheck(b, runner.getEmittedBin(), parent);
    own_docs.addArg("only=shale");
    var prev: ?*std.Build.Step = &own_docs.step;
    // **metal-vmm's queue tool, by this tree's own `shale`** (metal-vmm
    // QUEUE 233): its Shale tests on its fixtures, first, as the cheapest.
    {
        const dir = b.pathJoin(&.{ here, "..", "metal-vmm" });
        const run = b.addSystemCommand(&.{ "sh", "-c", b.fmt(
            "bin=\"$(cd \"$(dirname \"$0\")\" && pwd)/$(basename \"$0\")\"; " ++
                "cd '{s}' 2>/dev/null || {{ echo 'consumers: no metal-vmm beside this checkout ({s})'; exit 1; }}; " ++
                "echo '==> metal-vmm: shale .shale/queue_test.shale'; \"$bin\" --budget output_bytes=8388608 .shale/queue_test.shale > /dev/null",
            .{ dir, dir },
        ) });
        run.addFileArg(runner.getEmittedBin());
        run.has_side_effects = true;
        run.stdio = .inherit;
        if (prev) |p| run.step.dependOn(p);
        prev = &run.step;
    }
    for ([_]struct { repo: []const u8, cmd: []const u8 }{
        .{ .repo = "dog-park", .cmd = "zig build test" },
        .{ .repo = "zig-metal-unikernel", .cmd = "zig build gopher" },
        .{ .repo = "angry-gopher", .cmd = "./ops/check_zig" },
    }) |c| {
        const dir = b.pathJoin(&.{ here, "..", c.repo });
        const run = b.addSystemCommand(&.{ "sh", "-c", b.fmt(
            "cd '{s}' 2>/dev/null || {{ echo 'consumers: no {s} beside this checkout ({s})'; exit 1; }}; " ++
                "echo '==> {s}: {s}'; env -u ZIG_PROGRESS {s}",
            .{ dir, c.repo, dir, c.repo, c.cmd, c.cmd },
        ) });
        run.has_side_effects = true;
        run.stdio = .inherit;
        if (prev) |p| run.step.dependOn(p);
        prev = &run.step;
    }
    consumers.dependOn(prev.?);

    // Every repo's docs, or one's (`-- only=<repo>`, each repo's gate);
    // `-- at=dir/ show=N` pass through.
    {
        const run = docCheck(b, runner.getEmittedBin(), parent);
        run.addPassthruArgs();
        b.step("doc-check", "Every cited path in the repos' docs resolves and every cited name is there; `-- only=<repo>` for one repo's (tools/doc_check.shale; needs the sibling checkouts)").dependOn(&run.step);
    }
}
