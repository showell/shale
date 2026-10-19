# shale

**Shale**, a small language for a site's own data, and the box's tool in
place of bash and Python. You explore and print, read the output, then
write a script that changes things. It has Lua's look and smaller
semantics of its own. Every effect goes through
[zig-webbox](https://github.com/showell/zig-webbox)'s `store`, so a script
does the same on Linux and on metal, and every run stays inside its
budgets. A script **just runs**: a stopped run keeps what it already did,
and its effects log says what.

- **On a live site:** a signed script, run by the site itself
  (`script_route`, `zig build shale-sign`):
  [angry-gopher](https://github.com/showell/angry-gopher) (lynrummy.com) and
  [dog-park](https://github.com/showell/dog-park), on Linux and on
  [zig-metal-unikernel](https://github.com/showell/zig-metal-unikernel). The
  two name `.shale` in their `build.zig.zon`s; the kernel takes `-Dshale-root`.
  The installed suite (`shale_suite`) is built and tested, and no site runs
  it yet.
- **On the box:** the `shale` CLI, every top-level shell call an agent
  makes (`tools/shale-gate.sh`, the hook; [docs/SHALE-AGENTS.md](docs/SHALE-AGENTS.md)),
  with Zig's own parser behind `zig_decls`, `zig_move` and `zig_rename`.

## The modules

| module | root | what |
|---|---|---|
| `shale` | `src/shale.zig` | portable: what an application's code imports |
| `shale_linux` | `src/shale_linux.zig` | the Linux host (`run`, jobs, mounts, the Zig tools), for a Linux `main` only |

Both take webbox's `webbox` and `host_io` modules: this repo depends on
zig-webbox by path (`../zig-webbox`), and an application takes `shale` from
`b.dependency("shale", ...)` beside its `webbox`. zig-metal-unikernel
compiles the portable half with its own `host_io`, as it does webbox.

    script.run(io, backing_allocator, root, source, budgets, now, out) -> Result

**The reference is [docs/SHALE.md](docs/SHALE.md):**
- the language and its grammar;
- every builtin, with its cost and its errors;
- the budgets and what each charges;
- the store and its confinement;
- the runner (`zig build script`) and the signed route on the live site;
- a short tutorial;
- the site examples, operator scripts each tested against a store shaped
  like angry-gopher's data;
- what was decided and refused.

## Checks

    zig build test          # every test but the expensive ones: about 80 s
    zig build consumers     # the applications' own checks against this tree: minutes
    zig build fuzz          # the expensive fuzzes and sweeps, for a bug hunt
    zig build doc-check     # every repo's docs: each cited path and name is there

**`test`** runs the two modules' tests, the signer's and the gate's. The
Zig tools' tests read zig-webbox's checkout too: they replay splits from its
history and walk its sources beside this repo's. **`consumers`** runs this
repo's own docs check, metal-vmm's queue tool by this tree's `shale`, then
dog-park's `zig build test`, zig-metal-unikernel's `zig build gopher` and
angry-gopher's `ops/check_zig`, one at a time; it needs those checkouts
beside this one, and fails naming one that is missing. Run it before a change
here goes to master. **`fuzz`** runs the tests named `expensive fuzz: ...` or
`expensive sweep: ...`, which `test` counts as skipped: a fuzz hunts bugs
once, and each bug it finds becomes a test of its own. **`doc-check`** runs
`tools/doc_check.shale` from the folder that holds the repos, its fixtures
first: in every repo's tracked docs but the history, each backticked or
linked path must resolve, and each `` `file`'s `name` `` must be in that
file. Each repo's gate runs it for its own docs (`-- only=<repo>`).

`-Dtest-filter=TEXT` runs only the tests whose names contain it;
`-Dtest-timing` prints each test's time, slowest first; `-Dscript-fuzz=N`
sets how many sources the script fuzzer runs. Two more steps measure and
are run by hand, by no tier: `zig build script-bench` (Shale's hot paths)
and `zig build shale-scale` (the largest site each site example fits).

## Docs

- [docs/SHALE.md](docs/SHALE.md): the reference (above).
- [docs/SHALE-AGENTS.md](docs/SHALE-AGENTS.md): Shale as an agent's tool on
  Linux.
- [docs/SHALE-WANTS.md](docs/SHALE-WANTS.md): the ledger of what Claude
  reached past Shale for.
- [cookbook/zig/README.md](cookbook/zig/README.md): recipes for finding a
  Zig file's seams, moving code along them and judging the move, each
  tested to parse. Recipes, not builtins: copy one and change it.
- [cookbook/roc/README.md](cookbook/roc/README.md): recipes for a Roc
  tree: long lines laid out by `roc fmt`, and two archives judged the same
  machine code.
- [docs/designs/DESIGN-shale-dogfood.md](docs/designs/DESIGN-shale-dogfood.md):
  the design for Claude working in Shale instead of bash and Python.
- [docs/designs/DESIGN-shale-stream.md](docs/designs/DESIGN-shale-stream.md):
  a background run's output streamed as it runs; proposed, parked.

## Status

| | |
|---|---|
| Source | moved from zig-webbox at 61f66b6 (2026-10-18), without its history: that is in zig-webbox's (`git -C ../zig-webbox log -- src/script`) |
| Runs | on the box, as every agent's shell; in angry-gopher and dog-park on Linux and metal, through their builds |
| Tests | the modules' unit tests; the request-edge harness for its number parses runs in angry-gopher |
