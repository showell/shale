# The C port's seams

Shale is being rewritten in C (Steve, 2026-11-17). The language is frozen
byte for byte, and today's Zig Shale is the oracle. This page is the design
the agents porting it share: how the C represents a run, and where it meets
what is outside it. It holds no code. The C's style and its foundation (the
arena, `Str`, `vec`, `map`, err.h) are on the `c-foundation` branch, in
c/docs/style.md and c/foundation.

**What the port must match** is [`SURFACE.txt`](SURFACE.txt): every
builtin, stop text, budget, flag and report line, generated from the Zig
by `tools/surface.shale` and gated by `zig build surface-check`. The
differential harness (the `shale-differ` lane) runs both and compares
output, report, stop and exit code. Where this page and the Zig disagree,
the Zig wins.

## Values

- **A value is a tagged struct:** a tag (nil, boolean, int, string, table)
  and a union of `bool`, `int64_t`, a `Str` (pointer and length) and a
  `Table *`, as large and as aligned as Zig's `value.Value` (the probe below
  says how large; a 16-byte slice and its tag make it 24 bytes on x86-64).
  A string is never changed once made, and the value points at its bytes
  in the values' arena.
- **A table is one struct for list and map:** its kind (empty, list, map),
  a list of values grown as Zig's `ArrayList` grows (vec.h), and a map
  from `Str` to value that keeps the order keys were first set. `{}` takes
  its kind from its first entry, as in Zig.
- **Tables are shared by reference**, and `==` on them is refused, so a
  table needs no identity beyond its pointer. The yield's copy keeps "a
  table reached twice is one table" with a forwarding map from old pointer
  to new.
- **The values' map is not map.h during the port.** map.h keeps keys,
  hashes and values in parallel arrays with an index from the first entry.
  Zig's `StringArrayHashMapUnmanaged` keeps one buffer of entries (key,
  value, a 32-bit hash) and adds an index only past a few entries. Their
  allocations differ, so the arena would stop in a different place. Values
  use a second map, `ValueMap`, that makes Zig's allocations in Zig's order.
  map.h serves the C's own tables (the parser's names, the regex cache's
  keys), which live in the machine's arena.
- **A `..` chain** keeps Zig's tail buffer: the last string a chain built,
  its used and its capacity, so that `s = s .. x` is linear and holds what
  Zig's holds.

## Fidelity: the arena's byte counts

Steve agreed on 2026-11-17 that **arena byte counts are mirrored from Zig
during the port, and freed after the cut-over.** `budget().arena_bytes`,
where `budget arena` stops, and where `budget kept` and `budget machine`
stop are all observable, so the C allocates what Zig allocates, in the
same sizes, alignments and order.

- **The arena itself already matches.** arena.h is `value.Budgeted`: its
  16-byte chunk header, `chunk_least`, `big`, doubling, and only the last
  allocation growing in place. The C adds `Budgeted.heap()`'s twin, an
  allocator whose free gives the bytes back, for the machine's regex cache
  and a long call's arguments.
- **The structs whose sizes reach the arena are mirrored,** each with a
  `_Static_assert` on its size and alignment against the numbers the probe
  gives:
  - `value.Value` (the tag and its payload, as Zig lays out the union);
  - `value.Table`, and the buffers its list and map make: the list's
    `cap × sizeof(Value)`, the map's entry buffer and its index header;
  - `diag.Call` (a trail's entry) and the `Stop` a `fail` makes;
  - `interp.Effect`, the effects log's entry;
  - the machine's arena: `vm.Frame`, the operand slots, `compile.Op`, and
    `ast`'s node structs, since `budget machine` stops on what the tree and
    the code take;
  - the regex program's instructions, whose count is also the search's
    step charge.
- **The probe (a later task) is a small Zig program**, run by a `zig build`
  step, that prints a TSV: for each type above, its `@sizeOf` and
  `@alignOf`; and for a fixed list of operations (a list pushed to N, a map
  put to N, a string joined N times, a yield's copy, a source parsed and
  compiled), the sequence of `(len, align)` each makes of the arena. The C's
  tests read the same TSV, checked in beside the C's tests, and hold the C's
  allocations to it, a difference failing by its row. A change to the Zig's
  layout fails the probe's own check until the TSV is regenerated.
- **After the cut-over** the mirroring is let go: `ValueMap` can become
  map.h, the structs can shrink, and the arena's counts become the C's
  own, with the surface's texts unchanged.

## The VM, the stops and the budgets

- **The C ports the VM, not the tree-walker.** The ops of
  src/script/compile.zig and the loop of src/script/vm.zig are the oracle:
  the same ops, the same enters charged in the walker's order and hung on the op after them, and a loop that never
  recurses. The walker stays Zig's, as its second implementation.
- **A stop is err.h's `RunCtx`.** Every function that can stop returns
  `Status`; `run_fail` and `run_fail_built` set the place, the class, the
  message (Zig's format text, its fallback when the message cannot be
  made), the function and the trail. `run_budget_stop` is
  `interp.Run.budgetStop`, and every refused allocation is `run_oom`, the
  arena budget, as Zig reads every `OutOfMemory`.
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

The foundation's containers hand out pointers that a growth or a reset
frees (each marked `DRAGON:` in c/foundation). Nothing in the C holds a
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

## The secrets rule's one check

A secrets mount's path must never appear in a message. **`run_fail` is the
one check**: every stop's message passes through it (`run_fail_built`,
`run_budget_stop` and `run_oom` call it), so the C asks the `PathHook`
there whether the message names a path under a secrets mount, and refuses
it there, rather than at each place a message is built from a path. A
message that is not a stop's (a report line, a `--verbose` effect) goes
through the same check before it is written.

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
  path), `may_change` (asked before every change: a read-only mount
  refuses, and the undo journal records the path's state), and
  `absolute_hint` (the mount to name in an absolute path's stop). The Linux
  hook holds the mounts file's parse, the walk of each part with `lstat`
  (no symbolic link followed) and the secrets rule: no read, listing,
  `stat` or `real` of a secrets mount's files, no program handed its folder,
  and `run`'s `secrets` and `stdout_secret` the only ways in and out.
- **`Host`**, `interp.Host`: `call(name, args)`, behind the core's
  `host(...)`. Linux's has the names SURFACE.txt lists: `run` (fork by
  hand, `prctl`'s death signal, its own process group, the parent polling
  the pipes, a pidfd and a signalfd; `src/shale_spawn.zig`), the jobs
  (`shale --reap` double-forked out of the caller's tree, a job's record as
  text, `src/shale_jobs.zig`), `procs` and `proc_stop` (the /proc files, a pidfd,
  start times against a reused pid), `real`, `sleep`, `re_lines`, JSON, the
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
- **`zig-ast`** is being extracted now (the `zig-ast` lane): `zig_rename`,
  `zig_decls`, `zig_move`, `zig_imports` and `zig_tokens` move out of the
  host into a program over Zig's own parser, so the C needs no Zig parser. The C
  takes whatever interface that lane lands. SURFACE.txt freezes those
  builtins' messages today; when the extraction changes them, the surface
  is regenerated in that change, and the C follows the new surface.
- `shale_tokens` stays the host's, since it is Shale's own lexer.
