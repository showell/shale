# Zig Shale: deprecated and frozen

**This folder is deprecated and frozen.** It is Shale's first
implementation, in Zig. The C in [`../c`](../c) replaced it on 2026-11-17,
and the C is the only source of truth: every `shale` in use is C. Nothing
here gets fixes or features. A bug found here is fixed in the C, if the C
has it.

## What is inside

- `src/`: the language (`src/script/`: the lexer, parser, compiler, VM,
  builtins and budgets), the Linux host, the CLI, the signed script route,
  the installed suite, the browser build's module, and their unit tests.
- `build.zig`, `build.zig.zon`, `zig-version`: the build, its dependencies
  and the patched Zig it pins.
- `tools/`: the Zig gate (`shale_gate.zig`, which `../c/gate` ports), the
  hunt's and the timing's test runners, and the scripts that read the
  coverage and benchmark steps' output (`coverage.shale`, `bench.shale`).
- `coverage/lines.txt`: the coverage floor the unit tests were held to.
- `web/`: the WebAssembly playground and the tutorial's lessons.

Three files here are frozen copies of live ones, kept so the Zig still
builds as it was: the preludes (`src/script/prelude.shale`,
`src/shale_prelude_linux.shale`, `src/shale_prelude_git.shale`; the live
ones are `../c/preludes/`) and the zig-ast wire (`src/shale_zig_wire.zig`;
the live one is `zig-ast/wire.zig`). zig-ast itself is live, a repo of
its own, [zig-ast](https://github.com/showell/zig-ast).

## Why it is kept

- **History.** Shale was designed and grown in this code, from 2026-10-13
  until the port.
- **The reference.** The C port was matched against it byte for byte:
  every stop text, budget and report line. The golden tests in
  `../tests/golden` were frozen from it, and the harness still reads the
  Zig tests' scripts and `web/lessons.js` here when it collects their
  inputs.
- **Bare metal.** A bare-metal unikernel ran this Shale, compiled with its
  own `host_io`, as a site's script engine, until 2026-11-18.

## Building it, if ever needed

It builds with the patched Zig that `zig-version` names (from
[patched-zig-compiler](https://github.com/showell/patched-zig-compiler)),
with [zig-webbox](https://github.com/showell/zig-webbox) and
[zig-ast](https://github.com/showell/zig-ast) checked out beside Shale's
checkout (`build.zig.zon` names them by path), and
[zig-coverage-sdk](https://github.com/showell/zig-coverage-sdk) too for
the coverage and fuzz steps:

    cd legacy_zig && zig build        # zig-out/bin: shale, shale-gate, zig-ast
    cd legacy_zig && zig build test   # the unit tests

**zig-webbox and zig-coverage-sdk were retired from the box on
2026-11-18**, their checkouts deleted, so the frozen build may not build on
this box any more. That is accepted: the Zig is history, and GitHub still
has both repos, to check out beside it if it is ever wanted.

That build is best-effort. Its paths were moved here and not run again, and
some tests read the checkout's files by their old paths (the docs, the
cookbook, `tools/`), so they may fail where they look. The GitHub workflows
`.github/workflows/test.yml` and `coverage.yml` run it in this folder, on
demand only.
