# differ: a new Shale judged against today's

`tools/differ.shale` runs every input Shale's own checks hold it to under
two binaries and compares them from the outside. The language is frozen
byte for byte, so today's Zig Shale is the oracle and a rewrite (the C
port) must match it in every output:

    cd ~/showell_repos/shale && shale tools/differ.shale old=<path> new=<path>

`old` and `new` are Shale paths to two `shale` binaries (a mount's, like
bench/baseline.shale's `shale=`). The script's header lists the rest of its
arguments: `only=`, `predict=`, `keep=1`, `reuse=1`, `list=1` and `check=1`.

**`base=<folder>`** puts the work, the kept runs and the reports there in
place of `build:differ`, so two runs at once (two lanes) share nothing.

**`check=1`** runs every step with `--check` before its own words, so a
binary that only parses and reports, as the C port's first phase does, is
judged on that alone: what the check prints on stderr and how it exits.

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
mode and bytes (a git index by what it stages, below). Every run shares
the `build:` mount's `differ/` folder, so one runs at a time: a run that
finds another `tools/differ.shale` running stops before it touches
anything. The `── time:` line is let go whole: its numbers, whether
it is printed, and the order it names programs in (Steve, 2026-11-17).

**Arena bytes are outside the frozen surface** (docs/C-SEAMS.md,
"Fidelity"), so an input that observes them is **judged by class**: its
oracle stops on the arena budget, or its source reads `arena_bytes` outside
a comment. The harness finds these from the run and the source, never from
a list. Its stop's class and message are compared still; the digits of its
`── used:` line and its stop's `(N steps)` are let go, and, when it reads
`arena_bytes`, every digit of its output. The summary counts them and names
each one.

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

## What the oracle does not repeat, and how it is judged anyway

Three things the oracle's runs do not repeat from one run to the next, and
how the harness lets each go, so the inputs they touch are still judged:

- **`fresh_dir`'s random names.** The Linux prelude names its folder
  `<prefix>.XXXXXX`, mktemp's six characters from `[A-Za-z0-9]`. For the
  prefixes an input's source passes `fresh_dir` as string literals, each
  such name is masked as `<prefix>.<fresh N>` in stdout, stderr, the files'
  paths and their bytes, N counting the names in the order one run first
  shows them, so two folders stay two. Five cookbook self-tests were
  unjudged for it: remove_lane, retire_checkout, rewrite_method_calls,
  survey_repos and zig/check_const_unchanged.
- **The `── time:` line.** It appears only past a second spent in programs
  and names the slowest three in the order of their times, so it is let go
  whole (roc/check_foreign_code and rewrite_method_calls were unjudged for
  it on some runs). It is taken out before the `fresh_dir` names are
  masked: a child run's time line can name its folder, and on a busy box
  it numbered one side's names ahead of the other's, a false failure of
  rewrite_method_calls.
- **A git index's stat cache.** git writes `<repo>/.git/index` (and a linked
  worktree's `<repo>/.git/worktrees/<name>/index`) with each tracked file's ctime
  and mtime to the nanosecond, its device and inode, and a checksum of the
  whole, so its bytes differ from run to run. Such a file is compared by
  what it stages, `git ls-files --stage`: each path's mode, object and
  stage. Every other file under `.git` is still compared by its bytes, the
  objects and refs among them. remove_lane and zig/check_const_unchanged
  were unjudged for it. `shale tools/differ/plants/git_index.shale
  shale=<path>` is the plant: an index written again with the same entries
  passes, and one staging another blob, the work tree left alone, fails on
  the index.
