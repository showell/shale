# The C's style

Shale's C is written by agents, in one domain, in one style. Shale's own
tools need to read only this C, not arbitrary C, so the style is kept
regular enough for a small reader to split a file into its items and read
each one. That reader lives twice, as two copies of one logic. In Shale it
is `c/build/c_items.shale`: the fast tier splits every file with it
(c/build/discipline.shale), so a file it can't read fails the build, and
the discipline check reads the C through its masker. Its port to Roc,
`cookbook/c/items/`, is the splitter the Roc recipes over the C share
(`cookbook/c/map_file.shale` and the others), held to the Shale one's
output by their goldens. The fast tier builds no Roc, so it keeps the Shale
copy.

The foundation in `c/foundation/` follows every rule here.
`c/check_discipline.shale` holds the rules marked **held** on every build,
and its self-test plants a break of each one.

## Files

- The foundation is header-only. Each piece is one header: `arena.h`,
  `slice.h`, `buf.h`, `vec.h`, `map.h`, `status.h`, `err.h`, `wyhash.h`,
  `path.h` and `test.h`.
- A header includes the headers below it, and nothing includes one above it.
  The order is slice, arena, status, vec, buf, map, err. `test.h` and
  `wyhash.h` need only slice; `path.h` needs buf and wyhash.
- **The C builds as one translation unit** (Steve, 2026-11-17). A `.c`
  file's functions are all `static`, and a program or test `#include`s the
  `.c` files it needs, as `test_regex.c` includes `../script/regex.c`.
- A `.c` file's function that some unit including it never calls (the
  CLI's report, the reaper, a run's stop, which a test unit may not use) is
  `static inline`, so GCC's unused-function warning passes it by there.
- A test file is `c/tests/test_<header>.c`. It includes `test.h` first.
- **Held:** a header's first line is `// <its name>: ` and then what it is.
- **Held:** a header's guard is `SHALE_<NAME>_H`, and its last line is
  `#endif`.
- System headers come first, in angle brackets. The foundation's come next,
  in quotes.

## The top level

- **Held:** a line at column 0 starts one of these items, or closes one
  with `}` or `)`:
  - a `//` comment or a `#` line;
  - `static inline`, or `static` in a `.c` file;
  - `typedef` or `enum`;
  - `SLICE_TYPE(`, `VEC_DEFINE(`, `MAP_DEFINE(`, `TEST(`, `TEST_MAIN(`,
    `TEST_PART(`, `TEST_PARTS_MAIN(` or `_Static_assert(`;
  - `int main(`, a program's own: the one `.c` file that is a command
    (`c/shale.c`), a test's fixture (`c/tests/fixtures/`,
    `c/tests/host/fixtures/`) and a compiler's canary (`c/tests/canaries/`).
- Everything inside an item is indented, so the next line at column 0 starts
  the next item.
- **Held:** a function in a header is `static inline`.

## Layout

- **Held:** four spaces a level. There are no tabs.
- **Held:** an opening brace ends the line that opens it. A function's brace
  does too.
- **Held:** `else` follows its `}` on the same line: `} else {`.
- A one-statement body may share its `if` or `for` line, with no braces:
  `if (c == NULL) return NULL;`. A body longer than one line has braces.
- The `*` of a pointer sits on the name: `char *p`, `Arena *a`.
- A line may run past 100 columns where breaking it would split a
  declaration or a message.

## Declarations

- **Held:** one declaration per statement. `size_t len, cap;` is two lines.
- **Held:** a declaration with `=` ends its line, with `;`. A braced
  initializer may open on its line with `{` and close on a later one.
- A struct's fields are one to a line, each with a comment where its name
  doesn't say enough.
