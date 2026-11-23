# roc_platform: Roc scripts on C Shale

**Long-lived Shale recipes may be written in Roc**, on this platform: typed,
compiled, tested, and confined exactly as a Shale script is. One-offs stay
in Shale. Its surface is generated from Shale's builtin table.

## How it works

- **The platform** (`platform/Shale.roc`) gives a Roc app Shale's effects:
  files, globs, `run!`, `print!`, `arg!`, `fail!`, `roc_tree!` and the
  rest, each listed there with its type.
  - A Roc app reaches the system only through the platform, so every effect
    it makes goes through one of these.
  - **Only effects are exported:** lines, sorting and text are Roc's own.
    **The one exception is the regular expressions** (`re_find!` and its
    kin), which Shale supplies because Roc has none.
  - A hosted function is effectful in Roc, so a Roc function that calls one
    is effectful too.
- **The host** (`host/host.c`) is C Shale itself. It includes `c/shale.c`
  whole, so the command line, the mounts, the preludes, the budgets, the
  report and the exit code are `shale`'s. A hosted call goes like this:
  1. The Roc runs on a ucontext stack of its own and calls a hosted
     function, which is a shim.
  2. The shim writes its arguments and switches to Shale's stack.
  3. A small Shale driver, a loop that asks the Roc for its next effect
     (`host("roc_step", answer)`), does it with Shale's own builtin.
  4. Shale switches back, and the shim returns the answer.

  So the builtins written in Shale (`run`, `glob`, `grep_all`) keep their
  policy in one home, the preludes.
- **A refusal is a stop, and an outcome is a value.** A path out of the
  root, a program `run` refuses or a budget run out stops the run, in
  Shale's own words. A missing file is `Missing`, and a program's exit code
  is a field. A keep is a tag (`Head(n)`, `Tail(n)`, `Whole(n)`), and
  `run!`'s timeout is required.
- **Roc's memory is an arena of Shale's** (`c/foundation/arena.h`), held to
  the run's `arena_bytes`: `roc_dealloc` frees nothing, and a refusal stops
  the run as `budget arena`. `roc_crashed` stops it with Roc's message.
