# roc_platform: Roc scripts on C Shale

**Long-lived Shale recipes may be written in Roc**, on this platform: typed,
compiled, tested, and confined exactly as a Shale script is. One-offs stay
in Shale. It began as a spike (branch `roc-spike`, 2026-11-19); it is now a
component of Shale, and its surface is generated from Shale's builtin table.

## How it works

- **The platform** (`platform/`) gives a Roc app Shale's effects
  (`Shale.roc`): `read!`, `write!`, `exists!`, `list!`, `glob!`,
  `grep_all!`, `run!`, `print!`, `arg!`, `fail!` and `roc_tree!` (Roc's
  own parser's tree of each file, through roc-ast: a node is a record, its
  attributes a list of name and value). A Roc app has no FFI and makes no
  system call, so every effect it makes goes through one of these. **Only
  effects are exported:** what Roc does itself (lines, sorting, text) is
  Roc's. **The one exception is the regular expressions**, which Roc has
  none of: `re_find!`, `re_find_all!`, `re_lines!`, `re_match!` and
  `re_replace!`, Shale's own, a match a record (`at`, `text`, `groups`) and
  no match `NoMatch`. A hosted function is effectful in Roc, so a Roc
  function that calls one is effectful too.
- **The host** (`host/host.c`) is C Shale itself: it includes `c/shale.c`
  whole, so the command line, the mounts, the preludes, the budgets, the
  report and the exit code are `shale`'s. What it runs is a small Shale
  driver, a loop that asks the Roc for its next effect
  (`host("roc_step", answer)`), does it with Shale's own builtin and hands
  the answer back. Each hosted function is a shim: it writes its arguments,
  switches to Shale's stack (the Roc runs on a ucontext stack of its own),
  and returns the answer when Shale switches back. So the builtins written
  in Shale (`run`, `glob`, `grep_all`) keep their policy in one home, the
  preludes.
- **A refusal is a stop, and an outcome is a value.** A path out of the root,
  a program `run` refuses or a budget run out stops the run, in Shale's own
  words, and the Roc gets no answer. A missing file is `Missing`, and a
  program's exit code is a field. A keep is a tag (`Head(n)`, `Tail(n)`,
  `Whole(n)`), and `run!`'s timeout is required.
- **Roc's memory is an arena of Shale's** (`c/foundation/arena.h`), held to
  the run's `arena_bytes`: `roc_dealloc` frees nothing, and a refusal stops
  the run as `budget arena`. `roc_crashed` stops it with Roc's message.
- **The Roc's own computation has a budget of CPU time**, `roc_cpu_ms`
  (5000 unset; `## budget: roc_cpu_ms=N` in the script, or `--budget
  roc_cpu_ms=N`). Steps count Shale's work, and the Roc's is no Shale step,
  so it is timed: the thread's CPU clock is read as the Roc starts and as it
  hands Shale each request, and a timer on that clock, armed while the Roc
  runs, takes a loop that never calls Shale off its stack. A runaway stops
  as `budget cpu`, at its own line, and the `── used:` line says the Roc's
  part (`, 400 of 400 Roc CPU ms`). CPU time, not wall time, as `run`'s
  `cpu_budget_ms` is: a busy box doubles a wall time.
- **A stop made while the Roc waits names the Roc's line**: a hosted call
  Shale refused, a `fail!`, a budget run out answering one, the CPU budget:
  `` ── stopped at roc_platform/examples/escape.roc:25:8 (in `Shale.read!`):
  argument: ... ``. Each shim keeps its return address; the launcher links
  the program with a table of the script's lines (`app.sites`, from the
  pinned Roc's DWARF line table, `objdump --dwarf=decodedline`), and the
  address's line is the stop's. Roc's table gives the line; the column is
  the call's own text on that line, found in the script, else the line's
  first. An address outside the script's own lines (Roc's builtins: a
  `crash`, which reaches the host through them, or an allocation inside
  `Str.repeat`) leaves the stop at the driver's line.
- **A value no `Shale.arg!` reads is refused**, before anything is built,
  naming the ones the script reads, as Shale refuses a value no `arg` of a
  script reads. The launcher names its values by value, so Shale's own check
  cannot see them; it reads what it was given with `arg_names()`, and the
  arg call's Roc name comes from the table (`surface.gen.shale`).

## Generated from the builtin table

`c/builtins.txt` is the one home of Shale's builtins (`c/docs/builtin-table.md`).
An effect it exports says its Roc signature, which outcomes are values and
its driver clause, and `c/gen_builtins.shale` writes from it:
- `platform/Shale.roc`, and the hosted block of `platform/main.roc`;
- `host/hosted.gen.c`: the driver, each shim (`shale_<name>`), its request
  as a Shale map, its answer as Roc's value, and its Roc name;
- `surface.gen.shale`: the Roc names the launcher finds in a script's text.

Never edit those four: change the table and run `shale c/gen_builtins.shale`.
The C types are `roc glue`'s (`host/roc_platform_abi.h`, written by
`tools/glue.shale` with the pinned Roc), so a change to what is exported is
three runs: `gen_builtins`, `tools/glue.shale`, `gen_builtins` again (glue
names some types by their order in Shale.roc). The fast tier
(`c/build/builtin_table.shale`) holds the generated files and glue's
prototypes to the table, and has GCC read `host.c` with `-Werror`.

