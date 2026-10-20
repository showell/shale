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
2. **Run the scripted fixes**, then `~/zig-0.17.0/zig fmt $(git ls-files '*.zig')`
   (the `.zig` files only: `zig fmt .` formats `.zon` too, and reformatted
   dog-park's generated world.zon, whose hash then turned its golden tests
   into silent skips):
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
     - `X.pathFromRoot(p)` to `pathFromRoot(X, p)`, appending that function
       and `rootOf` (an absolute path), and `b.build_root.handle` to
       `b.root.root_dir.handle`;
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
   - A string repeated that is longer than one character: array_mult.shale
     writes `repeated(s, n)`; one built from an expression (`(part ++ "/")
     ** n`) is a hand's, the same way.
   - `std.meta.fields(T)` is a compile error: `std.meta.fieldNames(T)` where
     only names are used (`comptime` before it in an `inline for`); an
     enum's values are `@typeInfo(E).@"enum".field_values`.
   - The build API: `LazyPath.getPath` is gone (resolve a checked-in file's
     path yourself, the kernel's `sourcePath`); `setExecCmd` is gone (build
     the run step around the wrapper, the kernel's `runTest`);
     `b.findProgram(.{ .names = ... })` returns an optional; `addFmt` takes
     LazyPaths; `std.zon.parse.fromSlice(T, .{ .gpa, .arena, .source,
     .diagnostics })`.
   - A dependency's `builder.root.root_dir.path` is relative to the
     top-level build, not to the dependency: resolve it through the
     dependency's own `root_dir.handle` (shale's build worked alone and broke
     under angry-gopher).
   - **Zig compiles only what is used**, so a library's function its own
     tests never call keeps its 0.16 code until a consumer's build reaches
     it (webbox's `edge.countsJSON`). Grep the whole set for the removed
     forms (`.fields`, `meta.fields(`, ` ** `) instead of trusting one
     repo's green build.
   - `linux.waitpid` and `wait4` fill an `i32`; `linux.W` reads a `u32`:
     hold the status as `i32` and `@bitCast` it for `W`.
   - `b.addOptionPath` takes a path relative to the build root now; a
     consumer that needs a sibling's root absolute (shale runs git in
     webbox's) resolves it with `realPathFileAlloc`.
   - A switch on Zig's tokens: `.asterisk_asterisk` is gone.
   - Tests that read old source from git history (shale's zig_move and
     zig_decls replays) parse 0.16 code with the 0.17 parser: translate
     its `**` first, as array_mult.shale would, on the same lines.
   - has_decl.shale's names are plain words: a builtin or local with the
     same name (shale's own `between`) is a false hit. Read its list
     before `fix=yes`.
4. **Then the behaviour 0.17 changed under the code**, which no compiler
   error shows and the repo's own tests must:
   - A generic's `@src().fn_name` ends `__func_<n>` (it was `__anon_<n>`):
     zig-coverage-sdk cut on the old suffix, and two of its tests failed.
   - `Reader.allocRemaining(.limited(n))` takes exactly `n` bytes and errors
     past it; 0.16 errored on reaching `n`. Code that passed `max + 1` to
     allow `max` now allows `max + 1` (webbox's readLimitedBody): pass `max`.
     A `max + 1` read followed by its own `len > max` check (shale's script
     source) is still right. Audit every `.limited(...)`.
   - `@hasDecl` sees only `pub` (above): `has_decl.shale` before the tests.
5. **Judge** with the repo's own tests, and commit to the branch, not
   master: the repos move together.

## Done so far

| repo | branch | fixes |
|---|---|---|
| zig-coverage-sdk | `zig-0.17` | typeInfo fields, Ast.parse, one array, the `__func_` suffix, fmt's builtins |
| zig-webbox | `zig-0.17` | 34 arrays and strings, 5 renames, 3 `pub` for @hasDecl, `build_root`, allocRemaining's limit |
| metal-vmm | `zig-0.17` | arrays, the optimize tags, `b.args`, bitsets, bufPrintZ, type info, 4 field loops, a comptime string, waitpid |
| zig-metal-unikernel | `zig-0.17` (in `~/zig17`) | 113 arrays and strings, 15 renames, six build APIs by hand, meta.fields, a test's false assumption about where a test heap lies; 1436 tests as on 0.16. **Its images wait on 0.17.1** (below) |
| observatory | `zig-0.17` (in `~/zig17`) | scripts only; 13 tests as on 0.16; its image waits on 0.17.1 |
| dog-park | `zig-0.17` (in `~/zig17`) | scripts, findProgram, the zon parser, an enum loop; `check` and `roc-fresh` green, the golden physics bit-for-bit; its image waits on 0.17.1 |
| shale | `zig-0.17` (in `~/zig17`) | 9 arrays, 36 renames, `build_root`, webbox's root absolute, waitpid/wait4, the `**` token, the history replays; 327 tests as on 0.16. `zig build consumers` waits for the siblings |

## The repos with siblings

shale, zig-metal-unikernel, angry-gopher, dog-park and observatory build
against their siblings by path (`../zig-webbox`, ...), so they move as a
set: a tree of `zig-0.17` worktrees side by side (each repo's branch
checked out under one folder, as the siblings' paths expect), every repo
ported there, the whole set judged together, then each branch merged.

## The blocker: compiler_rt on soft-float x86_64

0.17.0's compiler_rt does not compile for a soft-float x86_64 target: its
long-double functions (`truncl`, `sinl`, ...) are exported unconditionally,
and 0.17 refuses `c_longdouble` in a C-ABI function under soft float
(ziglang/zig#37006, a regression in 0.17.0's notes; the fix is aimed at
0.17.1). Every kernel image (the kernel's probes, observatory's, dog-park's,
angry-gopher's) needs compiler_rt and soft float, so none builds on 0.17.0.
Everything else ports and tests on 0.17.0.
