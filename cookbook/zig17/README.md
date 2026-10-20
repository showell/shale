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
   - [array_mult.shale](array_mult.shale): `[_]T{v} ** N` becomes `@splat(v)`
     (0.17 removed array multiplication). What it can't type is listed.
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
   - `b.build_root` is `b.root` (a Path).
   - Run steps' `addArgs(b.args)` is `addPassthruArgs()`.
4. **Then the behaviour 0.17 changed under the code**, which no compiler
   error shows and the repo's own tests must:
   - A generic's `@src().fn_name` ends `__func_<n>` (it was `__anon_<n>`):
     zig-coverage-sdk cut on the old suffix, and two of its tests failed.
5. **Judge** with the repo's own tests, and commit to the branch, not
   master: the repos move together.

## Done so far

| repo | branch | fixes |
|---|---|---|
| zig-coverage-sdk | `zig-0.17` | typeInfo fields, Ast.parse, one array, the `__func_` suffix, fmt's builtins |
