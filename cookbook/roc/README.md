# The Roc cookbook

These Shale scripts check and tidy a tree of Roc code. They are recipes to
read, copy and change for your job. Each script's header comment is its
full documentation, its arguments and checks included; this page says what
each one is for and how to start it.

Run a recipe from the root of the repository it works on. In the lines
below, `<repo>` is that repository and `<shale>` is the shale checkout:

    cd <repo> && shale <shale>/cookbook/roc/hunt_names.shale glob='app/**/*.roc'

Most recipes take `glob=`, the files to read (`**/*.roc` by default), and
`self_test=1`, which runs the recipe over its test files in
`plants/<recipe>/` and fails on any wrong result. Run a self-test from the
shale checkout:

    cd <shale> && shale cookbook:roc/hunt_names.shale self_test=1

A recipe that runs the Roc compiler uses the version named in the
`roc-version` file where the run starts, so start it at the repository's
root.

## On someone else's repository

One command reads another project's Roc code, from that repository's root:

    cd <repo> && shale <shale>/cookbook/roc/check_foreign_code.shale

[check_foreign_code.shale](check_foreign_code.shale) runs the checks below
once over the whole tree. It only parses, so it runs nothing from the tree
and works without a compiler at the project's pin. It prints each recipe's
summary, then the findings to read first, and keeps each recipe's whole
output in `out=`. A project's own words go in as data: `roles=`, `terms=`
and `abbreviations=`.

It leaves out our house style, since a careful project may choose the
opposite. [house.shale](house.shale) lists those checks, and `house=0` on
any one recipe leaves them out of that run. Our own trees keep all of them.

A project's guide outranks ours. Luke Boswell's roc-style.md asks for
`if (x) a else b` where our braces stand, and it asks for stacking where
our rule is ours alone. `check_foreign_code.shale` names any guide it sees,
so read it, and run a left-out recipe alone when the guide asks for what it
checks.

## Checks

These recipes print findings, one line each that starts `path:line:` and
names its check, between two copies of a summary of counts. Only the
`write=1` of `check_stacking.shale` and `find_extra_parens.shale` changes
files. A file that roc-ast refuses is listed at the end, and the run then
fails.

**For a reader** (a run with findings exits 0; each finding is a
question):

- [hunt_names.shale](hunt_names.shale): awkward names. Its word lists are
  in `cookbook/words/`; a project's own terms come in as `terms=`. It is
  written in Roc, a root,
  [hunt_names/hunt_names.roc](hunt_names/hunt_names.roc), with its reading
  and its checks in the modules beside it;
  [check_goldens.shale](hunt_names/check_goldens.shale) holds it to its
  goldens.
- [check_errors.shale](check_errors.shale): how code reports failure.
- [check_boundaries.shale](check_boundaries.shale): whether each function
  earns its boundary and each parameter is really one. Give it the whole
  tree, tests included, since it counts callers only in the files it reads.
  It is written in Roc, a root,
  [check_boundaries/check_boundaries.roc](check_boundaries/check_boundaries.roc),
  with its passes and checks in the modules beside it;
  [check_goldens.shale](check_boundaries/check_goldens.shale) holds it to
  its goldens.
- [find_deep_calls.shale](find_deep_calls.shale): calls nested three or
  more deep, like `f(g(h(x)))`, which read inside out. It is written in
  Roc, a root,
  [find_deep_calls/find_deep_calls.roc](find_deep_calls/find_deep_calls.roc),
  with its rule in `Nest.roc` beside it;
  [check_goldens.shale](find_deep_calls/check_goldens.shale) holds it to
  its goldens.
- [find_extra_parens.shale](find_extra_parens.shale): the parentheses and
  braceless `if`s a code emitter leaves, which Roc's formatter keeps. With
  `write=1` it rewrites them and runs `roc fmt`. In a sweep it goes before
  `check_stacking.shale`, since what it removes can bring a group within a
  line.
- [check_stacking.shale](check_stacking.shale): groups laid out against
  the stacking rule, both ways, and lines wider than `width=`. With
  `write=1` it adds trailing commas and runs `roc fmt`. It is written in
  Roc, a root,
  [check_stacking/check_stacking.roc](check_stacking/check_stacking.roc),
  with the rule in the Roc library's `Groups.roc` and `Stacking.roc`
  (cookbook/roc/lib/);
  [check_goldens.shale](check_stacking/check_goldens.shale) holds it to
  its goldens.

**For a gate** (a run with findings exits nonzero; a project's own words
are passed in):

- [check_module_size.shale](check_module_size.shale): each module held to
  a size, the whole file counted, so a module stays small enough to take
  whole.

      cd <repo> && shale <shale>/cookbook/roc/check_module_size.shale exempt=tools/module_size.txt

