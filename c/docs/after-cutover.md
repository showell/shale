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

## Idioms to take up

The C should read the way agents normally write C (Steve, 2026-11-17).
During the port, some of it reads like Zig on purpose, so the fidelity
reviewer can line the two up. Once the Zig is gone, that reason is gone:

- **Names stop following Zig's.** A name is whatever reads best in C, and
  the idiom reviewer's renames go in. *Done (2026-11-17) for the bounds:*
  every `_MOST` and `_least` is `_MAX` and `_MIN` (style.md, "Names").
- **Zig's optionals become ordinary C.** A `has_x` flag beside `x` (as in
  `RunOpts`) becomes NULL for an absent pointer, or a sentinel or one
  flags field for the rest, wherever that reads better. *Done
  (2026-11-17):* `RunOpts` and job_start's and job_wait's options (nil
  values and `HOST_UNSET`), `SpawnSpec` and `SpawnOutcome` (an empty
  `cwd`, a NULL `stdin_bytes`, `SPAWN_UNLIMITED`, `SPAWN_NO_CODE`), walk's
  `depth` (`WALK_ANY_DEPTH`), the store's reads (`BUILTIN_TO_END`) and the
  CLI's script (`CliScript`). *Kept, on purpose:* `JobRecord`'s flags (a
  record is text, and every value of a field is one a record can hold),
  and walk's `after`, `like`, `skip`, `kind` and `keep_reads` (an empty or
  zero one given reads otherwise than none).
- `RegexCache` (6,856 bytes) and `Arena` were made by value
  (`regex_cache_make`, `arena_make`); an out-pointer initializer is the
  more ordinary C. *Done (2026-11-18, the run-context lane):*
  `arena_init(&a, limit)` and `regex_cache_init(&c)`.

## To follow up: coding standards declared in Roc

Steve wants to follow up on this idea (2026-11-17). It needs no change
to Shale's language, so it needn't wait for the cut-over.

A style rule lives today in several places that nothing ties together:

- the style guide's prose (roc-apps' `docs/roc-style.md`);
- a recipe's arguments, such as `tokens=6` or `min=4`;
- the plants that test the recipe;
- waivers like `skip=`, scattered wherever an agent put them.

The idea is to declare each rule once, as typed Roc data, in a small
`standards` package in roc-apps. Each rule carries:

- the guide's one sentence for it;
- its scope: all Roc, one tree, or one book;
- its check: a named recipe with its settings, a book's own setting, or
  `ByReader` where no script can see it;
- its exceptions, as an opaque type that refuses an empty reason;
- the code that first broke it.

A rule's id is a closed tag union, so the compiler makes every rule
documented and checked. A Roc program prints the standards as data, and
Shale's hunts read their settings, scopes and exceptions from it. The
policy lives in Roc and the mechanism stays in the recipes and their
plants. The strictness can be set per rule and per tree (held at zero,
against a baseline, or report only), which is how the quality gate's
debt gets paid down. The books' standards fit the same types: the word
limit per chapter, the negatives lint and the index terms.

The first steps are:

1. an inventory of every threshold, default and waiver across the Roc
   recipes, roc-apps' `tools/quality.shale`, the style guide and the
   books;
2. the package for the quality gate's rules alone, with the gate's
   counts unchanged;
3. the books' standards, as the second user.

Steve decided (2026-11-17) that the style guide stays wholly
hand-written. A gate fails when a rule's one-sentence summary is missing
from it, or when the guide names a rule the standards don't have.
