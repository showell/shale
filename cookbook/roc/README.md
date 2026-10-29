# The Roc cookbook

Recipes for keeping a Roc tree tidy and judging a change to it, written
in Shale over `run` (`roc`, `objdump`). Like the Zig cookbook's, they are
recipes, not builtins: read one, copy it, change it for the job. Each is
tested to parse (`src/shale_cookbook_test.zig`). Run one from the checkout
it works on, `<shale>` being the shale checkout:

    cd <repo> && shale <shale>/cookbook/roc/roc_commas.shale dir=roc width=100

- [name_hunt.shale](name_hunt.shale): **awkward names, a question each**,
  the Zig hunt's five checks over Roc's annotations and definitions, the
  top level's and module types' members (`held_raw!` named as a state).
  It reads Roc's own parser's tree (roc-ast, `tools:roc-ast/bin/roc-ast`,
  built by roc-webbox's `tools/roc_ast/install.shale`), so a broken
  annotation or a `where` clause reads as Roc reads it. A name is judged
  once, from its annotation when it has one (a hosted effect has nothing
  else); a `!` that names a callback's effect (`map2!`) and a name the
  language fixes (`is_eq`, `subscript`) are not judged as the author's.
  Beside the names: `command_tense` and `mixed_tense` (the tags an effect
  is handed, a `List(T)` judged as `T`, named as events, or as commands
  and events at once) and `container` (a type or record field named only
  by container words, cookbook/words/containers.txt, unless its doc says
  whose: `## A folder's entry`).
  `self_test=1` runs its plants (`plants/name_hunt/`).
- [roc_commas.shale](roc_commas.shale): **long lines laid out by `roc fmt`.**
  `roc fmt` never wraps a line, and it joins hand-made breaks back. A
  trailing comma after a record's, call's or lambda's last item makes it
  lay that group out one item a line. This puts one on each long line's
  widest group, runs `roc fmt`, and repeats until nothing changes. It lists
  what it can't reach: strings, long conditions, a function type's bare
  arguments.
- [stack.shale](stack.shale): **three or more, one a line.** Any list,
  record, tuple, call's or lambda's arguments or tag payload of three or
  more items on one line is a finding, and so is a line over 100 columns,
  and an annotation's three or more bare arguments (`U8, U8, U8 -> U8`),
  which no trailing comma stacks.
  `write=1` gives each a trailing comma, outermost first, and runs `roc fmt`
  until nothing changes; a second run changes nothing. Strings and comments
  are never read as items, nor is a record pattern's `..`; a `where`
  clause's items are its method constraints. `self_test=1` runs its
  plants (`plants/stack/`).
- [nest_depth.shale](nest_depth.shale): **a call nested three deep**,
  `f(g(h(x)))`, read inside out: the outermost call of each nest `min`
  (3) or more deep, from roc-ast's tree. A method's receiver, a pipe's or
  an arrow's left side, a tag, `?` and parentheses are no level; a lambda,
  record or list argument starts again (`loose=1` passes depth through the
  last two). `self_test=1` runs its plants (`plants/nest_depth/`).
- [boundaries.shale](boundaries.shale): **a function's boundary, from its
  references.** `single`: a wrapper (one small expression, by its tree,
  not its lines; a decision of a line or two) with one production
  caller, which no `expect`, test file or `##` doc keeps (a rule of thumb,
  not a gate); `fixed`: a parameter every production call fills with the
  same literal (`at: 0`), never context passed down (`me`, `req.me`).
  `self_test=1` runs its plants (`plants/boundaries/`).
- [errors.shale](errors.shale): **a failure said by its tags.**
  `error_alias`: a function whose result's `Try` names a shared error
  alias, not its tags in place (an alias in an argument, a handler's
  input, is fine); `optional_tag`: a tag named as an optional (`None`,
  `Some`: cookbook/words/optionals.txt). `self_test=1` runs its plants
  (`plants/errors/`).
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
