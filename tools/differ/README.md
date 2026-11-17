# differ: a new Shale judged against today's

`tools/differ.shale` runs every input Shale's own checks hold it to under
two binaries and compares them from the outside. The language is frozen
byte for byte, so today's Zig Shale is the oracle and a rewrite (the C
port) must match it in every output:

    cd ~/showell_repos/shale && shale tools/differ.shale old=<path> new=<path>

`old` and `new` are Shale paths to two `shale` binaries (a mount's, like
bench/baseline.shale's `shale=`). The script's header lists the rest of its
arguments: `only=`, `predict=`, `keep=1`, `reuse=1` and `list=1`.

## The inputs

The harness collects them as it starts, so an input added to a check is in
the next run with no list to keep:

- **zig-tests and prelude-tests:** every Shale source a Zig test hands to a
  runner helper (`expectOutput`, `expectStop`, `once` and the others in
  `RUNNERS`), as a string literal or a `\\` block. The three preludes' test
  files give `prelude-tests`.
- **docs:** every `shale` and `shale-linux` block of docs/SHALE.md and
  docs/SHALE-AGENTS.md.
- **cookbook:** every recipe that reads `arg("self_test"`, run with
  `self_test=1`, as `zig build cookbook-test` runs it.
- **lessons:** web/lessons.js. Input N is every cell of lessons 1 to N, run
  in order in one folder, since a lesson starts from the files the earlier
  ones made.
- **stops:** `tools/differ/stops/`, one script for each stop class and budget. Once
  docs/SURFACE.txt lands, a generator writes one script for each of its
  stop templates under the `build:` mount (differ/surface), and those are
  collected the same way.
- **cli:** `tools/differ/cli/`, the command line's own paths. A `-- differ-argv:` line
  gives the words after the binary (`{script}` is the file's own path), a
  `-- differ-stdin:` line one line of stdin, and `-- differ-stdin-self: 1`
  the file itself on stdin.

An input that holds `proc_stop(`, `http(`, `http_get(` or `keep_on_stop` is
skipped and listed with its reason. Each one reaches past the box's own
folders: another process, the network, or a job that outlives the run. A
source that two tests share runs once.

## A run

Each input runs three times: under `old` twice, then under `new`. Every run
is in a fresh folder at one fixed path, with one mounts file (`cookbook`,
copied from this checkout; `tools`; `scratch`, a folder beside the root)
and one environment. `SHALE_NOW` pins `now()`, git's dates and identity are
pinned, there is no user prelude, and HOME and PATH are fixed. The binary is
copied to one path for each phase, so a message that names it is the same.

The harness compares each step's ending, its stdout and its stderr (the
`── used:` line is part of stderr), and every file the run left, by kind,
mode and bytes. Only the digits of a `── time:` line are let go.
If the two `old` runs differ, the input is **nondeterministic**: it is
reported as a finding of the harness and never judged. An input that fails
runs under `old` a third time before the failure counts, since two runs can
agree by chance.

The report goes to the console and to a TSV in the `build:` mount
(`differ/reports/<time>.tsv`), with one row for each input. The console
groups the failures by the area most likely responsible and gives each
failure's first differing line. The area is a guess: a stop's class names
the lexer and parser, the VM or a builtin family, and a plain difference in
the output is laid at the most specific family the input calls.

## Proving it can see

`predict=<literal>` holds the failures to a prediction: an input should fail
exactly when the oracle's output holds the literal. To check a plant, build
a copy of Shale with one message changed, run it as `new`, and predict the
original text. The report counts the inputs that failed as predicted, the
ones predicted that passed, and the ones that failed without a prediction.

## What the oracle does not repeat

Two causes make inputs nondeterministic under today's Zig Shale alone, so
the harness cannot judge those inputs:

- **`fresh_dir`'s random names** (`mktemp`'s `XXXXXX`), which five cookbook
  self-tests print or leave on disk: remove_lane, retire_checkout,
  rewrite_method_calls, survey_repos and zig/check_const_unchanged.
- **The `── time:` line beyond its digits.** It appears only past a second
  spent in programs, and it names the slowest three in the order of their
  times. A run near a second, or with two programs close in time, differs
  from one run to the next (roc/check_foreign_code, rewrite_method_calls).
