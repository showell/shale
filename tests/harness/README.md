# The golden tests' harness

**The golden tests are the one judge of Shale's behaviour.** Every input
the harness collects is frozen in `tests/golden/` with the output a C
`shale` gave it, and a binary is held to that output byte for byte:

    cd ~/showell_repos/shale && shale tests/golden.shale [bin=<path>]
    cd ~/showell_repos/shale && shale tests/golden.shale freeze=1 [refreeze=<id>,<id>]

The run (`tests/harness/run.shale`) runs every case under `bin`
(`c/out/release/shale`, the C build under test, unset) in a work folder,
`base=`: unset, this checkout's own, `build:golden/<checkout's folder>-<8
hex digits of its real path's SHA-256>` (tests/golden.shale's
`checkout_base`), so two checkouts' runs never share one. It prints each
case that fails with its first differing line, then the count. A second
golden run with the same base stops before it touches anything. The C build's full tier runs it (`c/build.shale full=1`).
tests/golden.shale's header has the arguments of both.

**Its functions are a library**, `tests/harness/lib.shale`, which the run
and the freeze share. A library is read through a mount, so
tests/golden.shale launches each: it writes `c/out/golden.mounts`, naming
this checkout's `tests/harness` as `harness` beside the `build`, `tools`
and `scratch` mounts it was given, and runs the body under it.

The cookbook's self-tests are not frozen: the C full tier runs them live
under the binary (`c/build.shale`'s `cookbook_self_tests`, as Zig's
`cookbook-test` step does), so a recipe and its self-test change together.

## The inputs

The freeze collects them as it starts, so an input added to a check is
frozen by the next freeze with no list to keep:

- **zig-tests and prelude-tests:** every Shale source a Zig test hands to a
  runner helper (`expectOutput`, `expectStop`, `once` and the others in
  `RUNNERS`), as a string literal or a `\\` block. The three preludes' test
  files give `prelude-tests`.
- **docs:** every `shale` and `shale-linux` block of docs/SHALE.md and
  docs/SHALE-AGENTS.md.
- **lessons:** web/lessons.js. Input N is every cell of lessons 1 to N, run
  in order in one folder, since a lesson starts from the files the earlier
  ones made.
- **stops:** `tests/inputs/stops/`, one script for each stop class and
  budget.
