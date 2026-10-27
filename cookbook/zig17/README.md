# The Zig 0.17 upgrade cookbook

Recipes for moving Zig code from 0.16 to 0.17, the same mechanical work in
every repo, kept as the shape of the next upgrade. Each is tested to parse
(`src/shale_cookbook_test.zig`).

## The method, per repo

On a branch, with the new compiler beside the old:

1. **Count** what the repo uses that 0.17 changed:
   `shale <shale>/cookbook/zig17/census.shale only=<repo>` (from the folder
   that holds the repos). The counts are text, so an upper bound: many of
   0.17's renames keep the old name as a deprecated alias.
2. **Run the scripted fixes**, then `zig fmt` over the `.zig` files only
   (`zig fmt .` formats `.zon` too, and can change a generated file's hash
   under a golden test):
   - [array_mult.shale](array_mult.shale): `[_]T{v} ** N` becomes `@splat(v)`,
     and a repeated one-character string `"x" ** N` becomes
     `&@as([N]u8, @splat('x'))` (0.17 removed both). What it can't type is
     listed.
   - [renames.shale](renames.shale): what 0.17 removed or reshaped, each a
     sure rewrite (calls, the optimize tags, `b.args`, `pathFromRoot` and
     `build_root`, bitsets' `initEmpty`/`initFull`, type info's fields).
   - [has_decl.shale](has_decl.shale) (`fix=yes`): every declaration of a
     name some `@hasDecl` asks about, made `pub` (0.17's `@hasDecl` sees
     only `pub`; an optional check then turns its feature off silently).
     Its names are plain words, so a builtin or local of the same name is
     a false hit: read its list before `fix=yes`.
   - `zig fmt` itself (0.17) rewrites the deprecated builtins
     (`@intFromEnum`, `@enumFromInt`).
3. **Compile, and fix what the compiler names**, by hand, with `edit`.
   **Zig compiles only what is used**, so a library's function its own
   tests never call keeps its old code until a consumer's build reaches
   it: grep the whole set for the removed forms instead of trusting one
   repo's green build.
4. **Then the behaviour that changed under the code**, which no compiler
   error shows and the repo's own tests must (0.17's
   `Reader.allocRemaining(.limited(n))` takes exactly `n` bytes; a generic's
   `@src().fn_name` ends `__func_<n>`).
5. **Judge** with the repo's own tests, and commit to the branch, not
   master. The repos that build against their siblings by path (shale,
   zig-metal-unikernel, angry-gopher, dog-park, observatory) move as a set:
   a tree of the upgrade's worktrees side by side, judged together, then
   each branch merged.

0.17.0's compiler_rt does not compile for the kernel's soft-float x86_64
target; zig-metal-unikernel carries a patched copy, `compiler_rt_0_17_0/`
(its README says why and when to delete it).
