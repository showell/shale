//! Builds Shale: the modules an application imports (never by path), the
//! `shale` CLI, and the tools and tests around them. `zig build test` runs
//! the unit tests.

const std = @import("std");

/// The files of the tests named `expensive ...`, and the only ones
/// (`zig build fuzz`). src/script/hunt.zig imports exactly these (checked
/// below); src/script/hunt_guard_test.zig, handed the list, fails on such a
/// test anywhere else.
const hunt_files = [_][]const u8{
    "src/script/fuzz_test.zig",
    "src/script/lex_fuzz_test.zig",
    "src/script/props_test.zig",
    "src/script/builtins_hunt_test.zig",
    "src/script/gen_hunt_test.zig",
};
comptime {
    @setEvalBranchQuota(1_000_000);
    const root = @embedFile("src/script/hunt.zig");
    if (std.mem.count(u8, root, "\n    _ = @import(") != hunt_files.len) @compileError("src/script/hunt.zig imports other than build.zig's hunt_files");
    for (hunt_files) |path| {
        if (std.mem.indexOf(u8, root, "\n    _ = @import(\"" ++ path["src/script/".len..] ++ "\");") == null) @compileError("src/script/hunt.zig does not import " ++ path);
    }
}

/// The key modules `zig build script-coverage` measures: what the site runs,
/// held to 100% (C1-C6), then the Linux host, measured and reported (C7).
/// tools/coverage.shale groups them.
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

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // A script's effects all go through webbox's `store`, and its sources
    // take their `Io` from webbox's `host_io` (zig-metal-unikernel's build
    // hands the same sources the machine's).
    const webbox_dep = b.dependency("webbox", .{ .target = target, .optimize = optimize });
    const webbox = webbox_dep.module("webbox");
    const host_io = webbox_dep.module("host_io");

    // Two modules: `shale`, portable, which a site's code imports; and
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

    // The `shale` CLI (src/script_runner.zig): a script against a data
    // folder or a checkout, on Linux.
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

    // Times Shale's hot paths (src/script_bench.zig). No gate runs it.
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

    // The largest site each cookbook script fits (src/shale_scale.zig). No
    // gate runs it.
    const scale = b.addExecutable(.{ .name = "shale-scale", .root_module = b.createModule(.{
        .root_source_file = b.path("src/shale_scale.zig"),
        .target = target,
        .optimize = .safe,
        .imports = &.{.{ .name = "shale", .module = shale_release }},
    }) });
    b.step("shale-scale", "the largest site each cookbook script fits, under the site's budgets (ReleaseSafe)").dependOn(&b.addRunArtifact(scale).step);

    // Shale in a browser (web/): the portable module over an in-memory tree
    // (src/host_io_wasm.zig), and the page that edits the tree and runs
    // scripts against it. Installs zig-out/web/.
    // `atomics` for webbox's 64-bit change counter, which baseline wasm has no
    // instruction for.
    const wasm_target = b.resolveTargetQuery(.{
        .cpu_arch = .wasm32,
        .os_tag = .freestanding,
        .cpu_features_add = std.Target.wasm.featureSet(&.{ .atomics, .bulk_memory }),
    });
    const wasm_io = b.createModule(.{ .root_source_file = b.path("src/host_io_wasm.zig") });
    const wasm_webbox = b.createModule(.{
        .root_source_file = webbox_dep.path("src/webbox.zig"),
        .imports = &.{.{ .name = "host_io", .module = wasm_io }},
    });
    const wasm = b.addExecutable(.{ .name = "shale", .root_module = b.createModule(.{
        .root_source_file = b.path("src/shale_wasm.zig"),
        .target = wasm_target,
        .optimize = .ReleaseSmall,
        .imports = &.{
            .{ .name = "host_io", .module = wasm_io },
            .{ .name = "shale", .module = b.createModule(.{
                .root_source_file = b.path("src/shale.zig"),
                .imports = &.{
                    .{ .name = "host_io", .module = wasm_io },
                    .{ .name = "webbox", .module = wasm_webbox },
                },
            }) },
        },
    }) });
    wasm.entry = .disabled;
    wasm.rdynamic = true;
    const web = b.step("web", "Shale in a browser: zig-out/web/ (index.html and shale.wasm)");
    web.dependOn(&b.addInstallArtifact(wasm, .{ .dest_dir = .{ .override = .{ .custom = "web" } } }).step);
    web.dependOn(&b.addInstallDirectory(.{ .source_dir = b.path("web"), .install_dir = .{ .custom = "web" }, .install_subdir = "" }).step);
    const web_smoke = b.addSystemCommand(&.{"node"});
    web_smoke.addFileArg(b.path("web/smoke_test.mjs"));
    web_smoke.addArtifactArg(wasm);
    // What the test imports, so a change to either runs it again.
    web_smoke.addFileInput(b.path("web/shale.js"));
    web_smoke.addFileInput(b.path("web/sample.js"));
    web_smoke.addFileInput(b.path("web/lessons.js"));
    web_smoke.addFileInput(b.path("web/card.js"));
    web_smoke.addFileInput(b.path("web/highlight.js"));
    web_smoke.addFileInput(b.path("web/card.svg"));
    web_smoke.addFileInput(b.path("web/learn.html"));
    b.step("web-test", "the browser build's module under Node (web/smoke_test.mjs)").dependOn(&web_smoke.step);

    // The signer (src/shale_sign.zig): the key pair, made once, and a script
    // signed for a target.
    //   zig build shale-sign -- keygen [target] | pubkey [target] | sign <file.shale> <target> [minutes]
    //     | background <file.shale> <target> [minutes] | kill <target> [minutes]
    const signer = b.addExecutable(.{ .name = "shale-sign", .root_module = b.createModule(.{
        .root_source_file = b.path("src/shale_sign.zig"),
        .target = target,
        .optimize = optimize,
        .imports = &.{.{ .name = "shale", .module = shale }},
    }) });
    b.installArtifact(signer);
    const run_signer = b.addRunArtifact(signer);
    run_signer.addPassthruArgs();
    b.step("shale-sign", "the Shale signer: zig build shale-sign -- keygen | pubkey | sign <file.shale> <target> | background <file.shale> <target> | kill <target> (src/shale_sign.zig)").dependOn(&run_signer.step);
    const signer_test = b.addTest(.{ .root_module = signer.root_module });

    // shale-gate (tools/shale_gate.zig): the PreToolUse hook's classifier,
    // passing a `shale` launch or the escape and refusing the rest. Built
    // here; the hook is installed by hand.
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
    test_step.dependOn(&web_smoke.step);
    // The script fuzzer's count (src/script/fuzz_test.zig): a few hundred
    // sources by default, as many as asked for with -Dscript-fuzz.
    const fuzz_runs = b.option(u32, "script-fuzz", "how many random and mutated sources the script fuzzer runs (default 400)") orelse 400;
    // The expensive tests are for bug hunts, and no gate pays for them; each
    // bug a hunt finds becomes a targeted unit test. The tests named
    // `expensive fuzz: ...` or `expensive sweep: ...` live in `hunt_files`
    // alone, reached from a root of their own (src/script/hunt.zig, its
    // imports checked against that list), which `zig build test` compiles
    // and only `zig build fuzz` runs. Nothing is skipped: src/script/
    // hunt_guard_test.zig fails on a skip, or a hunt outside the list.
    const script_options = b.addOptions();
    script_options.addOption(u32, "fuzz_runs", fuzz_runs);
    const fuzz_step = b.step("fuzz", "The expensive fuzzes and sweeps (src/script/hunt.zig), for a bug hunt; no gate runs them");
    // -Dtest-filter=<text> runs a change's own tests in seconds, before the
    // whole suite.
    const test_filter = b.option([]const u8, "test-filter", "run only the tests whose names contain this");
    // -Dtest-timing (tools/timing_runner.zig) prints every test's wall time,
    // slowest first, so a suite's cost has names.
    const timing: ?std.Build.Step.Compile.TestRunner = if (b.option(bool, "test-timing", "print each test's time, slowest first") orelse false)
        .{ .path = b.path("tools/timing_runner.zig"), .mode = .simple }
    else
        null;
    // The program `run`'s tests spawn (src/shale_fixture.zig), so they never
    // depend on /bin/sh. Its path reaches the tests as
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
    // history and read its sources beside this repo's. It must be absolute:
    // the tests hand it to scripts that run `git -C` from elsewhere, where
    // addOptionPath's relative path (`../zig-webbox/.`) would not reach it.
    // We resolve it through webbox's own directory, since its
    // `root_dir.path` is relative to the top-level build, which is shale
    // only when shale is built alone.
    const webbox_root = webbox_dep.builder.root;
    shale_options.addOption([]const u8, "webbox_root", webbox_root.root_dir.handle.realPathFileAlloc(b.graph.io, webbox_root.subPathOrDot(), b.allocator) catch |e| std.debug.panic("shale: webbox's root {f}: {t}", .{ webbox_root, e }));
    // The unit tests read files as they run (each cookbook recipe, the
    // docs, tools/), which the build cannot list as inputs: a list made
    // when the build is configured is itself cached, and a file added since
    // would pass unread. So the unit runs always run.
    // Each unit binary twice: as `test` runs it, and Debug, whole and
    // under zig's own runner, for `script-coverage` (built only for it).
    // Zig 0.17's own backend emits no coverage for `--fuzz` (its report:
    // "corrupted coverage file ... pcs_len was zero"); LLVM's does. So
    // `zig build test -Dtest-llvm --fuzz=N` fuzzes the `std.testing.fuzz`
    // tests (241 D2); without --fuzz they run their corpus, as any test.
    const test_llvm = b.option(bool, "test-llvm", "build the unit tests with LLVM, which `--fuzz` needs in Zig 0.17") orelse false;
    const unit_roots = [_][]const u8{ "src/shale.zig", "src/shale_linux.zig" };
    var coverage_units: [unit_roots.len]*std.Build.Step.Compile = undefined;
    for (unit_roots, &coverage_units) |path, *for_coverage| for ([_]bool{ false, true }) |measured| {
        const unit = b.addTest(.{ .root_module = b.createModule(.{
            .root_source_file = b.path(path),
            .target = target,
            .optimize = if (measured) .debug else optimize,
        }), .filters = if (test_filter != null and !measured) b.dupeStrings(&.{test_filter.?}) else &.{}, .test_runner = if (measured) null else timing, .use_llvm = if (test_llvm and !measured) true else null });
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
            unit_run.has_side_effects = true;
            test_step.dependOn(&unit_run.step);
        }
    };
    // `zig build script-coverage` reports the lines no unit test runs: the
    // coverage SDK's `linecov` over the two unit binaries,
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
    // error (a skip among them) fails it, and it says how many ran. The hunts
    // over generated scripts say what they reached by the coverage SDK's
    // properties (241 D), so the hunt needs the SDK, resolved as
    // `script-coverage` resolves it; without it, `fuzz` says so and `test`
    // does not compile the hunt.
    if (!b.isRoot()) {} else if (!sdk_there) {
        fuzz_step.dependOn(&b.addFail("fuzz: no ../zig-coverage-sdk beside this checkout").step);
    } else if (b.lazyDependency("zig_coverage_sdk", .{})) |sdk| {
        const hunt = b.addTest(.{ .root_module = b.createModule(.{
            .root_source_file = b.path("src/script/hunt.zig"),
            .target = target,
            .optimize = optimize,
        }), .test_runner = .{ .path = b.path("tools/hunt_runner.zig"), .mode = .simple } });
        // The script module as a module: by path, the hunt would hold its tests.
        hunt.root_module.addImport("shale", shale);
        hunt.root_module.addImport("script_test_options", script_options.createModule());
        hunt.root_module.addImport("coverage", sdk.module("coverage"));
        hunt.root_module.addImport("explore", sdk.module("explore"));
        // Compiled by `test`, so a hunt cannot rot between expeditions.
        test_step.dependOn(&hunt.step);
        fuzz_step.dependOn(&b.addRunArtifact(hunt).step);
    }
    test_step.dependOn(&b.addRunArtifact(signer_test).step);

    // Checks the docs' cited paths and names: tools/doc_check.shale, by this
    // tree's own `shale`, from the folder that holds the repos, its fixtures
    // first, then the docs. Fails on any finding. Each repo's gate checks
    // its own (`only=<repo>`, through `zig build doc-check -- only=<repo>`
    // here), never another's.
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

    // `zig build consumers` runs each application's own checks against
    // this checkout (they name it by path, `../shale`), one at a time,
    // cheapest first, so a check downstream cannot quietly stop checking
    // this tree. A sibling that is not there fails the step by name. Not in
    // `test`: it needs the siblings, and minutes. Run it before a Shale
    // change is pushed to master.
    const consumers = b.step("consumers", "This tree's own docs checked, then each application's own checks against this tree: metal-vmm's queue tool, dog-park's tests, the kernel's gopher.elf, angry-gopher's ops/check_zig (needs the sibling checkouts)");
    const own_docs = docCheck(b, runner.getEmittedBin(), parent);
    own_docs.addArg("only=shale");
    var prev: ?*std.Build.Step = &own_docs.step;
    // metal-vmm's queue tool, by this tree's own `shale`: its Shale tests on
    // its fixtures, first, as the cheapest.
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
