# The Zig cookbook

Recipes for finding a Zig file's structure and moving code along it,
written in Shale over its Zig builtins (`zig_decls`, `zig_tokens`,
`zig_move`, `zig_rename`, `zig_renames`, `zig_imports`: Zig's own
tokenizer and parser). They are recipes to read, copy and change for the
job in front of you. Each script's header is its full documentation, its
arguments included. Each is held to parse and compile (`shale --check`) by
C's full tier, `shale c/build.shale full=1`.

Run one from the checkout it works on. `<shale>` is the shale checkout;
`<repo>` is the repo the recipe works on, its root the current directory:

    cd <repo> && shale <shale>/cookbook/zig/find_seams.shale path=src/x.zig

**A recipe says the room it needs.** The tree-wide recipes hold each
file's tokens for the run, far past the CLI's default budgets. Each states
its budgets in a line among its opening comments,

    -- budget: steps=900000000 arena_bytes=4000000000

which `shale` takes and prints before the run, so the cost is in sight. A
`--budget` on the command line still wins.

**A value with a glob, a space or a `|` goes on stdin**, after
`--args-stdin`, one `NAME=VALUE` a line, raw, since the gate passes plain
words alone. Every tree-wide recipe's `glob=` defaults to `src/**/*.zig`;
another is given this way:

    cd <repo> && shale <shale>/cookbook/zig/find_smells.shale --args-stdin <<'ARGS'
    glob=probe/**/*.zig
    ARGS

A recipe that changes several files runs with `--undo-on-stop`, so a stop
puts them back:

    cd <repo> && shale --undo-on-stop <shale>/cookbook/zig/split_file.shale --args-stdin <<'ARGS'
    from=src/x.zig
    to=src/x_part.zig
    as=x_part
    names=a|b|the part's test, with a comma
    doc=What the part is.
    ARGS

## Finding the seams

- `find_seams.shale`: the file's reference graph (`zig_decls`' `uses`),
  its hubs, the cut profile in file order, and **each declaration grouped
  by the hubs that reach it**: the structure by caller, which is where a
  hidden seam shows.
- `plan_function_cut.shale`: what a cut of a long function would read,
  write and give back: the parameters an extracted function would need.
- `map_file.shale`: a file's top-level declarations and tests, with their
  lines.
- `list_long_files.shale`: every `.zig` file over a thousand lines in the
  repos it names (edit its list for your box). It runs from the folder that
  holds the repos. It is the launcher of a Roc script,
  `list_long_files/list_long_files.roc`, on Shale's Roc platform.

## Smells: where a tree may want a second look

Lists for a reader, read from Zig's tokenizer, so comments and strings are
skipped:

- `check_stacking.shale`: **three or more, one a line**: a literal, a
  call's or a function's arguments, a container or error set of three or
  more items on one line, and any line over 100 columns. `write=1` gives
  each a trailing comma and runs `zig fmt` until the file settles. Like its
  Roc twin, it reads characters, stepping over strings, `\\` lines and
  comments.
- `find_smells.shale`: long functions, long parameter lists, `bool`
  parameters and fields, `catch {}` and `catch unreachable` by file, and
  numbered steps in comments (a caller's numbered protocol wants to be
  code).
- `hunt_names.shale`: **awkward names, a question each**: an effect named
  as a state, a question that returns some other type than bool, a name of
  vague words only, a `*count` or `*path` of the wrong type, a negative
  flag. One plant a check in `plants/hunt_names/`.
- `find_bool_flags.shale`: each function with a `bool` parameter and the
  calls that pass it a bare `true` or `false`, where a named enum would
  read better; a bool every call passes the same literal is marked `ONE
  VALUE`, a parameter to drop.
- `find_dead_code.shale`: the declarations nothing uses, which Zig's lazy
  analysis keeps quiet about. Dead: private, and named nowhere else in its
  file, which is certain. Unreached: `pub` and named nowhere in the tree,
  which is a question (another repo, a build, `@field`). `drop=yes` removes
  the dead; run it under `--undo-on-stop`, again until none is left, and
  let the tests judge.
- `find_repeated_params.shale`: a run of parameter names that several
  functions declare in a row (`io`, `alloc`, ...), plumbing that may want
  to be a struct.
- `find_clones.shale`: the same code in two places, runs of alike lines in
  one file or two. Code may be alike on purpose: each find is a question.
- `count_comments.shale`: per Zig file, its code, doc and plain comment
  lines, the ratio, and the loud and dated comments.
- `hunt_alignment_math.shale`: where a compiler copies alignment
  arithmetic instead of asking the one place that knows it. Written for the
  Roc compiler (roc-lang/roc#12641 is its archetype).

## Moving along them

- `rename_member.shale`: a declaration, or a container's member, renamed
  in its file and through every importing file's `alias.from` across the
  tree; what reaches it another way is listed by line for you to judge.
- `split_file.shale`: declarations moved to a new file and the old one made
  to use them: `zig_move`, `pub` where needed, the new file's doc and
  imports, the old file's uses qualified or aliased, and the new file added
  to the old one's `test {}` block.
- `add_missing_pubs.shale`: builds, and puts `pub` where the compiler says
  a method called through a value needs it, until it builds.
- `rewrite_call_args.shale`: every call of a function, its arguments
  regrouped by a template of `$N`s (`$1, .{ .dir = $2, .key = $3 }`), the
  call sites' half of a new signature. The declaration is yours to change.
  Run it before changing any call by hand: a call already in the new shape
  has the wrong count and stops the run. The body's half, when parameters
  become one struct's fields, is the builtin `zig_renames` over the body's
  lines; change the signature first.
- `tidy_file.shale`: an alias moved after the import it names; empty
  section banners removed.

## Judging a test suite

- `flip_bools.shale`: each bare `true` or `false` a production call
  passes, flipped alone, and the build's tests run; a flip that survives is
  a decision the tests leave open. It runs a test suite per flip, so run it
  detached, as a hunt. Read each survivor: code the step never runs
  survives too.

## Judging a move

- `judge_move.shale`: a pure move kept the behaviour: the tests by name
  and the runner's count, and each binary's symbols by name and size.
- `check_same_tests.shale`: a checkout's suite against a baseline of test
  names, wherever a moved test now lives.
- `check_const_unchanged.shale`: a top-level `const` as `rev` had it and as
  the file has it now, compiled side by side and compared by Zig, byte for
  byte: for a page's text spliced from a shared part (`a ++ shared ++ b`),
  which a symbol judge can't see.
- `check_comments_only.shale`: a change that touches comments only, by
  Zig's tokenizer; otherwise the first token that differs.

## Writing a recipe

docs/SHALE.md's "Before you send a script: the author's checklist" lists
what a recipe's author meets first.
