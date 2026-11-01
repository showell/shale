# The Roc cookbook

Recipes for keeping a Roc tree tidy and judging a change to it, written
in Shale over `run` (`roc`, `objdump`). Like the Zig cookbook's, they are
recipes, not builtins: read one, copy it, change it for the job. Each is
tested to parse (`src/shale_cookbook_test.zig`). Run one from the checkout
it works on, `<shale>` being the shale checkout:

    cd <repo> && shale <shale>/cookbook/roc/roc_commas.shale dir=roc width=100

- [name_hunt.shale](name_hunt.shale): **awkward names, a question each**,
  the Zig hunt's five checks over Roc's annotations and definitions, the
  top level's and module types' members (`held_raw!` named as a state;
  `rounded_rectangle!`, whose annotation names `RoundedRectangle`, is not).
  It reads Roc's own parser's tree (roc-ast, `tools:roc-ast/bin/roc-ast`,
  built by Shale's `tools/roc_ast/install.shale`), so a broken
  annotation or a `where` clause reads as Roc reads it. A name is judged
  once, from its annotation when it has one (a hosted effect has nothing
  else); a `!` that names a callback's effect (`map2!`) and a name the
  language fixes (`is_eq`, `subscript`) are not judged as the author's.
  An effect that starts with a namespace prefix (cookbook/words/namespaces.txt,
  `block_read_sector!`, `port_out_32!`) is judged by the word after it,
  and one whose first argument is a surface it draws on (`Frame`,
  `Canvas`: `circle! : Frame, Circle => {}`) is named for what it draws.
  Beside the names: `command_tense` and `mixed_tense` (the tags an effect
  is handed, a `List(T)` judged as `T`, named as events, or as commands
  and events at once) and `container` (a type or record field named only
  by container words, cookbook/words/containers.txt, unless its doc says
  whose: `## A folder's entry`), and `short_name` (a top-level or
  module-member name of one letter, `w : F64`; a local's or a lambda
  parameter's never), and `worker_name` (a definition named only as a
  helper, `_acc`, `_loop`, `_inner`, `_helper`, `_rec`, or a `_step`,
  `_walk` or `_go` that calls itself: name the worker for the parameter it adds,
  `_from`, `_onto`, `_with`, and give the entry function the plain name),
  and `stacked_preposition` (a name whose last two words are both
  prepositions after a word that is none, `rest_from_onto`, never
  `at_of`, whose `at` is a noun: a worker's suffix on an entry that
  already ends in one; the entry says what it gives, `rest`, the worker
  what it adds, `rest_onto`).
  `self_test=1` runs its plants (`plants/name_hunt/`), and fails on a
  plant named for no check.
- [roc_commas.shale](roc_commas.shale): **long lines laid out by `roc fmt`.**
  `roc fmt` never wraps a line, and it joins hand-made breaks back. A
  trailing comma after a record's, call's or lambda's last item makes it
  lay that group out one item a line. This puts one on each long line's
  widest group, runs `roc fmt`, and repeats until nothing changes. It lists
  what it can't reach: strings, long conditions, a function type's bare
  arguments.
- [stack.shale](stack.shale): **three or more, one a line.** Any list,
  record, tuple, call's or lambda's arguments or tag payload of three or
  more items on one line is a finding, each group its own, and so is a line
  over 100 columns with none, and a function type's three or more
  arguments, bare (`U8, U8, U8 -> U8`)
  or in parentheses (`(U8, U8, U8 -> U8)`), which no trailing comma stacks.
  `write=1` gives each a trailing comma, outermost first (never a function
  type's arguments), and runs `roc fmt` until nothing changes; a second run
  changes nothing. Strings and comments are never read as items, nor is a
  record pattern's `..`; a `where` clause's items are its method
  constraints. The Roc is the tree's own pin, `tools:roc-<pin>/roc` with
  the pin read from the `roc-version` where the run starts; `write=1`
  without one stops before any change. `self_test=1` runs its plants
  (`plants/stack/`), then rewrites copies of them in `scratch:` with the
  recipe's `SELF_TEST_PIN`: each must still parse, and a second rewrite
  change nothing.
- [nest_depth.shale](nest_depth.shale): **a call nested three deep**,
  `f(g(h(x)))`, read inside out: the outermost call of each nest `min`
  (3) or more deep, from `roc_tree` (roc-ast's tree, read in the host),
  `path:line:column`. A method's receiver, a pipe's or
  an arrow's left side, a tag, `?`, parentheses, an operator and a field's
  or tuple's access are no level; a lambda,
  record or list argument starts again (`loose=1` passes depth through the
  last two). `self_test=1` runs its plants (`plants/nest_depth/`).
- [boundaries.shale](boundaries.shale): **a function's boundary, from its
  references.** `single`: a wrapper (one small expression, by its tree,
  not its lines; a decision of two ways at most) with one production
  caller, which no `expect`, test file (a twin, `test/`, a
  `<Module>Spec.roc`) or `##` doc keeps, and which is neither a recursion
  nor an entry into one (a rule of thumb, not a gate); `fixed`: a
  parameter every production call fills with the same literal, a
  recursion's own calls counted, never context passed down (`me`,
  `req.me`); `entry`: a recursion's start, the same literal at every
  production call that its own calls vary (`at + 1`), which wants an entry
  function that takes only what varies, not dropping (quiet once that
  entry is its one outside call); `constant`: a function of
  `{}` alone with no `!` in its name, which gives the same value every
  call. A count of callers is of the files the glob reads, and a finding
  says how many.
  `self_test=1` runs its plants (`plants/boundaries/`).
- [errors.shale](errors.shale): **a failure said by its tags.**
  `error_alias`: a function whose result's `Try` names a shared alias of
  tags, not its tags in place (an alias in an argument, a handler's input,
  is fine; a record or `Str` error is no alias of tags); `optional_tag`: a tag named as an optional (`None`,
  `Some`: cookbook/words/optionals.txt); `set_fallback`: `List.set(l, i,
  v) ?? l`, the copying shape roc-apps CLAUDE.md names first ("Six shapes
  copy"); `placeholder`: a `??` falling back to a string literal (`??
  "bogus"`), where the guide says to crash on the impossible. `self_test=1`
  runs its plants (`plants/errors/`), and fails on a plant named for no
  check.
- [same_code.shale](same_code.shale): **the same machine code.** Two builds
  of an archive, compared instruction by instruction with Roc's hashed
  procedure names (`roc__p` and 32 hex digits, which move with the source's
  layout) masked. It is the judge of a pure reformat, whose archive bytes
  change though its code doesn't. An archive objdump can't read (wasm32) is
  refused, never passed.
- [tree_patch.shale](tree_patch.shale): **a tree's change as a patch.**
  Every file under a changed copy against the original, as one unified
  patch with `a/` and `b/` paths that `patch -p1` applies from the repo's
  root. Dog Park's new-kind proof (`test/new_kind/sheep.patch`) is rebuilt
  this way whenever the Roc it patches moves.
- [methods.shale](methods.shale): **old-style calls as method calls.**
  `Module.fn(first, rest...)` becomes `first.fn(rest...)` where that is the
  same call (`List.map(xs, f)` is `xs.map(f)`), a chain from the inside out
  (`List.len(List.keep_if(xs, p))` is `xs.keep_if(p).len()`). The calls are
  roc-ast's; **the compiler is the judge**, since a tree can't say a type:
  each file's rewrite is kept only when `roc check` on `root=` passes and
  `roc test`'s summary is unchanged. An error on a rewritten call's line
  leaves that call, its reason the error's title, and the file is tried
  again; any other error puts the file back. No candidate: a call used as a
  function value, a module's own call in its file; the right of a `|>` is
  listed and left. A dry run by default (each trial put back; run it with
  `--undo-on-stop`, so a stop mid-trial puts that back too); `apply=1`
  keeps them. Out: the counts, a line each call left, `path:line: why`,
  then the counts again. A run holds 64 programs: on a bigger tree it stops before a file
  and says `go on with from=N`, the glob's file to start the next at.
  The compiler is the tree's own pin, `tools:roc-<pin>/roc` with the pin
  read from the `roc-version` where the run starts; a run without one
  stops, since method lookup has changed between nightlies. `types=1`
  asks the checker first (`types.shale`, below): a candidate whose first
  argument is a concrete other type is left untried. `self_test=1` runs its
  plants (`plants/methods/`, copied to `scratch:`) with `SELF_TEST_PIN`,
  each in a child run of its own, so each has a run's 64 programs.
- [pinned.shale](pinned.shale): **the Roc a tree pins**, a library, not a
  recipe: `use("cookbook:roc/pinned.shale")` gives `roc_bin(who,
  self_test)`, the pinned `roc`'s path (`tools:roc-<pin>/roc`, the pin from
  `roc-version` where the run starts), and `SELF_TEST_PIN`, the one Roc
  every self-test runs. `methods.shale` and `stack.shale` use it.
- [summed.shale](summed.shale): **a list of findings between its
  summary**, a library: `print_summed(summary, lines)` prints the summary,
  the lines, then the summary again. Every recipe here whose list no tree
  bounds uses it and says `output_bytes=4000000` in its `-- budget:` line,
  so a run the output budget stops still says its totals, and a verify
  reading the last line still finds them.
- [parsed.shale](parsed.shale): **every file that parses is judged**, a
  library: `roc_parsed(paths)` splits `roc_tree`'s files that parse from
  those roc-ast refuses, and `fail_if_refused(who, refused)`, a run's last
  word, names each refused file with its reason and exits non-zero.
  `errors`, `boundaries`, `name_hunt` and `nest_depth` use it, so one
  old-syntax probe in a tree never hides the other files' findings, and is
  never passed unread.
- [types.shale](types.shale): **Roc's own checker's types**, a library:
  `use("cookbook:roc/types.shale")` gives `roc_types(root)`, every module
  the app `root` reaches, by real path, each a list of its expressions'
  and patterns' types with their regions, from roc-types
  (`tools:roc-types/bin/roc-types`, Shale's `tools/roc_types`). A root the
  checker refuses, a missing roc-types and an unknown header each stop the
  run. `methods.shale` uses it.

**Writing a recipe that edits Roc.** roc-ast's columns are bytes, and a
span's end is exclusive. Spans overlap: `type-mod` and `s-type-decl` both
start at the type's name. The file node starts after the leading comments,
and a `|>` is `e-arrow-call`. When the compiler judges an edit, `roc check`
can report two errors on one line, or report an error at the line of the
definition around it, so match its errors to an edit by span, not by
line alone. A recipe that compiles once per file meets the 64-program limit
and must stop and resume, as `methods.shale`'s `from=` does.
