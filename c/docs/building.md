# Building C Shale: the ladder

This file says how C Shale is compiled, during the port and after it, and
why each build exists. Two priorities set its shape. During the port, the
C must build for Linux x86-64 quickly. After the cut-over from Zig, the
runtime must be fast on the work that matters: cross-file greps, hunts and
edit sweeps.

## The compilers

- **TCC** (the Tiny C Compiler) compiles in one pass with almost no
  optimization, about ten times faster than GCC does at `-O0`. `tcc -run`
  compiles and runs a file in one step. It is the inner loop and never
  builds what we ship. The box's TCC is Ubuntu's 0.9.27 snapshot, and it
  has bugs: it miscompiles a struct returned by value unless the struct is
  one or two 8-byte words, and it passes structs through `...` wrongly
  (`style.md` steers around both). The differential harness, run under
  TCC, is the guard.
- **GCC** builds everything else: the sanitizer builds, the warnings build
  and every binary we run. It needs no LLVM.
- **Clang** builds only the browser playground, later, since GCC and TCC
  can't target wasm. It is the one LLVM exception.

C Shale builds as one translation unit (`style.md`, "Files"), which suits
both ends: TCC compiles the whole program at once, and GCC sees every
function at once, so it inlines across modules as link-time optimization
would.

## During the port

`shale c/build.shale` is the fast tier; `shale c/build.shale full=1` adds
the rest. Run the fast tier on every edit. Run the full tier before a
merge, and defer it while you're about 90% sure it has nothing new to say.

| Build | Compiler | Tier | What it's for |
|---|---|---|---|
| Every test, built and run | TCC | fast | does it compile, do the tests pass |
| The discipline check and the C `shale --check` smoke test | TCC, Shale | fast | the style rules hold; the command still works |
| Every test under the sanitizers | GCC `-O0`, ASan and UBSan | full | use after free, overruns, leaks, undefined behavior |
| The code at `-O2 -Werror`, run nowhere | GCC | full | the warnings only the optimizer finds (`-Wrestrict`, some `-Warray-bounds`) |
| Every planted fault | TCC or GCC, as the plant says | full | each test proven able to fail |
| `c/shale.c` at `-O2` | GCC | full | the binary the differential harness judges against Zig Shale |

No test runs at `-O2` during the port (Steve, 2026-11-17): the
optimizer's value then is its warnings, and the sanitizers at `-O0` are
where the real bugs show up.

When the full tier passes 90 seconds, stop and propose ways to cut it,
ranked by what they'd save, before adding more to it.

## After the cut-over

Each step is measured with `bench/baseline.shale` against Zig Shale's
numbers, and taken only if it pays.

1. **GCC `-O2`, as one translation unit:** the default release build.
2. **A baseline CPU, chosen once:** `-march=x86-64-v2` or `-v3`, for the
   byte scanning that dominates greps. Measure both.
3. **Profile-guided optimization:** build once instrumented
   (`-fprofile-generate`), run the baseline harness's workloads, and
   rebuild with the profile (`-fprofile-use`). It typically buys 10 to 30%
   for an interpreter, with no code change.
4. **Hot spots by hand, last, and only where `perf` points:** the list in
   `after-cutover.md`.

TCC stays the development compiler for editing Shale's C. Coverage may move
to `gcc --coverage` then, if its counts mean what linecov's do.

## Summary

| Build | Compiler | When |
|---|---|---|
| Fast tier | TCC | every edit, during the port and after |
| Safety net | GCC `-O0`, ASan and UBSan | before a merge |
| Warnings | GCC `-O2 -Werror`, run nowhere | before a merge |
| Judged binary | GCC `-O2` | the differential harness, through the cut-over |
| Release | GCC `-O2`, a chosen `-march`, then profile-guided | after the cut-over |
| Playground | Clang to wasm | later, a tiebreaker |
