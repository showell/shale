# roc_platform: Roc scripts on C Shale (a spike)

**This folder is a spike**, not part of Shale. It asks whether a Roc app can
be a Shale script: written in Roc, typed, compiled, and confined exactly as
a Shale script is. Nothing in `c/` depends on it, and the tiers don't build it.

## How it works

- **The platform** (`platform/`) gives a Roc app Shale's builtins as
  effects (`Shale.roc`): `read!`, `write!`, `exists!`, `list!`, `glob!`,
  `grep_all!`, `run!`, `print!`, `arg!` and `fail!`. A Roc app has no FFI
  and makes no system call, so every effect it makes goes through one of
  these.
- **The host** (`host/host.c`) is C Shale itself: it includes `c/shale.c`
  whole, so the command line, the mounts, the preludes, the budgets, the
  report and the exit code are `shale`'s. What it runs is a small Shale
  driver (`DRIVER` in host.c), a loop that asks the Roc for its next effect
  (`host("roc_step", answer)`), does it with Shale's own builtin and hands
  the answer back. Each hosted function is a shim: it writes its request,
  switches to Shale's stack (the Roc runs on a ucontext stack of its own),
  and returns the answer when Shale switches back.
- **A refusal is a stop, and an outcome is a value.** A path out of the root,
  a program `run` refuses or a budget run out stops the run, in Shale's own
  words, and the Roc gets no answer. A missing file is `Missing`, and a
  program's exit code is a field.
- **Roc's memory is an arena of Shale's** (`c/foundation/arena.h`), held to
  the run's `arena_bytes`: `roc_dealloc` frees nothing, and a refusal stops
  the run as `budget arena`. `roc_crashed` stops it with Roc's message.
- **The layout is Roc's**: `host/roc_platform_abi.h` is `roc glue`'s C
  header (CGlue.roc at the pinned compiler's commit), written by
  `tools/glue.shale`, and the shims use its types.

## Running a script

    shale roc_platform/run.shale script=roc_platform/examples/count_lines.roc file=README.md

Run this from the shale checkout's root. `run.shale` builds the script with
the pinned Roc (`roc-version`, `--opt=dev`) into an archive and links it
with the host's object. The host's object is all of C Shale, compiled once
with `gcc -O2`. The launcher then runs the program in this root. The
object and each script's program are cached by content hash under `out/`.
`timing=1` prints each phase's time. A `## budget: NAME=N ...` line in the
script sets its budgets.

The examples:
- `examples/count_lines.roc` reads a file, counts its lines and prints them.
- `examples/check_module_size.roc` is `cookbook/roc/check_module_size.shale`
  in Roc, with the same output on the same input (`self_test` left out).
  `tools/compare.shale` holds the two to each other, line for line.
- `examples/escape.roc` tries each guard (`try=climb|absolute|bash|write|crash|arena|missing|ok`).
  Each reach stops with Shale's own refusal.

`tools/measure.shale` times the builds and the runs.

## What the spike found

The measurements below are from 2026-11-19, on a box with 2 cores, with
builds at `nice 19`. Each time is the launcher's whole wall time.

| what | wall | notes |
|---|---|---|
| host object, once per change to `c/` or host.c | 33 to 49 s (32 s CPU) | `gcc -O2` of all of C Shale |
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

Lines of code: the port is 254 lines, 218 of them code. The original is 206
lines: 143 of code, or 106 without `self_test`. The extra lines are things
Shale has and Roc's builtins don't: a string sort (`byte_order`), `lines`
and `count_lines`.

**What held:** every layout. That includes a 72-byte record passed by
value, a tag union with payloads as an argument and as an answer, and a
`List` of records. The C was written to the glue header, and nothing needed
fixing. The compiler did not crash once.

**Surprises:**
- `RocStr` is `bytes, capacity, length`, while `RocList` is `elements,
  length, capacity`.
- `roc build` refuses an absolute platform path, so the launcher writes a
  relative one.
- The dev archive's object has no `.note.GNU-stack`, so it is linked with
  `-z noexecstack`.
- Shale's `run_new_table` makes a table of no kind, which Shale reads as
  empty. A host-made list must set `TABLE_LIST`.

**Gaps a real platform must close:**
- The Roc's own computation has no budget. Steps count only the driver's
  Shale, so a Roc loop that never ends is stopped only by the launcher's
  `timeout_ms`.
- A stop names the driver's line (`-e:31:5`), not the Roc's.
- The unread-argument check is lost: the launcher forwards only the names
  it finds in the script's `Shale.arg!("...")` calls.
- The launcher's own `fail` adds a second stop line after the program's.
- The Roc's arena has its own budget, set to the run's `arena_bytes`,
  beside the values' arena.
- The host's cache key ignores uncommitted edits under `c/`.
- A script must be one file.

## Generating the platform from Shale's builtin table

Shale's builtins are listed in four places:
- `c/script/ast.h`: the `Builtin` enum and each builtin's name;
- `c/script/vm.c`, `builtin_specs()`: each builtin's arity and C function;
- `c/preludes/*.shale`: the builtins written in Shale (`run`, `glob`,
  `grep_all`, the git prelude);
- `docs/SHALE.md`: each builtin's tables, its answer, its cost and its
  stops.

None of these has a type: Shale is dynamic, and its options are maps. So
generating the platform needs one new home, a typed table that lists, for
each builtin exposed to Roc:
- its Roc signature;
- which of its outcomes are values (a `Missing`, a code);
- the driver clause that calls it.

From that table, a generator would write `Shale.roc`, the `hosted` block in
`main.roc`, `DRIVER`'s clauses, and the shims. The shims need only a few
marshallers, one for each Roc type that crosses (`Str`, the integers,
`Bool`, `List(Str)`, records of those, `[Found(x), Missing]` and the keep's
tags). `roc glue` would then check the layout of what was generated.