- [check_vocabulary.shale](check_vocabulary.shale): a project held to its
  decided words. It is written in Roc, a root,
  [check_vocabulary/check_vocabulary.roc](check_vocabulary/check_vocabulary.roc),
  with its checks in `Retired.roc` beside it;
  [check_goldens.shale](check_vocabulary/check_goldens.shale) holds it to
  its goldens.
- [check_name_types.shale](check_name_types.shale): names that disagree
  with their types. It is written in Roc, a root,
  [check_name_types/check_name_types.roc](check_name_types/check_name_types.roc),
  with its checks in `NameType.roc`, `BareBesideUnit.roc` and `NoValue.roc`
  beside it; [check_goldens.shale](check_name_types/check_goldens.shale)
  holds it to its goldens.
- [check_families.shale](check_families.shale): a module spelled unlike
  the others that share its last word, and one type name declared in two
  modules with different shapes. Give it the whole tree, since twins often
  sit in two folders. It is written in Roc, a root,
  [check_families/check_families.roc](check_families/check_families.roc),
  with its checks in `Family.roc` and `Twin.roc` beside it;
  [check_goldens.shale](check_families/check_goldens.shale) holds it to its
  goldens.
- [find_bare_floats.shale](find_bare_floats.shale): a bare `F64` in a
  signature or a type alias's field, against a table of named exceptions.
  It is written in Roc, a root,
  [find_bare_floats/find_bare_floats.roc](find_bare_floats/find_bare_floats.roc),
  with its rule in `Floats.roc` beside it;
  [check_goldens.shale](find_bare_floats/check_goldens.shale) holds it to
  its goldens.
- [find_unclear_names.shale](find_unclear_names.shale): names and module
  docs that say too little. It is written in Roc, a folder of modules whose
  root is
  [find_unclear_names/find_unclear_names.roc](find_unclear_names/find_unclear_names.roc),
  on Shale's Roc platform; the `.shale` file is its launcher, and
  [check_goldens.shale](find_unclear_names/check_goldens.shale) holds it to
  its goldens.
- [find_unused_imports.shale](find_unused_imports.shale): an import line
  the file leaves unused, which Roc's compiler accepts quietly. It is
  written in Roc too, a root,
  [find_unused_imports/find_unused_imports.roc](find_unused_imports/find_unused_imports.roc),
  with its rule in the Roc library's `Imports.roc` (cookbook/roc/lib/);
  [check_goldens.shale](find_unused_imports/check_goldens.shale) holds it to
  its goldens.

