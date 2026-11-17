# After the cut-over: what the C makes possible

Until C Shale matches Zig Shale byte for byte, the language is frozen and
nothing here is built. This file holds the ideas for after the cut-over,
in the order they are meant to be taken up, with the rules Steve set for
them on 2026-11-17.

## The rules

- **No foreign-function interface in the language.** A script that can
  call arbitrary C escapes every budget, mount and secret rule. New power
  arrives as reviewed builtins.
- **Restraint with dependencies.** A small single-file library may be
  vendored on purpose and named in one place. A large one has to beat a
  spawned process in the baseline (bench/) first.
- **A builtin's budget may rest on an outside proof.** Where a library is
  known to run in linear time in its input, the builtin charges a budget
  computed from the input's size, rather than counting steps inside the
  library.
- **An in-process builtin respects mounts and the secrets rule by hand,**
  which `run` gets free at the process boundary. Its tests plant a secret
  path to prove it.
- **One consistent C style.** The C is written by agents, in one domain,
  in one style, so Shale's own tools need to read only that C, not
  arbitrary C. Keeping the style regular keeps that door open.

## The order

1. **Startup, and the builtins that cross many files.** These are the
   rewrite's stated goal: `grep_tree`, the tree readers and the edit
   sweeps. Use `mmap` for reads, libc's vectorized `memchr` for the
   literal search, and cheaper process starts for `run` where the
   death-signal and process-group setup allow. Each change is measured
   with bench/baseline.shale.
2. **In-process git reads:** objects, trees, refs and status, written
   small, here. git is the program Shale runs most. Pushes and merges
   stay with the real `git`.
3. **`libshale`,** a C library with one entry point, so a Roc platform's
   host (Zig, which links C directly) can carry a bounded scripting layer.
   It stays one function and one report struct until a second embedder
   needs more.
4. **A static musl build,** one file that runs on any Linux box. Lower
   priority, and so is a Cosmopolitan build for macOS and Windows. Each
   build is held by the differential harness.
5. **Tooling swaps:** gcov in place of linecov if its counts mean the
   same, perf and callgrind for profiling, and AFL++ for fuzzing the
   parser. Then **hunts over Shale's own C,** reading the C this file's
   style rule keeps regular.
6. **Parallelism, last.** On a two-core box, overlapping I/O can win, but
   a race or an order that differs between runs would make Shale lie. It
   belongs inside a builtin with a deterministic merge, never in the
   language.
