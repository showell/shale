# The C's style

Shale's C is written by agents, in one domain, in one style. Shale's own
tools need to read only this C, not arbitrary C, so the style is kept
regular enough for a small reader to split a file into its items and read
each one.

The foundation in `c/foundation/` follows every rule here.
`c/check_discipline.shale` holds the rules marked **held** on every build,
and its self-test plants a break of each one.

## Files

- The foundation is header-only. Each piece is one header: `arena.h`,
  `slice.h`, `buf.h`, `vec.h`, `map.h`, `status.h`, `err.h` and `test.h`.
- A header includes the headers below it, and nothing includes one above it.
  The order is slice, arena, status, vec, buf, map, err. `test.h` needs
  only slice.
- **The C builds as one translation unit** (Steve, 2026-11-17). A `.c`
  file's functions are all `static`, and a program or test `#include`s the
  `.c` files it needs, as `test_regex.c` includes `../script/regex.c`.
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
  - `SLICE_TYPE(`, `VEC_DEFINE(`, `MAP_DEFINE(`, `TEST(`, `TEST_MAIN(` or
    `_Static_assert(`;
  - `int main(`, a program's own: the one `.c` file that is a command
    (`c/shale.c`), and a test's fixture (`c/tests/host/fixtures/`).
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
- A struct returned by value is one 8-byte word or two of them, such as a
  pointer and a length (`Str`). Anything else goes through an out-pointer.
  TCC 0.9.27 miscompiles other shapes: a 12-byte return clobbered a local of
  its caller. The differ, run under TCC, stays the guard.

## Names

- A type is `PascalCase`: `Arena`, `RunCtx`, `StopClass`.
- A function is `snake_case`, starting with its piece's noun: `arena_alloc`,
  `buf_u64`, `str_eq`, `map_core_find`, `run_budget_stop`, `stop_report`.
- A function a macro makes is its type's name, an underscore, then the verb:
  `I64s_push`, `IntMap_put`.
- A constant is `UPPER_SNAKE`, starting with its type or piece:
  `CLASS_TYPE`, `BUDGET_ARENA`, `ARENA_BIG`. `OK` and `STOP` are the one
  exception.
- A macro that makes declarations or changes control flow is `UPPER_SNAKE`:
  `VEC_DEFINE`, `TRY`, `EXPECT`. A macro used as an expression is named like
  a function: `at`, `str_sub`, `buf_lit`. `S` is short, because every
  literal goes through it.
- A name Shale's Zig has keeps that name: `held`, `limit`, `chunk_least`
  becomes `ARENA_CHUNK_LEAST`, and `budgetStop` becomes `run_budget_stop`.

## Comments

- **Held:** comments are `//`. A comment inside a macro is `/* */`, on a line
  that ends with `\`.
- A function's comment says what it gives and when it refuses. It sits
  above the function.
- A file's comment says what the piece is, and which Zig it matches.

## Strings and indexing

- A string is a `Str`: a pointer and a length. A literal is `S("...")`. No
  string relies on a NUL at its end.
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

## Errors

- A function that can stop returns a `Status`: `OK`, or `STOP`. What it
  gives goes through an out-parameter.
- `TRY(expr)` returns `STOP` from the function when `expr` does.
- **Held:** a function returning `Status` is `STATUS_MUST_USE`, so GCC
  refuses an answer left unused, even one cast to `void` (`c/build.shale`
  plants one).
- The top level passes a run's answer through `run_ended`, which aborts on
  a `STOP` with no stop recorded.
- When an arena refuses, a container returns `STOP` and the arena's `hit`
  is set. The caller makes that the arena budget with `run_oom(ctx, pos)`.
  Every out-of-memory is the arena budget, as it is in Zig.
- A stop's place, class, budget, message, function and calls live in the
  run's `RunCtx`, never in a return value.
- A lookup that can miss returns `bool` and fills an out-parameter, or
  returns `NULL` or `SIZE_MAX`.
- A bug in the C (a bad index, an arena misuse, a stop that names no
  budget) aborts with one line on stderr. It is never a script's stop.
- A message a stop prints is Zig Shale's text, byte for byte. Where Zig
  formats a message and falls back to a plain one when memory runs out, the
  C does the same.

## Tests

- A test is `TEST(name) { ... }`. A file ends with `TEST_MAIN`, which names
  every case. A case that's left out is an unused static function, so GCC
  refuses it.
- `c/build.shale` holds each file's printed count to its `TEST(` lines.
- A test asserts every side: what changed, what didn't, and what was
  refused.
- `TEST_ONLY=a,b` runs only the cases it names; a name no case has fails
  the run.
- Each piece has planted faults in `c/build.shale`, and its full tier
  (`full=1`, run before a merge) fails unless a test catches each one. A
  plant names its `aim`, the tests that catch it, and its run is those
  tests alone.
- The fast tier, the loop between edits, runs every test under tcc (but
  those that wait out real time, `SLOW_TESTS`) and the discipline check, in
  about a second. The full tier adds ASan for the tests and -O2 for its
  warnings alone: no test is built or run at -O2.