- **cli:** `tests/inputs/cli/`, the command line's own paths. A
  `-- differ-argv:` line gives the words after the binary (`{script}` is
  the file's own path), a `-- differ-stdin:` line one line of stdin, and
  `-- differ-stdin-self: 1` the file itself on stdin. (The lines keep the
  name they were frozen with: a case holds its input's bytes.)
- **store:** `tests/inputs/store/`, probes of the store builtins' edges
  that the other inputs miss: edit's diffs and refusals, the reads at a
  file's edges, walk's options and its resume, edits_write's batches,
  symbolic links, the call-bytes cap, budget sweeps, mounts and a planted
  secrets mount. Each runs after `tests/inputs/store/fixture/setup.shale`,
  which builds the small tree the probes read.
- **git:** `tests/inputs/git/`, the git prelude's calls against a repo and
  a bare remote that each input makes in its own folder, under the run's
  pinned identity and dates.

**A hand-written input** (stops, cli, store) is one input for each of its
`-- differ-argv:` lines, so a probe at several budgets is several inputs of
one file: `store/store_budgets.shale` runs at every reads and changes value
and every 25 steps of its run, and `store/call_bytes.shale` at each cap on
either side of where an answer changes. Its origin ends with the argv's
words, `[--budget reads=3]`. Two more lines serve any of them:

- **`-- differ-before: <file>`** runs a script in the input's folder first,
  as a step of its own in the same work folder: a fixture.
- **`-- differ-mounts: <file>`** names a mounts file in the input's folder,
  whose mounts the input's own step gets beside the harness's (the fixture's
  step gets the harness's alone). Each line is `name = <path> [words]`, the
  path below the run's work folder with `root` or `scratch` first. The
  harness refuses any other path, `..`, a `tools` mount and the harness's
  own names, so an input's mount reaches only the run's fresh folders, never
  a real one: `store/store.mounts`'s secrets mount is a folder the fixture
  plants.

The ones that reach past the box's own folders stop before they do or
touch only their own: `http` and `http_get` inputs are refused before curl
runs, a `proc_stop` input stops only its own job or is refused a process of
root's, and the `keep_on_stop` input is a type error. A source that two
tests share is one input.

## A run

Every run is in a fresh folder at one fixed path, with one mounts file
(`cookbook`, copied from this checkout; `tools`; `scratch`, a folder beside
the root) and one environment. `SHALE_NOW` pins `now()`, git's dates and
identity are pinned, there is no user prelude, and HOME and PATH are fixed.
The binary is copied to one path, so a message that names it is the same.

A case's frozen output is each step's ending, its stdout and its stderr
(the `── used:` and `── steps:` lines are part of stderr), and every file
the run left, by
kind, mode and bytes (a git index by what it stages, below).

**Arena bytes are outside the frozen surface** (docs/C-SEAMS.md,
"Fidelity"), and so is the exact step of a `budget kept` or `budget
machine` stop, which hangs on the same layouts. An input that observes them
is **judged by class**: its frozen run stops on the arena, kept or machine
budget, or its source reads `arena_bytes` outside a comment. The freeze
finds these from the run and the source, never from a list, and says so in
the case's `case.json`. Its stop's class, place and message are held
still; the digits of its `── used:` and `── steps:` lines and its stop's
`(N steps)` are let go, and, when it reads `arena_bytes`, every digit of
its output.

## What a run does not repeat, and how it is held anyway

Three things a binary's runs do not repeat from one run to the next, and
how the harness lets each go, so the inputs they touch are still judged:

- **`fresh_dir`'s random names.** The Linux prelude names its folder
  `<prefix>.XXXXXX`, mktemp's six characters from `[A-Za-z0-9]`. For the
  prefixes an input's source passes `fresh_dir` as string literals, each
  such name is masked as `<prefix>.<fresh N>` in stdout, stderr, the files'
  paths and their bytes, N counting the names in the order one run first
  shows them, so two folders stay two.
- **The `── time:` line.** It appears only past a second spent in programs
  and names the slowest three in the order of their times, so it is let go
  whole. It is taken out before the `fresh_dir` names are masked: a child
  run's time line can name its folder.
- **A git index's stat cache.** git writes `<repo>/.git/index` (and a linked
  worktree's `<repo>/.git/worktrees/<name>/index`) with each tracked file's ctime
  and mtime to the nanosecond, its device and inode, and a checksum of the
  whole, so its bytes differ from run to run. Such a file is compared by
  what it stages, `git ls-files --stage`: each path's mode, object and
  stage. Every other file under `.git` is still compared by its bytes, the
  objects and refs among them.

## The golden cases

**The run's folder is padded to one length on every box.** A run's step
counts hang on the lengths of the paths it is handed (a prelude scans them;
18 cases moved when the base moved), so the run and the freeze each run in
a folder below their base whose real path is `WORK_PATH_BYTES` (160) bytes
long (`padded_base`, tests/harness/lib.shale): folders named by `p`s, 63 at
a time, since a store name holds at most 96. No symbolic link is used,
since the file system follows none. A base whose real path is past 158 bytes is
refused, naming the limit. So the goldens hold from GitHub's /home/runner,
a cloud's /root or a laptop alike.

**A case** is `tests/golden/<id>/`, its id from its input's origin, never a
run's numbering, so a regeneration diffs cleanly
(`src/script/interp_test.zig:192` is `zig-tests-script-interp_test-192`,
`tests/inputs/store/store_budgets.shale [--budget reads=3]` is
`store-store_budgets-budget-reads-3`). A doc block's id is its doc and the
first 8 hex digits of its text's SHA-256, never its line
(`docs-SHALE-AGENTS-1a2b3c4d`), so a paragraph added above it renames
nothing; two blocks of a doc whose digits collide take `-2`, `-3` in the
doc's order (`collect_docs`). Its origin in the manifest and `case.json`
keeps the line, as of the last freeze. A doc block whose text changes is a
new case, and its old one is gone: the freeze adds the new one and refuses
the loss until `refreeze=` names the old id.

A case holds:

- its files: a Zig test's or a doc block's source as `script.shale`, a
  lesson's cells as `cell-NN.shale`, a hand-written probe and its fixture by
  their own names, a step's stdin as `stdin-K.txt`;
- `case.json`: each step's argv (`{case}/<name>` for one of its files), its
  stdin's file and its own mounts (`mount_lines`); the `fresh_dir` prefixes
  its source passes; and how it is judged (`judge`, `arena`);
- `expected/`: the frozen run, masked: `meta` (each step's ending and what
  was left running), `K.out` and `K.err` for each step, and `files` (the
  files the run left, each regular file's SHA-256 after its line).

`tests/golden/manifest.tsv` lists the cases (id, source, origin, steps,
judged by class), and `tests/golden/left_out.tsv` the inputs not frozen and
why. Every case's run has the checkout's own cookbook as its `cookbook`
mount, for a case that `use`s a recipe.

**A case runs as it was frozen** (`launch`): in a fresh work folder, its
files copied to `<base>/case`, under the harness's mounts and pinned
environment. It is masked, and the golden file holds the masked form: the
`── time:` line out, `fresh_dir`'s names numbered, and, for an input judged
by class, the numbers `#`, so what is frozen is its stop's class, place and
message. Three masks more make a golden file the box's no more: the base's
path is `{base}` and the tools mount's `{tools}`, a folder's size in
`files` is `-` (the filesystem's), and a file's size is that of its masked
bytes.

## The freeze

    cd ~/showell_repos/shale && shale tests/golden.shale freeze=1 [refreeze=<id>,<id>]

It runs every input twice under `bin` (`c/out/release/shale` unset) as the
run does; an input whose two runs differ is nondeterministic and left out,
with the line they differ at, and so is each origin in
`tests/harness/freeze.shale`'s `LEFT_OUT`, with why; it collects no
cookbook self-test. It builds the new goldens in its padded folder under
its base (the run's, this checkout's own unset) first, and writes them only when no case's expected output
changed or went but those `refreeze=` names: **a re-freeze is a deliberate
act, a case at a time**, and its commit says why for each case. A new case
is added and counted. `only=` freezes the inputs whose origin holds a
literal, a trial into another `dir=`.
