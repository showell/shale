# roc-types findings

## Roc nightly 11-07: an LLVM debug-info assertion (2026-11-08)

### Symptom

roc-types fails to build for Roc nightly 11-07. LLVM hits a debug-info
assertion and the compiler aborts (ABRT):

    FrameIndexExprs.size() == 1 || llvm::all_of(...)

It happens while compiling `-Osafe` for `x86_64-linux-musl` at the baseline CPU.

### The runs

All four build the same roc-types source against Roc 11-07, on showell/shale.

| Zig | Result | Run |
| --- | --- | --- |
| Steve's patched Zig 0.17.0+patched.2 | crash | [42504889408](https://github.com/showell/shale/actions/runs/42504889408) |
| Stock Zig 0.17.0 from ziglang.org | passes | [42508848075](https://github.com/showell/shale/actions/runs/42508848075) |
| Unpatched Zig 0.17.0, built against Zig's CI LLVM bundle `zig+llvm+lld+clang-x86_64-linux-musl-0.17.0-dev.203+073889523` | crash, the same assertion | [42511942056](https://github.com/showell/shale/actions/runs/42511942056) |
| patched.2 minus patch 1 | crash | [42511946042](https://github.com/showell/shale/actions/runs/42511946042) |

The experiment branches behind these runs are `roc-types-1107-stock-zig`,
`roc-types-1107-unpatched-ci-llvm` and `roc-types-1107-patched2-minus-zir` on
showell/shale, and `exp/unpatched-ci-llvm` and `exp/patched2-minus-zir` on
showell/patched-zig-compiler.

### Conclusion

Steve's patches are cleared. Unpatched Zig 0.17.0 crashes the same way once it
is built against an LLVM with assertions on. The bug is upstream, in Zig 0.17.0
with an assertions-on LLVM, and Roc 11-07's code triggers it. The stock release
passes because its LLVM has assertions off.

### Unknown

Whether the stock build's debug info is subtly wrong. With assertions off, LLVM
does not check the condition, so a pass says nothing about the output. A bisect
of the Roc commits between 11-03 and 11-07 is under way to find the change that
triggers it.

### Decision

Everything stays on Roc nightly 11-03, where roc-types already exists. The
11-07 work stays on the branch `roc-types-1107`.
