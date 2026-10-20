# The Zig 0.17 upgrade cookbook

Recipes written for moving our Zig code from 0.16 to 0.17 (begun
2026-10-20), kept here so the move is the same mechanical work in every
repo, and a cold agent or Cloud Claude can follow it. Each is tested to
parse (`src/shale_cookbook_test.zig`). They have one job; once every repo
is on 0.17 they are history, kept for the next upgrade's shape.

## The method, per repo

On a branch (`zig-0.17`), with `~/zig-0.17.0/zig` (0.16 stays on the PATH
until the cutover):

1. **Count** what the repo uses that 0.17 changed:
   `shale <shale>/cookbook/zig17/census.shale only=<repo>` (from
   `~/showell_repos`). The counts are text, so an upper bound: many of
   0.17's renames keep the old name as a deprecated alias.
2. **Run the scripted fixes**, then `~/zig-0.17.0/zig fmt` over the tree:
   - [array_mult.shale](array_mult.shale): `[_]T{v} ** N` becomes `@splat(v)`,
     and a repeated one-character string `"x" ** N` becomes
     `&@as([N]u8, @splat('x'))` (0.17 removed both). What it can't type is
     listed.
   - [renames.shale](renames.shale): what 0.17 removed or reshaped, each a
     sure rewrite:
     - calls: `dupeZ` to `dupeSentinel(.., 0)`, `fmt.bufPrintZ` to
       `mem.printSentinel(.., 0)`, `ascii.indexOfIgnoreCase` to
       `findIgnoreCase`, `Ast.parse`'s mode;
     - the optimize tags: `.ReleaseSafe` to `.safe` and so on; `.Debug`
       only on a line about the optimize mode;
     - `if (b.args) |a| run.addArgs(a);` to `run.addPassthruArgs();`;
     - a fixed-size bitset's `.initEmpty()` / `.initFull()` to `.empty` /
       `.full`;
     - type info: `.fields.len` to `.field_names.len`; a pointer's
       `.is_const` (and the rest) under `.attrs`; a fn's
       `.params[i].type` to `.param_types[i]`.
   - [has_decl.shale](has_decl.shale) (`fix=yes`): every declaration of a
     name some `@hasDecl` asks about, made `pub` (0.17's `@hasDecl` sees
     only `pub`; an optional check then turns its feature off silently).
   - **`zig fmt` itself (0.17) rewrites the deprecated builtins**:
     `@intFromEnum` to `@backingInt`, `@enumFromInt(x)` to
     `@fromBackingInt(@intCast(x))`.
3. **Compile, and fix what the compiler names**, by hand, with `edit`:
   - `@typeInfo(T).@"struct".fields` is `field_names` / `field_types`
     (struct-of-arrays). A loop that uses only each field's name loops over
     `field_names`; its capture may not shadow a parameter.
   - `Ast.parse(gpa, src, .zig)` is `Ast.parse(gpa, src, .{ .mode = .zig })`.
   - An optimize mode compared with `==` or switched on uses the new tags
     (`.debug`, `.safe`, `.fast`, `.small`); as a value (`.optimize =
     .ReleaseSafe`) the old name still resolves, deprecated.
   - `b.build_root` is `b.root` (a Path): the build root's path is
     `b.root.root_dir.path`; `b.pathFromRoot` is gone too.
   - A loop over a type's fields (`for (info.fields) |f|`): loop over
     `info.field_names`, `info.field_types` or both zipped, `f.name` and
     `f.type` renamed in the body. Eight in all our repos: by hand.
   - A string repeated that is longer than one character: build it at
     comptime (metal-vmm's client.zig has the pattern).
   - `linux.waitpid` fills an `i32`; `linux.W` reads a `u32`: hold the
     status as `i32` and `@bitCast` it for `W`.
4. **Then the behaviour 0.17 changed under the code**, which no compiler
   error shows and the repo's own tests must:
   - A generic's `@src().fn_name` ends `__func_<n>` (it was `__anon_<n>`):
     zig-coverage-sdk cut on the old suffix, and two of its tests failed.
   - `Reader.allocRemaining(.limited(n))` takes exactly `n` bytes and errors
     past it; 0.16 errored on reaching `n`. Code that passed `max + 1` to
     allow `max` now allows `max + 1` (webbox's readLimitedBody; shale's
     script_runner source cap): pass `max`. Audit every `.limited(...)`.
   - `@hasDecl` sees only `pub` (above): `has_decl.shale` before the tests.
5. **Judge** with the repo's own tests, and commit to the branch, not
   master: the repos move together.

## Done so far

| repo | branch | fixes |
|---|---|---|
| zig-coverage-sdk | `zig-0.17` | typeInfo fields, Ast.parse, one array, the `__func_` suffix, fmt's builtins |
| zig-webbox | `zig-0.17` | 34 arrays and strings, 5 renames, 3 `pub` for @hasDecl, `build_root`, allocRemaining's limit |
| metal-vmm | `zig-0.17` | arrays, the optimize tags, `b.args`, bitsets, bufPrintZ, type info, 4 field loops, a comptime string, waitpid |

## The repos with siblings

shale, zig-metal-unikernel, angry-gopher, dog-park and observatory build
against their siblings by path (`../zig-webbox`, ...), so they move as a
set: a tree of `zig-0.17` worktrees side by side (each repo's branch
checked out under one folder, as the siblings' paths expect), every repo
ported there, the whole set judged together, then each branch merged.