## Running a script

    shale roc_platform/run.shale script=roc_platform/examples/count_lines.roc file=README.md

Run this from the shale checkout's root. `run.shale` builds the script with
the pinned Roc (`roc-version`, `--opt=dev`, or `--opt=speed` with
`opt=speed`) into an archive and links it
with the host's object. The host's object is all of C Shale, compiled once
with `gcc -O2` (about 25 s). The launcher then runs the program in this
root. The object and each script's program are cached by content hash under
`out/`. `timing=1` prints each phase's time. A `## budget: NAME=N ...` line
in the script sets its budgets.

The examples:
- `examples/count_lines.roc` reads a file, counts its lines and prints them.
- `examples/check_module_size.roc` is `cookbook/roc/check_module_size.shale`
  in Roc, with the same output on the same input (`self_test` left out).
  `tools/compare.shale` holds the two to each other, line for line.
- `examples/escape.roc` tries each guard (`try=climb|absolute|bash|write|crash|arena|loop|spin|missing|ok`).
  Each reach stops with Shale's own refusal.

**`shale roc_platform/self_test.shale`** is the policy proofs: each of
escape.roc's tries held to Shale's own stop (or, for `spin`, `missing` and
`ok`, to its output), then count_lines.roc given a misspelled value and a
right one. `shale=c/out/tcc/shale` runs the launcher with a checkout's own
build. It needs the pinned Roc and builds the
host's object, so it runs when a Roc build is asked for, not in the tiers.

`tools/measure.shale` times the builds and the runs.

## The ports

**`cookbook/roc/find_unclear_names/find_unclear_names.roc`** is the first
real port: cookbook/roc/find_unclear_names.shale in Roc, with its arguments,
its output, its stops, its exit code and its self-test (`self_test=1` runs
the original's plants against the port, each through this launcher). The
Shale original stays beside it. `compare_with_original.shale`, in the same
folder, holds the two to each other line for line and exit code for exit
code: each plant row, both self-tests, and roc-chat's app, roc-markdown's
package and roc-apps' Safari (`opt=speed`, `runs=N`).

Measured on 2026-11-19 (2 cores; the fastest of 3; the launcher's whole
wall time, the original's as `shale` runs it):

| tree | Shale | Roc, dev | Roc, `--opt=speed` |
|---|---|---|---|
| roc-chat app (153 files) | 0.73 to 0.76 s | 0.79 s | 0.55 s |
| roc-markdown package (28 files) | 0.17 to 0.18 s | 0.20 s | 0.13 s |
| roc-apps Safari (212 files) | 1.27 to 1.37 s | 1.35 s | 0.93 s |
| a plant (one file) | 10 to 15 ms | 30 to 40 ms | |

The port's own builds: 1.2 to 1.3 s for `--opt=dev` (cold or after a
one-line edit), 17 s for `--opt=speed`, beside the host's object once per
change to `c/` (25 to 50 s).

## What the spike measured

The measurements below are from 2026-11-19, on a box with 2 cores, with
builds at `nice 19`. Each time is the launcher's whole wall time.

| what | wall | notes |
|---|---|---|
| host object, once per change to `c/` or host.c | 25 to 49 s (21 to 32 s CPU) | `gcc -O2` of all of C Shale |
| count_lines: first build | 0.17 to 0.20 s | `roc build` 50 to 70 ms, link 80 to 95 ms |
| count_lines: cached run | 15 ms | the program 4 ms |
| count_lines: after a one-line edit | 0.16 s | |
| check_module_size: first build (Roc's cache cold) | 1.9 s | `roc build` 1.7 s |
| check_module_size: first build (Roc's cache warm) | 1.1 s | |
| check_module_size: cached run | 48 to 64 ms | the program 34 to 40 ms |
| check_module_size: after a one-line edit | 1.5 s | |
| check_module_size, the program alone, on roc-chat (147 files) | dev 36 to 47 ms; `--opt=speed` 12 to 14 ms | the Shale original 9 to 11 ms |
| `--opt=speed` build | 18 s | the archive is not PIE: link with `-no-pie` |
| peak memory of a cold `roc build` | 116 MB | GNU time |

**What held:** every layout. That includes a 72-byte record passed by
value, a tag union with payloads as an argument and as an answer, and a
`List` of records. The compiler did not crash once.

**Surprises:**
- `RocStr` is `bytes, capacity, length`, while `RocList` is `elements,
  length, capacity`.
- `roc build` refuses an absolute platform path, so the launcher writes a
  relative one.
- The dev archive's object has no `.note.GNU-stack`, so it is linked with
  `-z noexecstack`.
- Shale's `run_new_table` makes a table of no kind, which Shale reads as
  empty. A host-made list must set `TABLE_LIST`.
- TCC cannot read glue's header (`__int128`), so the host is GCC's alone.

## What is left

- **A crash names the driver's line**: `roc_crashed` is called through
  Roc's builtins, so its return address is theirs, not the script's.
- A stop names the hosted call and its line, not the Roc function it is in
  (Roc's DWARF names its functions by hash), nor the Roc calls that led
  there.
- The launcher's own `fail` adds a second stop line after the program's.
- The Roc's arena has its own budget, set to the run's `arena_bytes`,
  beside the values' arena.
- The host's cache key ignores uncommitted edits under `c/`.
- A script must be one file, and there is no helper library: a port writes its
  own text helpers (a `Text` module is undecided).
