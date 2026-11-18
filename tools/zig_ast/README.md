# zig-ast

zig-ast reads a Zig file the way Zig's own tokenizer and parser
(`std.zig`) read it, and answers Shale's host over a pipe. It is what
Shale's Zig builtins stand on: `zig_decls`, `zig_tokens`, `zig_imports`,
`zig_rename`, `zig_renames` and `zig_move`. Shale's host checks a call's
arguments, reads the files, sends them here, and acts on the answer: the
values, the stops, the step charges, and the edits, which it writes through
its own `edit`. zig-ast writes no file.

This folder is self-contained. Nothing in it reaches outside it; its one
outside dependency is zig-webbox (its `src/zig_refs.zig` reads a file's
`@import`s), pinned in `build.zig.zon`.

## What is here

| file | what it holds |
|---|---|
| `main.zig` | the server: each command, its request and its answer's events |
| `wire.zig` | the wire format, its one home |
| `zig_ast.zig` | the reader: declarations, tokens, identifiers |
| `plan.zig` | the rename planner |
| `zig_move.zig` | the mover |
| `zig_ast_test.zig` | the tests |
| `install.shale` | builds it and installs it where Shale's host looks |
| `zig-version` | the Zig it is built with |

## The wire

Shale's host starts `zig-ast --serve` at a run's first Zig call and keeps
it for the whole run. The other side is C: `c/host/zig_wire.c` and
`c/host/zig_ast.c` in Shale's repo, held to `wire.zig`.

- zig-ast first writes one line, `zig-ast serve <format> <zig version>`.
  The format is `wire.zig`'s `format`, 1 today; a change a reader must know
  of is a new number.
- Then each request is a frame, `<length>\n` and that many bytes, and each
  answer is a frame the same way.
- A frame's body is lines of fields joined by a tab, each field escaped
  (`\\`, `\t`, `\n`, `\r`).
- A request's first line is its command: `tokens`, `decls`, `imports`,
  `rename` or `move`. A line `src<TAB><n>` is followed by n raw bytes, a
  source file, and a newline.
- An answer is events, read in order: `work <n>` charges n steps, a
  refusal is its own word (`parse_error <line> <tag>`, `invalid <line>`,
  and the others `main.zig`'s header lists), and then the values.
  `tokens` answers with a block, `tb<TAB><n>` and n records of 13 bytes
  (the tag's number, the start, the end and the line, little-endian).
- A request it cannot read is answered `bad <why>`, and the host stops.

`main.zig`'s header gives each command's request lines and answer events in
full.

## Building, testing and installing

It builds with the Zig that `zig-version` names (a patched 0.17.1). The
tree it reads is the building Zig's, so the pin is the Zig whose syntax it
knows.

```sh
zig build          # zig-out/bin/zig-ast, always ReleaseSafe
zig build test     # the format, then the tests
```

The tests hold the reader, the planner and the mover to fixtures and to a
corpus of real Zig, read whole: this folder's own files and zig-webbox's
`src/`. A rename there and back, or a move there and back, must give the
file it began with, and the two must agree on what is a use.

`shale install.shale` (from this folder, or `shale
tools/zig_ast/install.shale` from Shale's root) builds it with the pinned
Zig, found at `tools:zig-<version>/zig`, and installs it as
`tools:zig-ast/bin/zig-ast`, where Shale's host looks. `SHALE_ZIG_AST`
names another binary for one run, as Shale's tests do.
