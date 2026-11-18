# The C port's seams

Shale was rewritten in C (Steve, 2026-11-17). The language is frozen byte
for byte, and the golden tests (`tests/golden.shale`), frozen from the Zig's
output, are its one judge. This page is the design
the agents porting it share: how the C represents a run, and where it meets
what is outside it. It holds no code. The C's style and its foundation (the
arena, `Str`, `vec`, `map`, err.h) are in c/docs/style.md and c/foundation,
and how the C is compiled is in c/docs/building.md.

**What the port must match** is [`SURFACE.txt`](SURFACE.txt): every
builtin, stop text, budget, flag and report line, generated from the Zig
by `tools/surface.shale` and gated by `zig build surface-check`. The golden
tests hold every input's output, report, stop and exit code. Where this page
and the goldens disagree, the goldens win.

## Values

- **A value is a tagged struct:** a tag (nil, boolean, int, string, table)
  and a union of `bool`, `int64_t`, a `Str` (pointer and length) and a
  `Table *`, laid out as the C sees fit (Fidelity, below, says why its size
  is free). A string is never changed once made, and the value points at
  its bytes in the values' arena.
- **A table is one struct for list and map:** its kind (empty, list, map),
  a list of values (vec.h), and a map from `Str` to value (map.h) that
  keeps the order keys were first set. `{}` takes its kind from its first
  entry, as in Zig.
- **Tables are shared by reference**, and `==` on them is refused, so a
  table needs no identity beyond its pointer. The yield's copy keeps "a
  table reached twice is one table" with a forwarding map from old pointer
  to new.
- **One map serves every table:** the values' maps and the C's own (the
  parser's names, the regex cache's keys) are all the foundation's map.h.
  Its allocations differ from Zig's `StringArrayHashMapUnmanaged`, which
  only moves where an arena stop lands, and that is outside the surface.
- **A `..` chain** keeps Zig's tail buffer: the last string a chain built,
  its used and its capacity, so that `s = s .. x` is linear, not
  quadratic.

## Fidelity: what the arena may change

Steve decided on 2026-11-17 that **arena byte counts are outside the frozen
surface.** Only 4 of the 1,024 inputs the port was judged on observe them,
so mirroring Zig's layouts would buy little: the C lays out its values,
tables, frames, ops and tree for itself, with no `_Static_assert` against a
Zig size and no Zig probe of them.