- **The Roc's own computation has a CPU budget**, `roc_cpu_ms` (`## budget:
  roc_cpu_ms=N` in the script, or `--budget roc_cpu_ms=N`). Steps count
  Shale's work, so the Roc's is timed instead: a timer on the thread's CPU
  clock, armed while the Roc runs, stops a loop that never calls Shale. A
  runaway stops as `budget cpu`, at its own line, and the `── used:` line
  gives the Roc's part.
- **A stop made while the Roc waits names the Roc's file and line**: a
  hosted call Shale refused, a `fail!`, a budget run out answering one, a
  `crash`, the CPU budget. For example: `` ── stopped at
  roc_platform/examples/Reach.roc:10:7 (in `Shale.read!`): argument: ...
  ``. The place is found so:
  1. The launcher links the program with a table of the lines of the
     script's files (`app.sites`), made from the pinned Roc's DWARF line
     table, which names a row's file by its module.
  2. Each shim keeps its return address and its own frame.
  3. The host walks the frame-pointer chain up from that frame, at most 8
     frames, to the first return address whose line holds the call's own
     text (`Shale.read!(`, `crash`). Roc 0.1.0_RC1 calls each hosted
     function from a wrapper of its own, so the shim's return address alone
     names the wrong line.
  4. When the walk finds no such line, the return address alone gives the
     line.
  5. The column is the call's own text on that line, else the line's
     first.
- **A value no `Shale.arg!` reads stops the run** before anything is built,
  naming the ones the script reads, as Shale does for a script's `arg`.

## Generated from the builtin table

`c/builtins.txt` is the one home of Shale's builtins
(`c/docs/builtin-table.md`).

- An effect the table exports gives its Roc signature, which outcomes are
  values, and its driver clause.
- `c/gen_builtins.shale` writes four files from it: `platform/Shale.roc`,
  the hosted block of `platform/main.roc`, `host/hosted.gen.c` and
  `surface.gen.shale`. Change the table and run `shale c/gen_builtins.shale`;
  leave those four files to the generator.
- The C types are `roc glue`'s (`host/roc_platform_abi.h`, written by
  `tools/glue.shale` with the pinned Roc). Glue names some types by their
  order in Shale.roc, so a change to what is exported is three runs:
  1. `shale c/gen_builtins.shale`;
  2. `shale roc_platform/tools/glue.shale`;
  3. `shale c/gen_builtins.shale` again.
- The fast tier (`c/build/builtin_table.shale`) holds the generated files
  and glue's prototypes to the table, and has GCC read `host.c` with
  `-Werror`.

## Running a script

    shale roc_platform/run.shale script=roc_platform/examples/count_lines.roc file=README.md

Run this from the shale checkout's root. `run.shale`:

1. builds the script with the pinned Roc (`roc-version`) into an archive,
   Roc's dev build unless `opt=speed` asks for `--opt=speed`;
2. links the archive with the host's object, which is all of C Shale
   compiled once with `gcc -O2`;
3. runs the program in this root, or rooted at `in=` (an absolute folder).

More of how it runs:

- The host's object and each script's program are cached by content hash
  under `out/`.
- Each build runs in a job of its own (`out/jobs/`), so concurrent runs
  build a key once.
- A `## budget: NAME=N ...` line in the app's root sets its budgets.
- `run.shale`'s header has the rest of its arguments.

### A script of several files

`script=` names the app's root, and the launcher takes with it:

- **its own modules**, each a type module beside the file that imports it
  bare (`import Words` is `Words.roc`), followed in turn. A module reaches
  the system as the root does, through `import pf.Shale`: the platform is
  the only door, whichever file asks.
- **packages by a relative path** in the app's header (`lib:
  "../lib/main.roc"`, then `import lib.Plural`), as roc-chat imports
  roc-markdown: that `package [Plural] {}` file and the modules it exposes.

Packages are how the recipes share their modules:

- The cookbook's Roc recipes name `cookbook/roc/lib/main.roc` in their
  headers as `lib`, then `import lib.Tree`.
- The recipes over Shale's C name their splitter, `cookbook/c/items/main.roc`,
  as `items`.
- A package reaches no effect. What runs a program or reads a file (a
  self-test, a word list, the C itself) stays in each recipe, which hands
  the package the text. So a recipe that runs roc-types keeps a small
  `Types.roc` of its own, which runs it and hands its output to the
  package's `RocTypes`.
- A script's package imports no package of its own.

How the launcher reads the files:

- Every file is read through Shale's store, so a path out of the root stops
  the launch.
- Each file is copied into the cache (the root as `main.roc`, packages
  under `pkg/<name>/`, the header's paths made the cache's) and hashed into
  the key, so an edit to any module rebuilds the program and nothing else
  does.
- `Shale.arg!` reads are found in all of them.
- Two modules of one name are refused: Roc's line table names a file by its
  module, so a stop could not say which.
- The host's sites table numbers the files (`file K PATH`, and each row,
  call and line its `K`), so a stop in a module names that module's
  `file:line:col`.

### The cache

**A script's build prunes `out/`** of what no current key reaches:

- every host object but the current one;
- every build linked with another host's object;
- the script's own builds at other keys (each edit's);
- the logs of the jobs that made them.

A build whose job still runs is left alone. A build says what it was built
for in its `built_for` file, which pruning reads.

**`reads=inputs`** builds and runs nothing: it prints the real path of every
file the program is built from (the script's files, the platform, the
launcher, the host and the code git tracks in `c/`), one a line, for a
caller that keys a cache on a Roc recipe. A launcher hands its values on,
so `shale cookbook:roc/find_names_and_docs_that_say_too_little.shale
reads=inputs` asks the recipe itself, with no `const SCRIPT` to read.

## The examples and the self-test

The examples in `examples/`:

- `count_lines.roc`, the smallest app.
- `check_modules_stay_under_500_lines.roc`, a cookbook recipe in Roc,
  sorted with the cookbook's `lib` (`Text.sort_bytewise`). `tools/compare.shale`
  holds it line for line to its Shale original.
- `escape.roc`, which tries each guard and stops with Shale's own refusal.
  It is several files: `Reach.roc`, a module of its own that tries a guard
  too, and `kit/`, a package it names by a relative path.

**`shale roc_platform/self_test.shale`** is the policy proofs: each of
escape.roc's tries held to Shale's own stop or output (a refusal in
Reach.roc named at Reach.roc's line, a package's function run), then a
misspelled argument and a right one. `shale=` runs the launcher with
another binary, such as a checkout's own build under c/out/. It needs the
pinned Roc and builds the host's object, so it runs when a Roc build is
asked for, outside the tiers.

`tools/measure.shale` times the builds and the runs.

## The ports

The cookbook's recipes written in Roc are listed in
`cookbook/roc/EXAMPLES.txt`, each beside its Shale original and the commit
that replaced it, and shown side by side at
<https://roc.lynrummy.com/roc-on-shale/>.

- Each recipe keeps its name and arguments, and its `.shale` file is its
  launcher, which hands its values to `run.shale` in the checkout its
  cookbook is in. The launcher's header says how the Roc is built (dev, or
  `opt=speed`) and why.
- Each port's goldens were written from its Shale original on plants, and
  the `check_goldens.shale` in the port's folder holds the Roc to them. The
  full tier runs them with the cookbook's self-tests.

## Pitfalls (2026-11-23, Roc 0.1.0_RC1)

- `RocStr` is `bytes, capacity, length`, while `RocList` is `elements,
  length, capacity`.
- `roc build` takes only a relative platform path, so the launcher writes
  one.
- The dev archive's object lacks `.note.GNU-stack`, so it is linked with
  `-z noexecstack`; the `--opt=speed` archive is not PIE, so it is linked
  with `-no-pie`.
- Shale's `run_new_table` makes a table of no kind, which Shale reads as
  empty. A host-made list must set `TABLE_LIST`.
- TCC can't read glue's header (`__int128`), so the host is built by GCC.
- `module` and `with` are reserved words in Roc: neither names a record's
  field, and the parser's error points past them.

## Open edges (2026-11-23, Roc 0.1.0_RC1)

- A crash inside Roc's builtins, rather than at a `crash` in the script,
  names the driver's line: `roc_crashed`'s return address is theirs, and no
  line up the frame chain holds `crash`. So does any stop at an address
  outside the script's own lines.
- A stop names the hosted call and its line, but neither the Roc function
  it is in (Roc's DWARF names its functions by hash) nor the Roc calls
  that led there.
- The launcher's own `fail` adds a second stop line after the program's.
- The Roc's arena has its own budget, set to the run's `arena_bytes`,
  beside the values' arena.
- The host's cache key ignores uncommitted edits under `c/`.
- A stop in a package's module names the driver's line: the dev build's
  line table has no rows of a package's module (escape.roc's `kit/` has
  none). A package reaches no effect, so only a crash or the CPU budget can
  stop there.
- A build of under a second takes about 1.3 s of wall time: the launcher
  waits for its build job by `job_wait`'s polling, which doubles from 20 ms.
- `measure.shale` copies an example's root alone, so it times one-file
  examples only.