- **Held:** a struct returned by value is at most 8 bytes, exactly 16 (two
  words, like `Str`), or more than 16; never 9 to 15. TCC 0.9.27 miscompiles
  a return of 9 to 15 bytes that goes straight into another call as its
  argument, `f(g())`, though the same return kept in a local first is right.
  It is known upstream ([tinycc-devel, July
  2023](https://lists.libreplanet.org/archive/html/tinycc-devel/2023-07/msg00027.html))
  and fixed on TinyCC's mob branch after 0.9.27; the discipline check sizes
  every return through a probe, and `c/tests/canaries/tcc_struct_return.c`,
  which alone holds the shape, is the canary that says when the rule can
  go.

## Names

- A type is `PascalCase`: `Arena`, `RunCtx`, `StopClass`.
- A function is `snake_case`, starting with its piece's noun: `arena_alloc`,
  `buf_u64`, `str_eq`, `map_core_find`, `run_budget_stop`, `stop_report`.
- A prefix is a whole word, or words: `builtin_split`, `message_format`,
  `fixture_init`, not `bi_`, `msg_` or `fx_`. C's conventional ones are
  kept: `str_`, `buf_`, `mem_`, `vm_`, `tty_`, `lex_`, `json_` and `c_` (as
  in `c_bug`), and so is a one-letter parameter (`a`, `b`, `r`, `s`).
  `ran_` is a word: what ran.c ran.
- A function a macro makes is its type's name, an underscore, then the verb:
  `I64s_push`, `IntMap_put`.
- A constant is `UPPER_SNAKE`, starting with its type or piece:
  `CLASS_TYPE`, `BUDGET_ARENA`, `ARENA_BIG`. `OK` and `STOP` are the one
  exception.
- A macro that makes declarations or changes control flow is `UPPER_SNAKE`:
  `VEC_DEFINE`, `TRY`, `EXPECT`. A macro used as an expression is named like
  a function: `at`, `str_sub`, `buf_lit`. `S` is short, because every
  literal goes through it.
- A name is whatever reads best in C, not what Shale's Zig called it
  (`after-cutover.md`, "Idioms to take up"). Until the cut-over the C kept
  Zig's names so the two lined up; Zig's `chunk_least` is now
  `ARENA_CHUNK_MIN`.
- A bound is `_MAX` or `_MIN`, as C's own are (`SIZE_MAX`, `PATH_MAX`):
  `REGEX_PATTERN_MAX`, `ARENA_CHUNK_MIN`, not Zig's `_most` and `_least`.

## Comments

- **Held:** comments are `//`. A comment inside a macro is `/* */`, on a line
  that ends with `\`.
- A comment is its consecutive whole comment lines: a blank line or a line
  of code ends it.
- **Held by folder:** a `//` comment line wraps at 80 columns. A comment
  after code on its line is not judged, since the code sets that line's
  width, and neither is a macro's `/* */` line, padded out to its `\`.
- **Held by folder:** every `DRAGON:` is answered in its own comment, before
  the next `DRAGON:`, by a line that starts `REMEDY:`, saying what keeps the
  guarantee:

  ```c
  // DRAGON: <the guarantee at stake>. <what would go wrong>.
  // REMEDY: <what keeps it safe>.
  ```

  A `DRAGON:` starts its comment's line. A comment that points at one says
  "the DRAGON at buf_view", with no colon: a `DRAGON:` mid-line (outside
  backticks) is a finding of its own, since it reads as a marker waiting
  for a `REMEDY:`.

- **Held by folder:** no comment cites a Zig file's line (`vm.zig:123`).
- The rules held by folder are reported everywhere: the discipline check
  prints every finding, and fails on them only in the folders its `HELD`
  list names (`c/foundation`, `c/script`, `c/host`, `c/tests`, or
  `c/shale.c`). A folder joins `HELD` once its comments are clean.
- A function's comment says what it gives and when it refuses. It sits
  above the function.
- A file's comment says what the piece is.

## Strings and indexing

- A string is a `Str`: a pointer and a length. A literal is `S("...")`. No
  string relies on a NUL at its end.
- **A string the kernel takes is an `OsStr`** (slice.h): a Str known to hold
  no NUL, made only by `os_str_of` (false for a NUL) and `os_str_own`
  (Shale's own string, whose NUL is a bug in the C). A script's string
  becomes one through the host's `os_str` (host.c), which refuses a NUL
  as CLASS_REFUSED naming the argument; a config file's through its parser
  (mounts.c's `mounts_parse`, naming the line). `str_cstr_into`,
  `arena_cstr` and `file_system_cstr` take an OsStr, so their one refusal
  is no room. **Held:** no search of a string for a NUL outside
  `os_str_of` (`NUL_SEARCHERS`), but one that says `// nul: <why>` (a
  binary file's sniff); and no name `RETIRED` lists.
- A table of names for an enum is sized by its names, `names[]`, never by the
  enum, with `_Static_assert(sizeof names / sizeof *names == X_COUNT, ...)`:
  sized by the enum, a name left out is zero-filled and reads as "". Its
  items are `S_ITEM("...")`, since TCC 0.9.27 refuses an unsized array of
  `S`'s compound literals. `c/build.shale` drops a name from each and must
  see the assert refuse it.
- **Held:** a header has no `char *` parameter or field without its length.
  A field `ptr` is followed by `size_t len` on the next line. Anything else
  carries `// cstr: <why>` on its line, as `__FILE__` does.
- Every index goes through `at(s, i)`, which checks bounds in every build.
  A bad index ends the process: it is a bug in the C, not a stop of the
  script.
- **Held:** there is no raw `[i]` index of anything (a slice's `ptr`, a
  field, a local array) and no `*(ptr + i)` outside `slice.h`. An array's
  size in its declaration is no index.
- **Held:** a `mem*` call has no `+` or `-` in its arguments outside
  `c/foundation/`, and inside it only under a `DRAGON:` comment. A checked
  part, `&at(s, i)` or a `SLICE_SUB`'s `ptr`, needs neither.
- **Held:** no `memcpy(`, `memmove(`, `memcmp(`, `memchr(` or `memmem(`
  outside `c/foundation/`: slice.h's `mem_copy`, `mem_move`, `mem_compare`,
  `mem_find_byte`, `str_find_byte` and `mem_find` make no call for 0 bytes.
  `mem_find`, memmem's, is there only for a unit that defines `_GNU_SOURCE`
  before its first include, since memmem is a GNU extension. A mem* call
  on NULL is undefined in C17 even for 0 bytes, an empty Str may hold NULL,
  and glibc marks the arguments nonnull, so GCC may delete a later NULL
  check. Inside the foundation a raw call is in `mem_copy`, `mem_move`,
  `mem_compare`, `mem_find_byte` or `mem_find` alone, under a `DRAGON:`:
  every other function calls one of them.
- **Held:** a `SLICE_SUB`'s arguments have no `++`, `--`, `=` or call: it
  evaluates them more than once. `str_sub` is a function and takes any.
- A loop that has proved its own bounds may use `at_unchecked(s, i, "why")`.
  The compiler refuses one without a non-empty literal reason.
- A buffer's spare room is indexed through a slice of its capacity:
  `Bytes room = {b->ptr, b->cap}; at(room, b->len) = c;`.

## Memory

- **Held:** nothing outside `arena.h` calls `malloc`, `free`, `calloc` or
  `realloc`.
- Every byte comes from an `Arena`, whose limit is a budget. The bytes held
  need not match Zig's (`docs/C-SEAMS.md`, "Fidelity"), but a run that stops
  on the arena budget under the Zig stops on it under the C, with Zig's
  message.
- A container takes its arena when it is made (`buf_make(a)`,
  `I64s_make(a)`) and keeps it.
- An arena and the regex cache are made by an out-pointer initializer,
  `arena_init(&a, limit, &ctx->refused)` and `regex_cache_init(&c,
  &ctx->refused)`, not returned by value.

## State

- **Everything a run owns hangs off the run and is passed, never a static**
  (Steve, 2026-11-18): as Odin's implicit `context` and Zig's explicit
  allocators and `Io` are passed, so are the run's things here. Its arenas,
  its refusal, its regex cache and its zig-ast are on its `RunCtx`, its
  `Run` or its `Host`, and a function that needs one takes it. Two runs in
  one process (the tests' runner makes many) share nothing.
- A run's arenas are made with its refusal, `&ctx->refused`; one outside any
  run (a test, the CLI's own setup and parse, the gate) points at a local of
  the code that made it.
- What a run starts and ends is held by the run's owner and handed in:
  c/shale.c's run and the tests' `Ran` each hold a `HostZigProc`, which
  the host's `zig` field points at, and each ends it.
- **Held:** no mutable variable of static storage, at file scope or inside a
  function, but those named in `STATICS_KEPT` in c/check_discipline.shale,
  each with why it is the process's and no run's: slice.h's bounds trap,
  test.h's runner state and the compiler canary's counts. A `const` one
  passes. So does a test's own in its function (`static Ran x;`), which
  keeps a run off the C stack and is that test's alone.
- What the process has once is no variable, and stays the process's: the
  signal dispositions (`spawn_quiet_sigpipe`), a child's death signal, and
  the environment `getenv` reads.

## Errors

- A function that can stop returns a `Status`: `OK`, or `STOP`. What it
  gives goes through an out-parameter.
- `TRY(expr)` returns `STOP` from the function when `expr` does.
- **Held:** a function returning `Status` is `STATUS_MUST_USE`, so GCC
  refuses an answer left unused, even one cast to `void` (`c/build.shale`
  plants one).
- The top level passes a run's answer through `run_ended`, which aborts on
  a `STOP` with no stop recorded.
- When an arena refuses, a container returns `STOP`, and the arena sets its
  own `hit` and its run's refusal, `RunCtx.refused`, which every arena of
  the run is made with (`arena_init(&a, limit, &ctx->refused)`). The caller
  makes that the arena budget with `run_oom(ctx, pos)`, which reads and
  clears `ctx->refused` and aborts when no arena of the run refused. Every
  out-of-memory is the arena budget, as it is in Zig.
- A stop's place, class, budget, message, function and calls live in the
  run's `RunCtx`, never in a return value.
- A lookup that can miss returns `bool` and fills an out-parameter, or
  returns `NULL` or `SIZE_MAX`.
- **A refusal is decided where its cause is known, its answer names one
  cause, and none is swallowed** (Steve, 2026-11-18). A NUL is refused
  where the string is read, not as the "out of memory" a converter far
  below would give; a helper that can fail for several causes returns which
  (an enum, an errno), never a bare `false` its caller reads as one of
  them; and a failure a caller passes by says why the skip is the answer.
  **Held:** a one-line test of a failure that skips on (`if (e != OK)
  return OK;`, `== NULL) continue;`, `< 0) break;`) carries `// skip:
  <why>`, and a kernel call whose answer is cast away (`(void)unlink(p)`)
  carries `// ignored: <why>`.
- A function that holds OS resources (a file descriptor, a child process)
  releases them on its error paths through one `goto` label at its end, as
  ordinary C does: `fail:` closes what is open and returns `STOP`. `goto` is
  for that alone. Memory needs none, since the arena owns it.
- A bug in the C (a bad index, an arena misuse, a stop that names no
  budget) aborts with one line on stderr. It is never a script's stop.
- A message a stop prints is C Shale's own text, held byte for byte by the
  golden tests (`tests/golden.shale`); a change to one is a named refreeze
  (`refreeze=<ids>`), its commit saying why (Steve, 2026-11-18: no parity
  with the Zig). A message formatted with arguments falls back to its plain
  format when memory runs out, and the fallback reads the refusal
  (`run_fail_built`), so no stale `RunCtx.refused` outlives the stop.
- **No refusal is dropped.** A number for a message comes from the run's
  ring (`run_num`, `run_unum`), which needs no arena; anything else a run
  makes, a `--verbose` record or the time line's argv among them, answers a
  refusal as the arena budget's stop. `c/build/arena_faults.shale` proves it
  on demand (`c/docs/building.md`).
- **Held:** no `run_fail(` has a `{` or `}` in its literal. Zig's format
  escapes a brace (`{{`), and a refused arena shows that raw format, so the
  stop goes through `run_failf` or `path_fail` with the format as its
  fallback (`docs/C-SEAMS.md`).

## Tests

- A test is `TEST(name) { ... }`. A file ends with `TEST_MAIN`, which names
  every case. A case that's left out is an unused static function, so GCC
  refuses it.
- `c/build.shale` holds each file's printed count to its `TEST(` lines.
- A test file that includes `c/tests/ran.c`, the whole interpreter, is a
  part of `c/tests/ran_tests.c` instead of a binary of its own: it ends
  with `TEST_PART(name, ...)`, and the binary prints each part's count in a
  line of its own (`c/docs/building.md`).
- A test asserts every side: what changed, what didn't, and what was
  refused.
- A call that must give OK, where a run's context is in scope, is
  `EXPECT_OK_IN(ctx, expr)`: on a failure it prints the stop the context
  recorded (its place, class, budget and message), so a failing host or
  file system test says why. A Ran's context is `&x.r.ctx`, the path fixture's
  `&f.ctx`. `EXPECT((expr) == OK)` stays for a call that has no context,
  as a buffer's append has none.
- A table of cases is counted by the compiler, never by hand: a list of
  literal strings is `Lits xs = LITS("a", "b");` (test.h), and a table of
  structs is `{cases, sizeof cases / sizeof *cases}` over an unsized
  array (`S_ITEM` for a `Str`'s, as above). The count sizes the loop over
  the cases alone. **A count the test asserts stays a literal number**
  (`EXPECT_EQ(ms.len, 5)`): one computed from the table agrees with any
  table, so it would hold whatever the code did.
- `TEST_ONLY=a,b` runs only the cases it names; a name no case has fails
  the run.
- Each piece has planted faults in `c/build.shale`, and its full tier
  (`full=1`, run before a merge) fails unless a test catches each one. A
  plant names its `aim`, the tests that catch it, and its run is those
  tests alone. A plant is a one-time proof: once caught it is skipped
  until its text, its file or its tests' files change, and `full=1
  plants=all` runs every one (`c/docs/building.md`).
- The fast tier, the loop between edits, runs every test under tcc (but
  those that wait out real time, `SLOW_TESTS`) and the discipline check, in
  about a second. The full tier adds ASan for the tests and -O2 for its
  warnings alone: no test is built or run at -O2.