- **Outside the surface:** the bytes a run holds (`budget().arena_bytes`'s
  value, the report's arena and `── used:` numbers), and the exact step at
  which an arena stop lands. So are `budget kept` and `budget machine`'s
  exact places, which hang on the same layouts.
- **Inside it:** a run that stops on the arena budget under Zig stops on it
  under C, with the same class (`budget arena`), the same message, and in
  the same stage of the script wherever that is observable (what it printed
  first, which effects it made). Only the step count may differ.
- **The arena keeps Zig's shape anyway.** arena.h is `value.Budgeted`: its
  16-byte chunk header, `chunk_least`, `big`, doubling, and only the last
  allocation growing in place, so a budget means about what it means today.
  Steve confirmed (2026-11-17) that `machine_bytes` is outside the surface
  in the same way, so `Budgeted.heap()`'s twin, an allocator whose free
  gives the bytes back, is a nicety for the regex cache and a long call's
  arguments, not a fidelity requirement.
- **The golden tests judge these runs by class.** The freeze
  (`tests/harness/freeze.shale`) finds the inputs that observe the layouts
  mechanically (an arena, kept or machine stop, or a read of
  `arena_bytes`) and marks them in each case's `case.json`; the run compares
  their stop's class, place and message but not those numbers or the step
  count.

## The VM, the stops and the budgets

- **The C ports the VM, not the tree-walker.** The ops of
  src/script/compile.zig and the loop of src/script/vm.zig are its model:
  the same ops, the same enters charged in the walker's order and hung on the op after them, and a loop that never
  recurses. The walker stays Zig's, as its second implementation.
- **A stop is err.h's `RunCtx`.** Every function that can stop returns
  `Status`; `run_fail` and `run_fail_built` set the place, the class, the
  message (Zig's format text, its fallback when the message cannot be
  made), the function and the trail. `run_budget_stop` is
  `interp.Run.budgetStop`, and every refused allocation is `run_oom`, the
  arena budget, as Zig reads every `OutOfMemory`. Zig's `Run.fail` is
  `allocPrint(fmt, args) catch fmt` even with no arguments, so where the
  format escapes a brace (`{{`) the raw format is what a refused arena
  shows. A stop whose Zig format has an escape goes through `run_failf` (or
  the host's `path_fail`) with that format as its fallback, never
  `run_fail` with the finished text; test_vm.c holds every such site to the
  raw text under an arena that refuses everything.
- **The C has no background run.** Zig's process.zig and script.zig run a
  script as `.background` (`read_only`), stepped in slices while others
  write: its walk then skips an entry that vanished mid-walk rather than
  stopping (builtins_walk.zig:40, :238), and `admit` refuses every change,
  "a background run only reads" (builtins_change.zig:430). The C's CLI
  runs every script as an atomic run, so it ports neither; a C embedder that
  wants a background run must port both.
- **`attempt` is a frame marked with its answer**, as in src/script/vm.zig: a stop
  that is not a budget's unwinds to the innermost marked frame, cuts the
  call trail back to its depth, and fills `{ok = false, error, class, ...}`.
  A budget never unwinds.
- **The budgets are err.h's `Budgets`**, with the core's defaults; the
  CLI's own (output 64 KiB, call bytes 64 MiB) are set by the CLI, never the
  core. Each budget is checked where Zig checks it: steps at each enter and
  each charge, `depth` at each enter, the arena at each allocation, reads
  and changes at each store call. A charge past what is left stops one step
  past the budget, as `Run.charge` does.
- **Zig's formatting is the C's**: `{d}`, `{s}`, `{x:0>2}`, `{t}` (an
  error's or an enum's name) each get a buf.h writer that prints what
  Zig's does. Zig's error names that reach a message (`FileNotFound`,
  `AccessDenied`, ...) come from one table mapping errno to Zig's name.

## Before `attempt` and yield are ported

The foundation's containers hand out pointers that a growth frees (the
`DRAGON:` at arena.h's `arena_free`). Nothing in the C holds a
caller to drop them in time, so these tests are written, under ASan, before
`attempt` and yield are ported, and fail on a read of freed memory:

- a map key made in a buffer that later grows, or in an arena span later
  reset, then looked up (map.h's keys are held by pointer);
- a map value's `_ptr`, and an item's `&at(v, i)`, kept across puts and
  pushes that move a big buffer;
- a `buf_view` kept across an append past `ARENA_BIG`;
- a stop's trail and message read after a push, a pop, `attempt`'s cut of
  the calls and a reset of the machine's and the values' arenas (`run_fail`
  keeps a view of the live call vec, not a copy);
- a yield's copy, with everything it keeps read after the reset it makes.

## The secrets rule's check

A secrets mount's path must never appear in a message. **`path_fail`
(c/host/path_run.c) checks the arguments the host makes**: a stop built
there, which is every stop path_run.c and paths.c build from a path and
every one host_run.c's `host_fail` and `host_fail_plain` make (host_jobs.c
uses them too), asks the `PathHook`'s `names_secret` of each argument marked
as the host's (an OS path, a mount's folder), and one that names a secrets
mount's folder ends the process, a bug in the C, before any message is
made. The script's own text is not checked: it may come back. `--undo-on-
stop`'s lines are asked the same hook before they are said (c/shale.c).

**What no check sees:** a stop made by `run_failf` (zig_ast.c, host_call.c,
the VM's own), the `── ran` records, the time line and the `── stopped with
the run` line. Today these hold only the script's own text (its argv, its
paths as it wrote them) and fixed words, never a path the host made; a new
one that puts an OS path in any of them must go through `path_fail`
instead.

## The host interface

The core makes no system call. What zig-webbox and the Linux host give
today, the C gives as three vtables of function pointers, each returning
`Status` and leaving its error's Zig name for the message:

- **`Store`**, zig-webbox's `store`: list, stat, stat-or-null, has, read-at,
  replace (whole, never torn), append, make-dir, remove, remove-tree, and
  the bases a path is measured from. The C keeps the root's rules in the
  core: FAT names, no `..`, no absolute path, `""` the root. The C's Store
  is a port of webbox's Linux store. Whether metal (zig-metal-unikernel)
  ever takes the C, with a store of its own, is Steve's to decide; nothing
  here depends on it.
- **`PathHook`**, `interp.PathHook`: `resolve` (a mount's path to an OS
  path), `check_root` (a root path's own checks, once the core has held its
  every part to be a name), `may_change` (asked before every change: a
  read-only mount refuses, and the undo journal records the path's state),
  `absolute_hint` (what an absolute path's stop may say: the root's words
  alone, nothing named, or the mount to name), `names_secret` and
  `in_secrets`. Every PathRun has one (`path_run_make` refuses none);
  metal's would be `PATH_HOOK_NONE`, which knows no mounts. The Linux
  hook holds the mounts file's parse, the walk of each part with `lstat`
  (no symbolic link followed) and the secrets rule: no read, listing,
  `stat` or `real` of a secrets mount's files, no program handed its folder,
  and `run`'s `secrets` and `stdout_secret` the only ways in and out.
- **`Host`**, `interp.Host`: `call(name, args)`, behind the core's
  `host(...)`. Linux's has the names SURFACE.txt lists: `run` (fork by
  hand, `prctl`'s death signal, its own process group, the parent polling
  the pipes, a pidfd and a signalfd; `src/shale_spawn.zig`), the jobs
  (`shale --reap` double-forked out of the caller's tree, a job's record as
  text, `src/shale_jobs.zig`; when the job's leader exits, the reaper TERMs
  its group while the leader is an unreaped zombie, reaps it, then KILLs
  what is left at 2 seconds and writes the end only once the group is empty,
  as `run` does; the C's reaper, `jobs_reap_main` in c/host/jobs.c, does the
  same),
  `procs` and `proc_stop` (the /proc files, a pidfd,
  start times against a reused pid; the C's c/host/host_procs.c), `real`, `sleep`, `re_lines`, JSON, the
  tree readers and `edits_write`. Its time line, its records and the jobs a
  stopped run ends are the host's, written by the CLI after the report.
- **The preludes stay Shale.** The Linux and git preludes are source the C
  embeds and parses as Zig does, so `run`'s refusals and git's calls are
  ported once, in the host, not again in the prelude.

## The browser's host, as a tiebreaker

The playground runs the core over an in-memory tree, with no `Host` at all
(`src/shale_wasm.zig`, `src/wasm_io.zig`). **The C core builds to wasm with
the in-memory `Store` and no `Host`**, so whatever the browser cannot have
stays out of the core: a question of where a piece goes is settled by
whether the playground needs it.

## The external tools

The parsers Shale reads code with are programs, not part of the C:

- **`roc-ast` and `go-ast`** already are: a recipe's `roc_tree` and
  `go_tree` run the tool under `run` and hand its `--flat` output to the
  host's reader (`src/shale_host_flat_tree.zig`), which the C ports as is.
- **`zig-ast`** (tools/zig_ast) holds `zig_rename`, `zig_decls`,
  `zig_move`, `zig_imports` and `zig_tokens`, over Zig's own parser, so the
  C needs no Zig parser. The host starts one `zig-ast --serve` process a
  run, at its first Zig call, and speaks to it in framed requests and
  answers. **The C's host speaks the same wire to the same `zig-ast`
  program**; the protocol's one home is `src/shale_zig_wire.zig`, and the C
  follows it, never a copy of it here. The builtins' messages are
  SURFACE.txt's, as every builtin's are.
- `shale_tokens` stays the host's, since it is Shale's own lexer.
