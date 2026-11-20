# bench: Shale's speed baseline

`bench/baseline.shale` measures C Shale where its speed matters, so a
change can be judged against the last baseline on the same inputs. Its
header lists the arguments.

- **The runtime:** the workloads in `bench/workloads/` and the Roc name
  hunt, each run as a fresh process of the binary under test, with the
  median wall and CPU time of its repetitions kept.
- **The build:** release (GCC), debug (TCC) and the fast tier's test build,
  each cold and incremental (after one comment line appended to `touch`'s
  file), as `c/build.shale` makes them. C keeps no build cache, so the two
  are the same compile from scratch.

Each run writes one TSV under `bench/baselines/`, named for the date, the
commit, the label and the part, and stamped `clean` or `notclean`. Its `#`
lines describe the box (its load before and after, heavy processes, the
CPU, the kernel, the compilers, the source's commit and whether it was
dirty), and each row gives a workload's medians. For a workload that runs
programs, `shale_*` is Shale's own part and `child_*` the programs'.
`json_write` parses the document, then writes it twice, so one write is
(json_write - json_read) / 2.

**A wrong answer stops the run.** Every workload prints a `bench-ok` line
that must match in each repetition and equal the expected answer on the
pinned inputs (`EXPECT` in the harness). A workload or build that fails or
answers differently stops the run, and no TSV is written.

**The inputs are pinned.** The repos in `PINS` are cloned at fixed
revisions under the `build:` mount's `bench/inputs`, and the large text
file and JSON document are made from them. The workloads run with a mounts
file of their own and no user prelude, so a run months later reads the
same bytes. The build clones the source's HEAD under `bench/src` there and
leaves the checkout alone.

## A dry run

    cd <shale> && shale bench/baseline.shale

This runs on a busy box, stamps every row `not clean`, and lists what a
clean run would wait for. Its numbers show the harness works; a baseline
comes from a quiet box.

## The real baseline, on a quiet box

`clean=1` starts only when the box is quiet: the 1-minute load at or
below 0.30 and none of the heavy programs in `HEAVY` running. So, first:

1. **Stop the other agents and sessions**, and keep the one that starts
   the run idle until it ends.
2. **Stop the services and the browser**: dev servers, browser automation,
   and any tier still running.
3. **Wait for the load to fall** (`uptime`); it takes a few minutes after
   a build.
4. **Run it** from a Shale checkout at the commit to measure:

        cd <shale> && shale bench/baseline.shale clean=1

   It names what is busy if the box isn't quiet. Otherwise it builds, runs
   every workload and writes a `clean` TSV. Commit that file.
5. **Restart** the services and the agents.

## The build's commands

`BUILD` in the harness holds the build's defaults, and each is an argument
too. Values with spaces go on stdin:

    cd <shale> && shale bench/baseline.shale --args-stdin <<'ARGS'
    build_test=shale c/build.shale full=1
    ARGS

`shale=<path>` with `part=runtime` measures a binary already built;
`part=build` builds only. Two TSVs compare row by row, since the inputs
are the same.