The Roc recipes share one package, [lib/](lib/main.roc): `Tree` (Roc's
tree of a file read), `Declarations` (what a file declares), `Text` (text
cut and ordered as Shale's builtins do it) and `Finding` (a check's finding,
ordered and printed). A recipe's app names it in its header, `lib:
"../lib/main.roc"`, and imports `lib.Tree`.

`hunt_names`, `check_errors` and `check_boundaries` can also ask Roc's type
checker for types: add `types=1 root=<app>`, where the app imports the
files read, or several apps separated by commas. A file that no app
imports is checked without types, listed at the end, and fails the run.

    cd <repo> && shale <shale>/cookbook/roc/check_errors.shale glob='app/**/*.roc' types=1 root=app/main.roc,app/Specs.roc

## Rewrites and comparisons

- [wrap_long_lines.shale](wrap_long_lines.shale): shortens lines wider than
  `width=` by adding a trailing comma to a group, so `roc fmt` lays it out
  one item a line, and lists the lines it leaves. It rewrites a copy and
  writes the folder once the rewrite has finished, so a run that stops
  leaves the folder as it was. It is the launcher of a Roc script
  ([wrap_long_lines/](wrap_long_lines/)), whose stacking rule is the Roc
  library's `Stacking.roc`, the rule's one home, which `check_stacking`
  reads too.

      cd <repo> && shale <shale>/cookbook/roc/wrap_long_lines.shale dir=roc width=100

- [rename.shale](rename.shale): renames one identifier across a tree by
  whole words, so `portion` leaves `proportion` and `portion_size` alone.
  `to_module=` moves a qualified name to another module after a split,
  fixing the imports.

      cd <repo> && shale <shale>/cookbook/roc/rename.shale from=portion to=share module=Pie
      cd <repo> && shale <shale>/cookbook/roc/rename.shale from=RiderPoint module=Geom to_module=RiderView

- [rewrite_method_calls.shale](rewrite_method_calls.shale): rewrites calls
  like `List.map(xs, f)` as method calls, `xs.map(f)`. Roc's type checker
  and compiler decide which rewrites are safe; each call it leaves is
  listed with the reason. It reports until given `apply=1`. Run it with
  `--undo-on-stop`, so a run that stops in the middle puts back the file it
  was trying. A symmetric function's call (`U64.max(a, b)`, from
  [symmetric_functions.txt](symmetric_functions.txt)) stays a module call.

      cd <repo> && shale --undo-on-stop <shale>/cookbook/roc/rewrite_method_calls.shale glob='app/*.roc' root=app/main.roc,app/Specs.roc
      cd <repo> && shale <shale>/cookbook/roc/rewrite_method_calls.shale glob='app/*.roc' root=app/main.roc trials=0

- [compare_machine_code.shale](compare_machine_code.shale): says whether
  two builds of a Roc archive or program hold the same machine code, to
  confirm that a change is a reformat or a move. It compares each
  procedure's body with Roc's hashed names and the addresses masked, so a
  move across modules is still SAME.

      cd <repo> && shale <shale>/cookbook/roc/compare_machine_code.shale old=<a.a> new=<b.a>

- [write_tree_patch.shale](../write_tree_patch.shale), in the cookbook's
  top folder, turns a changed copy of a folder into one patch that
  `patch -p1` applies from the repository's root.

      cd <repo> && shale <shale>/cookbook/write_tree_patch.shale old=roc new=<copy>/roc under=roc out=<file>.patch

## Reading types

- [print_module_types.shale](print_module_types.shale): a module's types
  and every type they depend on, the most primitive first, each with its
  doc, its definition and its functions' annotations, for reviewing the
  types alone. Each member ends with how many other files use it.
  `clients=` names the apps that use a package, and `modules=A,B,C` walks
  sibling modules as one family.

      cd <repo> && shale <shale>/cookbook/roc/print_module_types.shale module=Pigs packages=lib=<path to lib>

- [rank_boundary_hints.shale](rank_boundary_hints.shale): ranks cheap
  hints that one module does another's work, for a short review. It fails
  only on a file roc-ast refuses.

      cd canvas_apps && shale <shale>/cookbook/roc/rank_boundary_hints.shale root=safari clients=*_widget,widget_kit

## Libraries

Recipes load these with `use("cookbook:roc/<file>.shale")` (or `./<file>`
beside them). Each header lists its functions.

- [parsed.shale](parsed.shale): parse a tree's files, check the ones that
  parse, and fail at the end with the ones roc-ast refused.
- [headers.shale](headers.shale): the names a file's header dictates, what
  a package or platform exposes, and the methods Roc calls by name.
- [house.shale](house.shale): the checks that are our house style alone.
- [findings.shale](../findings.shale), in the cookbook's top folder: the
  summary, the findings and the summary again, so the counts survive when
  the output limit cuts a long list.
- [pinned.shale](pinned.shale): the path of the Roc compiler a repo's
  `roc-version` names.
- [types.shale](types.shale): runs
  [roc-types](https://github.com/showell/roc-types) for one app or several.
- [declared.shale](declared.shale): what the naming recipes read out of a
  parse tree.
- [member_uses.shale](member_uses.shale) and
  [module_family.shale](module_family.shale): `print_module_types`'s name
  resolution, use counts and family printing.
- [peer_calls.shale](peer_calls.shale),
  [number_literals.shale](number_literals.shale) and
  [method_text.shale](method_text.shale): parts of `rewrite_method_calls`.
- [imports.shale](imports.shale): a file's imports that it leaves unused,
  for `rewrite_method_calls`; the Roc library's `Imports.roc` holds the same
  rule for `find_unused_imports`.
- [gate_recipe.shale](gate_recipe.shale): what the gating recipes share,
  their verdict and their plants' self-test.

## Tools the recipes need

- [roc-ast](https://github.com/showell/roc-ast), Roc's own parser, gives
  the recipes their syntax trees through Shale's `roc_tree`. Install it by
  running `shale install.shale` in a checkout of that repo; it lands under
  the `tools` mount as `roc-ast/bin/roc-ast`.
- [roc-types](https://github.com/showell/roc-types) gives the types,
  installed the same way, as `roc-types/bin/roc-types`.
- The Roc compiler named in `roc-version`, at `tools:roc-<version>/roc`.

## Writing a recipe that edits Roc

roc-ast's columns count bytes, and a span's end is exclusive. Spans can
overlap: `type-mod` and `s-type-decl` both start at the type's name. The
file node starts after the leading comments, and a `|>` is an
`e-arrow-call`. `roc check` can report two errors on one line, or report an
error at the line of the definition around it, so match its errors to an
edit by span. A Shale run starts at most its `programs` budget of
programs, so a recipe that compiles once per file stops and resumes past
it, as `rewrite_method_calls.shale`'s `from=` does.
