# Zig Shale: deprecated and frozen

**This folder is deprecated and frozen.** It is Shale's first
implementation, in Zig. The C in [`../c`](../c) replaced it and is the
source of truth: every `shale` in use is C. Fixes and features go to the
C.

## What is inside

- `src/`: the language (`src/script/`: the lexer, parser, compiler, VM,
  builtins and budgets), the Linux host, the CLI, the signed script route,
  the installed suite, the browser build's module, and their unit tests.
- `build.zig`, `build.zig.zon`, `zig-version`: the build, its dependencies
  and the patched Zig it pins.
- `tools/`: the Zig gate (`shale_gate.zig`, which `../c/gate` ports), the
  hunt's and the timing's test runners, and the scripts that read the
  coverage and benchmark steps' output.
- `coverage/lines.txt`: the coverage floor the unit tests were held to.
- `web/`: the WebAssembly playground and the tutorial's lessons.

The preludes (`src/script/prelude.shale`, `src/shale_prelude_linux.shale`,
`src/shale_prelude_git.shale`) and the zig-ast wire
(`src/shale_zig_wire.zig`) are frozen copies, kept so the Zig builds as it
was. The live ones are `../c/preludes/` and
[zig-ast](https://github.com/showell/zig-ast)'s `wire.zig`.

## Why it is kept

- **The reference.** The C port was matched against it byte for byte:
  every stop text, budget and report line. The golden tests in
  `../tests/golden` were frozen from it, and the harness reads the Zig
  tests' scripts and `web/lessons.js` here when it collects their inputs.
- **The tutorial.** The browser build in `web/` runs the online lessons.

## Building it

It builds with the patched Zig that `zig-version` names (from
[patched-zig-compiler](https://github.com/showell/patched-zig-compiler)),
with [zig-webbox](https://github.com/showell/zig-webbox) and
[zig-ast](https://github.com/showell/zig-ast) checked out beside Shale's
checkout (`build.zig.zon` names them by path), and
[zig-coverage-sdk](https://github.com/showell/zig-coverage-sdk) too for
the coverage and fuzz steps:

    cd legacy_zig && zig build        # zig-out/bin: shale, shale-gate, zig-ast
    cd legacy_zig && zig build test   # the unit tests

The build is best-effort: some tests read the checkout's files by their
paths from before the move into this folder (the docs, the cookbook,
`tools/`), and may fail there. The GitHub workflows
`.github/workflows/test.yml` and `coverage.yml` run it in this folder, on
demand.
