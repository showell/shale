# The builtin table

**`c/builtins.txt` is the one home of Shale's builtin surface.** Every
builtin a script can call is an entry there: the 78 of the C
(`c/script/ast.h`, `c/script/vm.c`) and every function of the three
preludes (`c/preludes/core.shale`, `linux.shale`, `git.shale`), helpers
included, since a helper's name is taken too. The C, the preludes and the
docs are held to it in the fast tier, and the Roc platform's surface
(`roc_platform/`) is written from it.

## What an entry says

The file's header is the format's home; in short, a stanza a builtin:

```
read(p: Path, offset?: Int, n?: Int) -> Str
  impl: c builtin_read
  kind: effect
  stops: argument, budget read_bytes, file_system
  doc: a file's text, or its part from `offset`, `n` bytes
  anchor: docs/SHALE.md#the-store
  roc: read! : Str => [Found(Str), Missing]
  roc_params: path
  roc_doc: The file's text, or `Missing` when nothing is at the path.
  values: Found(text); Missing, when nothing is at the path
  driver: if not exists(q.path) then return {tag = "Missing"} end
  driver: return {tag = "Found", value = read(q.path)}
```

- **The signature** gives each argument's name and type, `?` for one a call
  may leave out and `...` for any number, and the answer's type. The types
  are Shale's values, named for what they hold (`Path`, `List(Str)`, `Opts`,
  `{line: Int, text: Str}`, `Str | Nil`).
- **`kind`** is `effect` (it reaches past its arguments: the store, a
  program, the clock, the run's output, its arguments or budgets, or it
  stops the run), `pure` (its answer is its arguments' alone) or `internal`
  (a prelude's helper). Today: 86 effects, 66 pure, 19 helpers.
- **`stops`** are the classes it stops with beyond a wrong type or count,
  which every builtin has; `opts` an Opts argument's keys and types.
- **`doc` and `anchor`**: one line, and the docs' section that names it.

**Only effects are exported to Roc**, since Roc does pure work itself (Steve,
2026-11-19: "exporting things that Roc can do more or less itself would be
overkill"). **The one exception is the regular expressions** (`re_find`,
`re_find_all`, `re_lines`, `re_match`, `re_replace`): pure, but exported
because Roc has none (Steve, 2026-11-19), listed in
`c/builtin_table.shale`'s `BT_PURE_EXPORTS`. A Roc hosted function is
always effectful (`=>`), so a Roc function that calls one is effectful too.
An exported effect adds `roc` (its declaration), `roc_params`
(its arguments' names), `roc_doc`, `values` (the outcomes that are values:
every other one is a stop) and its `driver` lines. Ten are exported now;
the other 76 effects are candidates, each a table entry away.

## Why a text file read by Shale

The table is read by Shale scripts, not by the C compiler, so it is a text
file of stanzas rather than a C header of entries:
- what it feeds is text: Roc declarations, Shale driver clauses, C shims and
  docs. A C header would need a C program to print each of them, where the
  Shale that reads the table is a few hundred lines;
- a stanza of `key: value` lines reads and diffs as prose, with room for a
  type like `{lines: List(Str), next: Int, done: Bool}` that a TSV cell or
  a C initializer would cramp;
- it needs nothing new: the build is Shale scripts already (`c/build.shale`),
  and the tiers have `shale`.

**The C is checked against the table, not generated from it.** `ast.h`'s
`Builtin` enum and names and `vm.c`'s `builtin_specs()` stay hand-written,
and `c/check_builtins.shale` holds them to the table. Generating them would
put a generator before the first compile, and a new machine's first build
needs no `shale` (`c/docs/building.md`, "First install on a new machine");
`c/gen_preludes.c` is in C for that reason. The check gives the same
guarantee: a builtin in one place and not the other fails the fast tier.

## The check: c/check_builtins.shale

Run from the root, `shale c/check_builtins.shale`; any finding is a line
and fails the run. Both ways, each a finding:
- **the C:** ast.h's names and vm.c's specs, the same names in the same
  order as the table's `impl: c ...` entries, each spec's function the
  entry's, its arity the entry's arguments;
- **the preludes:** each top-level function an entry with that prelude as
  its `impl`, its parameters by name with `?` where Shale has it; each
  `git_opts` call's keys the entry's `opts`;
- **the docs:** each anchor a heading of its file whose section names the
  builtin, and every builtin in docs/SHALE.md's "Every builtin, at a
  glance" an entry;
- **the Roc platform:** glue's header declares exactly the exported effects,
  and the four generated files are what the table makes now.

`self_test=1` runs the checks on texts it changed (an entry renamed, a
builtin added to ast.h, an arity changed, a prelude function added, a
parameter renamed, an option made optional, a git option added, a doc
section that no longer names it, an anchor that is no heading, an export
that is pure) and holds each to its finding.

## The generator: c/gen_builtins.shale

`shale c/gen_builtins.shale` writes, from the exported entries:
- `roc_platform/platform/Shale.roc` and `main.roc`;
- `roc_platform/host/hosted.gen.c`: the driver (a Shale function an
  effect, from its `driver` lines, and the loop), each shim (which keeps
  its return address, the Roc's call site), the request (Roc's arguments as
  a Shale map, each by its `roc_params` name), the answer (Shale's value as
  Roc's) and each one's Roc name, which a stop at the Roc's line names;
- `roc_platform/surface.gen.shale`: the Roc names the launcher finds in a
  script's text (`arg`'s call, whose literals are the values the script
  reads, and every hosted call).

The marshalling follows the Roc type: `Str`, `U64`, `I64`, `Bool`, `{}`,
`List(T)`, records and tag unions, nested. A tag union crosses as `{tag =
"Name", value = payload}`. The C types are glue's (`roc_platform/host/
roc_platform_abi.h`): a shim's prototype, a record's fields, a tag union's
payloads, and a list's record by its fields. A Roc type the emitters do not
carry yet (a list of tag unions) stops the generator, naming the function
to extend. The library both scripts use is `c/builtin_table.shale`.

## What runs where

- **The fast tier** (`c/build/builtin_table.shale`): the check, its
  self-test, and `gcc -fsyntax-only -Werror` of `roc_platform/host/host.c`,
  which includes all of C Shale and the generated file (about half a
  second; TCC cannot read glue's header).
- **When a Roc build is asked for:** `roc_platform/run.shale` builds the
  host's object (`gcc -O2`, about 25 s, cached) and the script;
  `roc_platform/self_test.shale` is the policy proofs end to end. Both need
  the pinned Roc, which the tiers do not have.
