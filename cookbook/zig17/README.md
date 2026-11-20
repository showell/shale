# The Zig 0.17 upgrade cookbook

Recipes for moving Zig code from 0.16 to 0.17, the same mechanical work in
every repo, kept as the shape of the next upgrade. Each is held to parse
and compile (`shale --check`) by C's full tier, `shale c/build.shale full=1`.

## The method, per repo

On a branch, with the new compiler beside the old:

1. **Count** what the repo uses that 0.17 changed:
   `shale <shale>/cookbook/zig17/count_upgrade_changes.shale only=<repo>`
   (from the folder that holds the repos). The counts are text, so an
   upper bound: many of 0.17's renames keep the old name as a deprecated
   alias.
2. **Run the scripted fixes**, then `zig fmt` over the `.zig` files only
   (`zig fmt .` formats `.zon` too, and can change a generated file's hash
   under a golden test):
   - [rewrite_array_mult.shale](rewrite_array_mult.shale): `[_]T{v} ** N`
     and `"x" ** N` become `@splat` forms (0.17 removed both), and it lists
     what it leaves for you to type.
   - [rewrite_renamed_calls.shale](rewrite_renamed_calls.shale): what 0.17
     removed or reshaped, each a sure rewrite.
   - [find_hidden_decls.shale](find_hidden_decls.shale) (`fix=yes`): makes
     `pub` every declaration some `@hasDecl` asks about, since 0.17's
     `@hasDecl` sees only `pub` and an optional check would quietly turn
     its feature off. It matches plain words, so a builtin or local of the
     same name is a false hit: read its list before `fix=yes`.
   - `zig fmt` itself (0.17) rewrites the deprecated builtins
     (`@intFromEnum`, `@enumFromInt`).
3. **Compile, and fix what the compiler names**, by hand, with `edit`.
   **Zig compiles only what is used**, so a library function its own tests
   skip keeps its old code until a consumer's build reaches it: grep the
   whole set for the removed forms, beyond one repo's green build.
4. **Then the behaviour that changed under the code**, which the repo's own
   tests must catch, since the compiler is silent on it (0.17's
   `Reader.allocRemaining(.limited(n))` takes exactly `n` bytes; a
   generic's `@src().fn_name` ends `__func_<n>`).
5. **Judge** with the repo's own tests, and commit to the branch. Repos
   that build against their siblings by path move as a set: the upgrade's
   worktrees side by side, judged together, then each branch merged.

0.17.0's compiler_rt fails to compile for the kernel's soft-float x86_64
target; zig-metal-unikernel carries a patched copy,
[compiler_rt_0_17_0](https://github.com/showell/zig-metal-unikernel/tree/master/compiler_rt_0_17_0)
(its README says why and when to delete it).
