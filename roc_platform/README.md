# roc_platform: Roc scripts on C Shale

**Long-lived Shale recipes may be written in Roc**, on this platform: typed,
compiled, tested, and confined exactly as a Shale script is. One-offs stay
in Shale. Its surface is generated from Shale's builtin table.

## How it works

- **The platform** (`platform/Shale.roc`) gives a Roc app Shale's effects:
  files, globs, `run!`, `print!`, `arg!`, `fail!`, `roc_tree!` and the
  rest, each listed there with its type. A Roc app reaches the system only
  through the platform, so every effect it makes goes through one of
  these. **Only effects are exported:** lines, sorting and text are Roc's
  own. **The one exception is the regular expressions** (`re_find!` and
  its kin), which Shale supplies because Roc has none. A hosted function is
  effectful in Roc, so a Roc function that calls one is effectful too.
- **The host** (`host/host.c`) is C Shale itself: it includes `c/shale.c`
  whole, so the command line, the mounts, the preludes, the budgets, the
  report and the exit code are `shale`'s. It runs a small Shale driver, a
  loop that asks the Roc for its next effect (`host("roc_step", answer)`),
  does it with Shale's own builtin and hands the answer back. Each hosted
  function is a shim: it writes its arguments, switches to Shale's stack
  (the Roc runs on a ucontext stack of its own), and returns the answer
  when Shale switches back. So the builtins written in Shale (`run`,
  `glob`, `grep_all`) keep their policy in one home, the preludes.
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
- **A stop made while the Roc waits names the Roc's line**: a hosted call
  Shale refused, a `fail!`, a budget run out answering one, the CPU budget:
  `` ── stopped at roc_platform/examples/escape.roc:25:8 (in `Shale.read!`):
  argument: ... ``. Each shim keeps its return address; the launcher links
  the program with a table of the script's lines (`app.sites`, from the
  pinned Roc's DWARF line table), and the address's line is the stop's. The
  column is the call's own text on that line, else the line's first.
- **A value no `Shale.arg!` reads stops the run** before anything is built,
  naming the ones the script reads, as Shale does for a script's `arg`.

## Generated from the builtin table

`c/builtins.txt` is the one home of Shale's builtins
(`c/docs/builtin-table.md`). An effect it exports gives its Roc signature,
which outcomes are values and its driver clause, and
`c/gen_builtins.shale` writes from it `platform/Shale.roc`, the hosted
block of `platform/main.roc`, `host/hosted.gen.c` and `surface.gen.shale`.

Change the table and run `shale c/gen_builtins.shale`; leave those four
files to the generator. The C types are `roc glue`'s
(`host/roc_platform_abi.h`, written by `tools/glue.shale` with the pinned
Roc), so a change to what is exported is three runs: `gen_builtins`,
`tools/glue.shale`, `gen_builtins` again (glue names some types by their
order in Shale.roc). The fast tier (`c/build/builtin_table.shale`) holds
the generated files and glue's prototypes to the table, and has GCC read
`host.c` with `-Werror`.

## Running a script

    shale roc_platform/run.shale script=roc_platform/examples/count_lines.roc file=README.md

Run this from the shale checkout's root. `run.shale` builds the script with
the pinned Roc (`roc-version`; `opt=speed` for `--opt=speed`) into an
archive and links it with the host's object, which is all of C Shale
compiled once with `gcc -O2`. The launcher then runs the program in this
root, or rooted at `in=` (an absolute folder). The object and each
script's program are cached by content hash under `out/`, each built in a
job of its own (`out/jobs/`), so concurrent runs build a key once. A
`## budget: NAME=N ...` line in the script sets its budgets. `run.shale`'s
header has the rest of its arguments.

The examples in `examples/`: `count_lines.roc`, the smallest app;
`check_module_size.roc`, a cookbook recipe in Roc, held line for line to
its Shale original by `tools/compare.shale`; and `escape.roc`, which tries
each guard and stops with Shale's own refusal.

**`shale roc_platform/self_test.shale`** is the policy proofs: each of
escape.roc's tries held to Shale's own stop or output, then a misspelled
argument and a right one. `shale=c/out/tcc/shale` runs the launcher with a
checkout's own build. It needs the pinned Roc and builds the host's
object, so it runs when a Roc build is asked for, outside the tiers.

`tools/measure.shale` times the builds and the runs.

## The ports

**`cookbook/roc/find_unclear_names/find_unclear_names.roc`** is a recipe
in Roc: `cookbook/roc/find_unclear_names.shale` is its launcher. The
launcher hands its values to `run.shale` in the checkout its cookbook is in
(the `cookbook` mount's parent) with `in=` its own root, so the build is
the checkout's and the program runs where the caller is. That folder's
`check_goldens.shale` holds it, output and exit code, to goldens written
from the Shale original on the plants, and the full tier runs it with the
cookbook's self-tests.

## Pitfalls

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

## Open edges

- A crash names the driver's line: `roc_crashed` is called through Roc's
  builtins, so its return address is theirs. So does any stop at an
  address outside the script's own lines.
- A stop names the hosted call and its line, but neither the Roc function
  it is in (Roc's DWARF names its functions by hash) nor the Roc calls
  that led there.
- The launcher's own `fail` adds a second stop line after the program's.
- The Roc's arena has its own budget, set to the run's `arena_bytes`,
  beside the values' arena.
- The host's cache key ignores uncommitted edits under `c/`.
- A script is one file, with its own text helpers.
- A caller's cache key that follows a recipe's `use`s misses a change to
  the Roc script a launcher runs, or to the platform.
