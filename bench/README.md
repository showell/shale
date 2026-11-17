# bench: Shale's speed baseline

`bench/baseline.shale` measures Shale where its speed matters, so C Shale
(the default) can be judged against the Zig Shale it replaced, and each
change to it against the last, on the same inputs:

- **the runtime:** ten workloads in `bench/workloads/`, each run as a fresh
  process of the binary under test, five times, with the median wall and
  CPU time kept. These are cross-file greps (a literal and a regex over
  seven repos, about 3,300 files), the Roc name hunt, `edits_all` over
  about 500 files, `re_lines` over 16 MB, `json_read` and `json_write` of
  a 7.5 MB document, startup (50 launches), `run` overhead (200 runs of
  `true`) and a pure interpreter loop;
- **the build:** release, debug and the tests' build, each **cold** and
  **incremental** (one comment line appended to a core file, then the same
  command again), as `c/build.shale` makes them:
  - **release:** `c/shale.c` at GCC `-O2`, by the command block's own
    compile line (`shale c/build.shale binary=release`, which runs
    `c/build/binary.shale`: the preludes generated, then the compile), into
    `<prefix>/bin/shale`, the binary the runtime workloads then run;
  - **debug:** the same under TCC (`binary=tcc`);
  - **test:** the fast tier (`shale c/build.shale`), which builds **and
    runs** every C test, where the Zig's `zig build quick` only compiles
    its tests: so C's test row is not the same work as the Zig's.

  **C has no build cache**, so its cold and incremental builds are the same
  compile from nothing. Both rows are kept, with the same command (the
  incremental one after a comment line in `c/script/vm.c`), so a C TSV has
  the Zig's rows and the two line up row by row.

Each run writes one TSV, `bench/baselines/<date>-<commit>-<label>-<part>-<clean|notclean>.tsv`, where `<label>` is `label=`, or `built-c` or `built-zig` for a binary the run built, or the binary under test's folder and name, so two binaries measured at one commit never share a file (a file already there stops the run).
Its `#` lines say what the box was (its load before and after, heavy
processes, the CPU, the kernel, the implementation, the compilers, the
source's commit and whether it was dirty), and each row gives a workload's
medians. For a
workload that runs programs, `shale_*` is Shale's own part and `child_*`
the programs': `run_true` adds up `run`'s own measure of each `true`, and
`hunt_names` takes roc-ast's wall time from its `--verbose` report and
roc-ast's CPU from a separate run of roc-ast alone over the same files.
`json_write` parses the document, then writes it twice, so one write is
(json_write - json_read) / 2. In `run_true`, `run`'s wall time per program
is mostly Shale's own spawn and wait, and `true`'s CPU rounds to 0 ms a
run, so its `child_cpu_ms` is 0 and its `shale_cpu_ms` is everything.

**Fail, never record.** Every workload prints a line of what it found,
which must be the same in each repetition and equal to what today's Zig
Shale prints on the pinned inputs (`EXPECT` in the harness). A workload that
exits nonzero, prints no such line or answers differently stops the run,
and no TSV is written. The same applies to a build that fails.

**The inputs are pinned.** The seven repos are cloned at the revisions in
`PINS`, and the large text file and the JSON document are made from them,
all under `<build>/bench/inputs` (outside the repo, `<build>` being the `build:` mount). The workloads run with
a mounts file of their own (`cookbook` is the pinned Shale's, `tools` the
box's) and no user prelude. So a run months later reads the same bytes. The
build clones the source's HEAD under `<build>/bench/src` (for the Zig, with
zig-webbox beside it) and never builds in the checkout. A fresh clone has
all the C build needs: `c/out/gen/preludes.h` is not tracked, and the build
writes it from `src/`'s preludes.

## A dry run

    cd ~/showell_repos/shale && shale bench/baseline.shale

This runs on a busy box, stamps every row `not clean`, and prints the
reasons a clean run would be refused. For C Shale on the busy box on
2026-11-17, the builds took about 80 seconds (the two release compiles
most of it) and the runtime half about a minute; with `impl=zig` the builds
took about 25 minutes. Its numbers show that the harness works; they
are not a baseline. `bench/baselines/2026-11-17-0e25229-notclean.tsv` is
one, kept as the format's example.

## Taking the real baseline on a clean box

`clean=1` refuses to start unless the box is quiet: the 1-minute load at
or below 0.30, and none of `zig`, `roc`, `roc-ast`, `roc-types`, `node`,
`chrome`, `cc`, `make`, `claude` or another `shale` running (an idle
`shale --reap` for a job is counted and listed, not refused). So, first:

1. **Stop the agents.** End every Claude Code session but the one that will
   start the run, and every cloud or lane agent working on the box. The
   session that starts it should do nothing else until it ends.
2. **Stop the services and the browser:** the essay surface's server,
   Chrome (the browser automation's), any `zig build serve` or dev server,
   and any quality gate or tier still running in a lane.
3. **Wait for the load to fall.** `uptime` should show the 1-minute load
   at or below 0.30; it takes a few minutes after a build.
4. **Run it,** from a Shale checkout at the commit to measure:

        cd ~/showell_repos/shale && shale bench/baseline.shale clean=1

   It refuses, naming why, if the box is not quiet. Otherwise it builds,
   runs every workload five times and writes
   `bench/baselines/<date>-<commit>-<label>-<part>-clean.tsv`. Commit that file.
5. **Restart** the services and the agents.

## Measuring the Zig Shale

`impl=zig` measures the Zig Shale, until it is deleted: its builds are
`zig build` (release), `zig build script` (debug) and `zig build quick`
(the tests, compiled only), each in a cache of its own, with zig-webbox
cloned beside the source and `src/script/interp.zig` the incremental
build's file:

    cd ~/showell_repos/shale && shale bench/baseline.shale impl=zig clean=1

`IMPLS` in the harness holds each implementation's defaults, and every one
is an argument too, so a build is changed with no edit to the harness.
Values with spaces go on stdin:

    cd ~/showell_repos/shale && shale bench/baseline.shale --args-stdin <<'ARGS'
    impl=zig
    build_test={zig} build test --cache-dir {cache} --prefix {prefix}
    ARGS

`{src}`, `{cache}`, `{global}`, `{prefix}` and `{zig}` are filled in for
each build; `{cache}` and `{global}` are empty folders for a cold build and
the same folders for the incremental one. `{global}` is each build's
`ZIG_GLOBAL_CACHE_DIR` too, since Zig 0.17's `zig build` takes no flag for it. `built=` is the release binary
(`<prefix>/bin/shale`), which the runtime workloads then run;
`shale=<path>` with `part=runtime` measures a binary already built instead,
and `part=build` builds only. Compare two TSVs row by row: the inputs are
the same, so a row's check is the same too (the test row aside, which is
not the same work in the two).
