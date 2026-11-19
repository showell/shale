# The Roc cookbook

These Shale scripts check and tidy a tree of Roc code. They are recipes,
not builtins: read one, copy it, and change it for your job. Each script's
header comment is its full documentation; this page says what each one is
for and how to start it.

Run a recipe from the root of the repository it works on. In the lines
below, `<repo>` is that repository and `<shale>` is the shale checkout:

    cd <repo> && shale <shale>/cookbook/roc/hunt_names.shale glob='app/**/*.roc'

Most recipes take `glob=`, the files to read (`**/*.roc` by default), and
`self_test=1`, which runs the recipe over its test files in
`plants/<recipe>/` and fails if any gives the wrong result. Run a
self-test from the shale checkout:

    cd <shale> && shale cookbook:roc/hunt_names.shale self_test=1

A recipe that runs the Roc compiler uses the version named in the
`roc-version` file where the run starts, so start it at the repository's
root.

## On someone else's repository

One command reads another project's Roc code, from that repository's root:

    cd <repo> && shale <shale>/cookbook/roc/check_foreign_code.shale

[check_foreign_code.shale](check_foreign_code.shale) runs every check below once over the whole
tree, as `check_families` and `check_boundaries` judge across folders. It only parses, so
it runs nothing from the tree and needs no compiler at the project's pin. It
prints each recipe's summary and then the findings to read first, and it keeps
each recipe's whole output in `out=` (`scratch:foreign` by default). A project's
own words go in as data: `roles=`, `terms=` and `abbreviations=`, as below.

Our house style is left out, because a careful project may do the opposite. A
check is house style when it says more about our habits than about their code:
[house.shale](house.shale) lists them, and `house=0` on any one recipe leaves
them out of that run. That is the whole of `check_stacking` and `find_deep_calls`, `paren_if`
and `lambda_if` in `find_extra_parens`, `single` in `check_boundaries`, `bare_participle`,
`container`, `phrase_name`, `singular_list`, `where_name` and `world_verb` in `hunt_names`, and `abbreviation` in `find_unclear_names`. Our own trees
keep all of them, and `write=1` refuses `house=0`.

A project's guide outranks ours. Luke Boswell's roc-style.md asks for
`if (x) a else b` where our braces stand, and it asks for stacking where our
rule is only ours to hold. `check_foreign_code.shale` names any guide it sees, so read it,
and run a left-out recipe alone when the guide asks for what it checks.

## Checks

These recipes print findings and, except for the `write=1` of `check_stacking.shale`
and `find_extra_parens.shale`, change no files. All but the last four below exit 0 on findings. A finding is a question for a reader, not a verdict. Each
finding is one line that starts `path:line:` and names its check. The
findings are printed between two copies of a summary of counts
(`check_boundaries.shale` prints its summary once, after the findings). A file
that doesn't parse is listed at the end, and the run then fails.

- [hunt_names.shale](hunt_names.shale) looks for awkward names: an effect
  named like a state, a question that doesn't return a `Bool`, a name made
  only of vague words, a pure function named by a bare past participle
  (`added`, `named`), a function named as a bare phrase (`in_view`), a
  list named as one thing (`triangle : List(Shape)`), a pure function with a
  verb that pretends to an effect (`draw_tree`), a comment that names what a
  rename left behind (`stale_doc_name`), and twelve other checks. Its word lists are in
  `cookbook/words/`, and a project's own terms come in as `terms=`, a file of
  `route: Stop` lines (a `List(Stop)` is a route) and lone terms (`two_opt`).
  `words=1` prints a count of the first words of effects instead of the findings.
- [check_errors.shale](check_errors.shale) checks how code reports failure: a shared
  alias of error tags where the tags belong in place, a tag named like an
  optional (`None`) beside a tag with a payload, a `List.set(...) ?? l` that
  copies the list, a `??` that falls back to a placeholder string, and one that
  falls back to a number after a lookup by index (`number_fallback`).
- [check_boundaries.shale](check_boundaries.shale) asks whether each function earns
  its boundary and each parameter is really a parameter: a function with no
  caller (`dead`), a small function with one, a parameter every caller passes
  the same literal, a function of `{}` that could be a constant, an `if` whose
  branches are the same (`same_branches`), and (with `types=1`) a `Bool`
  parameter that callers pass as a literal. Give it the whole tree, tests
  included, because it counts callers only in the files it reads. What a
  header dictates and what Roc calls by name need no caller. A public name
  with none has its own count, `unused_api`, as a question: its callers may
  all be outside the tree. That is a member of a module a package or platform
  exposes, a member under `lib/`, or a name a `module` header exposes.
- [find_deep_calls.shale](find_deep_calls.shale) finds calls nested three or more
  deep, like `f(g(h(x)))`, which read inside out. `min=` sets the depth to
  report.
- [find_extra_parens.shale](find_extra_parens.shale) finds the parentheses and braceless `if`s a
  code emitter leaves: `if (x) a else b`, parentheses around a whole body
  (`|x| (...)`), `else { if ... }`, and an `if` hanging off a lambda's `|`
  (`lambda_if`), which starts its own line. Roc's formatter keeps all four.
  With `write=1` it rewrites them and runs `roc fmt`. In a sweep it goes before
  `check_stacking.shale`, because what it takes off can bring a group within a line.
- [check_stacking.shale](check_stacking.shale) finds groups laid out against the stacking
  rule, both ways: a group of three or more items and six or more tokens
  that sits on one line (`stack`), and any other group that is stacked and
  would fit on one line (`unstack`). It finds lines wider than `width=` (100
  by default) too. A wide line
  is `wide string` when it would fit if the text of each string literal
  counted as empty, by Roc's parser, and `long` otherwise, so a gate can
  hold `long` at zero and leave the strings Roc can't split. With `write=1`
  it adds trailing commas and runs `roc fmt`, which lays each such group out
  one item a line.

Six more checks can gate a build, because they exit nonzero on any
finding. A project's own words stay with the project and are passed in.

- [check_module_size.shale](check_module_size.shale) holds each module to 500
  lines (`limit=`), the whole file counted, its expects, blank lines and
  comments included, so a module stays small enough to take whole. Test
  roots have no limit and are left out (`tests=`, `*Spec.roc,test/` by
  default). `exempt=` names a project's file of `path -- reason` lines for
  the rare module that properly shows the workhorse pattern; a line with no
  reason or a path that isn't there stops the run, and an exemption a module
  within the limit holds is a finding. `all=1` prints every module's count.

      cd <repo> && shale <shale>/cookbook/roc/check_module_size.shale exempt=tools/module_size.txt

- [check_vocabulary.shale](check_vocabulary.shale) holds a project to its decided
  words. `words=` names the project's file of `kept: retired, retired`
  lines, and each identifier or comment holding a retired word is a finding.
- [check_name_types.shale](check_name_types.shale) finds names that disagree with their
  types: `name_type`, from a project's `table=` of `word: Type` lines, on
  names and fields; `bare_beside_unit`, a field that is a bare number in one
  record and a unit in another; and `no_value`, a constant like `no_frames`
  whose ordinary value is kept to mean none. A `word: any` line says a word
  holds no promise, so a bare field of that name is no finding.
- [check_families.shale](check_families.shale) finds a module spelled unlike the
  others that share its last word (`family`), and one type name declared in
  two modules with different shapes (`twin_name`), so a copy and a namespace
  module are no finding. `roles=` names a project's file of type names each
  module declares for itself, as `Id` in `Message.Id`. Read the whole tree, as
  twins often sit in two folders.
- [find_bare_floats.shale](find_bare_floats.shale) finds a bare `F64` in a signature or a
  type alias's field, and answers each from a table of named exceptions, whose
  unused lines fail the run.
- [find_unclear_names.shale](find_unclear_names.shale) finds a pure function named like a
  mutation (`pure_mutator`), an abbreviation in a name
  (`abbreviation`, from `cookbook/words/abbreviations.txt`), and a module
  with no doc or with history in its top comment (`module_doc`). `terms=`
  names a project's file of fixed terms that open a name and only look like a
  verb, as `set_cookie` does.
- [find_unused_imports.shale](find_unused_imports.shale) finds an import line whose
  name the file never uses (`unused_import`), since Roc doesn't warn of one:
  no value, tag or type starts with the module's name or alias, no name
  the line exposes is used, and no entry of a platform header's `hosted`
  block names it. A use in an annotation, a pattern or an expect
  counts; one in a comment or a string doesn't. Its `glob=` takes several
  globs separated by commas.

`hunt_names`, `check_errors` and `check_boundaries` can also ask Roc's type checker
for types: add `types=1 root=<app>`, where the app imports the files
read. When no one app imports them all, name several apps separated by
commas. A file that no app imports is checked without types, listed at the
end, and fails the run.

    cd <repo> && shale <shale>/cookbook/roc/check_errors.shale glob='app/**/*.roc' types=1 root=app/main.roc,app/Specs.roc

## Rewrites and comparisons

- [wrap_long_lines.shale](wrap_long_lines.shale) shortens lines wider than `width=`
  in the folders `dir=` (several separated by commas) by adding a trailing
  comma to a group on each whose stacking brings the line within the width,
  so that `roc fmt` lays it out one item a line. It edits the files, then
  lists the lines it couldn't shorten, and counts those wide only for their
  strings or holding numeric data; `write=0` rewrites only a copy, a report. With `tokens=6` it also holds the
  folder to the stacking rule, stacking and joining groups whatever their
  width, reading groups as `check_stacking.shale` does. Numeric data, such as a
  list of polygon points, stays packed, as both recipes leave it (`groups.shale`'s
  `is_data_line` and `is_number_data`). It rewrites a copy under
  `scratch:` and writes the folder only once the rewrite has finished, so a
  run that stops leaves the folder as it was.

      cd <repo> && shale <shale>/cookbook/roc/wrap_long_lines.shale dir=roc width=100

- [rename.shale](rename.shale) renames one identifier across a tree by
  whole words, so `portion` leaves `proportion` and `portion_size` alone.
  `module=` holds it to one module's name (qualified everywhere, bare
  inside the module's own file), `fields=1` takes record fields too, `book=` a
  markdown file's `{{code Mod.from}}` includes, `skip=` globs to leave out
  and `dry=1` only counts. Mentions in comments and strings are left and
  counted, and a run that renames nothing fails. `to_module=` moves the
  name instead, after a split: with `module=Geom to_module=RiderView`, each
  `Geom.from` becomes `RiderView.from` (or `RiderView.to` given `to=`, and
  bare inside RiderView's own file); a changed file gains `import RiderView` beside
  its `import Geom` unless it has one, and loses `import Geom` once no
  `Geom.` is left in its code. A changed file whose `import Geom` exposes or
  aliases is refused, and a refusal writes nothing.

      cd <repo> && shale <shale>/cookbook/roc/rename.shale from=portion to=share module=Pie
      cd <repo> && shale <shale>/cookbook/roc/rename.shale from=RiderPoint module=Geom to_module=RiderView

- [rewrite_method_calls.shale](rewrite_method_calls.shale) rewrites calls like `List.map(xs, f)` as
  method calls, `xs.map(f)`. Roc's type checker and compiler decide which
  rewrites are safe; each call it leaves is listed with the reason. It is a
  dry run unless given `apply=1`. Run it with `--undo-on-stop`, so a run
  that stops in the middle puts back the file it was trying. A run starts
  at most its `programs` budget of programs (1,000 unset), so on a very
  large tree it stops and prints the `from=` argument for the next run.
  Its `glob=` takes several globs separated by commas, and a file with no
  call to try needs no root. `trials=0` counts without the compiler: a call
  the types prove counts as rewritable, one they leave open as `unsettled`,
  and nothing is written, so a report runs in seconds; a proven call the
  compiler would refuse (a literal whose type defaults) is counted, which
  only a trial catches. `types_from=` reads the root's types from a file a
  gate kept, in place of running roc-types again. A call of a symmetric
  function ([symmetric_functions.txt](symmetric_functions.txt): `max`,
  `min`, `compare`, `concat`, `map2`, the `bitwise_*` three) whose first two
  arguments' types have one head, aliases read through (`U64.max(a, b)`,
  `Str.concat(a, b)`), stays a module call, its reason `peers`; with the
  types, each such method call (`a.max(b)`) is a `peer_receiver` finding,
  counted in the summary, and `peers=undo` turns them back into module
  calls, judged by the compiler as a rewrite is. Every other function, a
  unit's `plus` or `starts_with`, keeps its method call. What a rewrite
  leaves for the sweep is reported, not fixed: an import it made unused
  (`made_unused_import`) and a line it pushed past 100 columns
  (`made_long_line`), each counted in the summary.

      cd <repo> && shale --undo-on-stop <shale>/cookbook/roc/rewrite_method_calls.shale glob='app/*.roc' root=app/main.roc,app/Specs.roc
      cd <repo> && shale <shale>/cookbook/roc/rewrite_method_calls.shale glob='app/*.roc' root=app/main.roc trials=0
      cd <repo> && shale --undo-on-stop <shale>/cookbook/roc/rewrite_method_calls.shale glob='app/*.roc' root=app/main.roc peers=undo apply=1

- [compare_machine_code.shale](compare_machine_code.shale) compares two builds of a Roc archive
  or program and says whether they hold the same machine code. Use it to
  confirm that a change is only a reformat or a move. It compares each
  procedure's body as a multiset, Roc's hashed names and the addresses
  masked, so a move across modules, which reorders the whole layout, is
  still SAME; a body whose code matches and whose data alone differs (a
  crash message naming a renamed module) is reported apart, as DATA ONLY.
  `by=line` is the stricter line-by-line comparison, for a reformat inside
  one module.

      cd <repo> && shale <shale>/cookbook/roc/compare_machine_code.shale old=<a.a> new=<b.a>

- [write_tree_patch.shale](../write_tree_patch.shale), in the cookbook's top folder, turns a changed copy of a folder
  into one patch that `patch -p1` applies from the repository's root.

      cd <repo> && shale <shale>/cookbook/write_tree_patch.shale old=roc new=<copy>/roc under=roc out=<file>.patch

## Reading types

- [print_module_types.shale](print_module_types.shale) prints a module's types and every type
  they depend on, the most primitive first and the module's own last, each
  with its doc, its definition as written and the annotations of its
  functions and constants, with no bodies or values. A constant's annotation
  ends in `# constant`, and the types it mentions are followed like a
  function's. An annotation of type `_` and the helpers outside any type's
  block are left out. It is for reviewing the types alone. A package outside the
  root needs `packages=`, as below; a name it can't resolve fails the run.
  Each of the module's members ends with how many other files use it: by
  name, or by a method call or operator on a receiver the file plainly types
  as its type. A call on a receiver it can't type is counted apart, by name,
  and the counts line says how many calls were typed and how many pooled.
  A file roc-ast refuses has no uses the walk can read, so it is named on
  the second line and in the counts line, a member with none says `no
  outside use, as far as the parsed files show`, and the run fails.
  A package that apps use is walked from outside it, with `clients=` naming
  the apps, so their uses and signatures count too, and a member used across
  several apps says how many (`used by 9 files in 5 apps`). An app beside
  the package that uses it but holds no client file fails the walk, naming
  it, since a member only it uses would read as unused: `clients=*_widget`
  leaves out Safari and the other apps, and `clients=*` names them all.
  `modules=A,B,C`
  walks sibling modules as one family: the types two or more reach once,
  each member's types and signatures with its private functions', and a
  table of their public names by shape, with the error tags each answers.

      cd <repo> && shale <shale>/cookbook/roc/print_module_types.shale module=Pigs packages=lib=repos:roc-apps/canvas_apps/lib
      shale cookbook:roc/print_module_types.shale root=repos:roc-apps/canvas_apps/lib module=Brush clients=repos:roc-apps/canvas_apps/*
      cd canvas_apps && shale <shale>/cookbook/roc/print_module_types.shale root=safari modules=TreeSighting,RailSighting,TowerSighting packages=lib=repos:roc-apps/canvas_apps/lib

- [rank_boundary_hints.shale](rank_boundary_hints.shale) ranks cheap hints that one
  module does another's work, for a short review rather than an audit: a
  public member whose production uses outside its module all sit in one
  other module (`fed_to_one`, strongest when that module builds shapes in
  the function that uses it), and a public name that starts with another
  module's name and an underscore (`borrowed_noun`, `sky_top_stop` in
  Backdrop). Uses are counted as print_module_types counts them; tests are no user.
  Hints don't gate: the run fails only on a file that doesn't parse.

      cd canvas_apps && shale <shale>/cookbook/roc/rank_boundary_hints.shale root=safari clients=*_widget,widget_kit

## Libraries

These files are loaded with `use("cookbook:roc/<file>.shale")` and are not
run on their own.

- [member_uses.shale](member_uses.shale): `print_module_types`'s file loading, package
  folders and client files, its name resolution and its
  counts of each member's outside uses, with the rules for typing a method
  call's receiver and the operators Roc sends to members (`<` to `is_lt`),
  and a family member's private functions and public names.
- [module_family.shale](module_family.shale): `print_module_types`'s printing order, the
  family table and its shape rule, and the self-test's checks.
- [parsed.shale](parsed.shale): the recipes check every .roc file in a
  tree, and some files may not parse. This library lets a recipe check the
  files that do parse, print what it found, and then fail with a list of
  the files it couldn't parse and why: `parse_roc_files(paths)` and
  `fail_if_refused(who, refused, unreached?)`. Used by `check_boundaries`,
  `check_errors`, `hunt_names`, `find_deep_calls`, `find_extra_parens` and `check_stacking`.
- [headers.shale](headers.shale): who chooses a file's names:
  `header_names(nodes)`, what an app's or platform's header dictates and what
  a `module` header exposes; `exposed_by_folder(dir)`, `is_exposed_module(path)`
  and `is_private_module(path)`, what a package or platform exposes; and
  `LANGUAGE_FIXED_NAMES`, the methods Roc calls by name. Used by `check_boundaries`,
  `check_families`, `hunt_names`, `find_unclear_names` and `check_stacking`.
- [house.shale](house.shale): `HOUSE_CHECKS`, the checks that are only our
  house style, and `house_leaves_out(recipe, check)`, which `house=0` turns on.
  Used by `check_boundaries`, `check_foreign_code`, `hunt_names`, `find_deep_calls`, `find_extra_parens`,
  `find_unclear_names` and `check_stacking`.
- [findings.shale](../findings.shale), in the cookbook's top folder:
  `print_findings(summary, lines)` prints the summary, the findings and the summary again. The first copy survives
  when Shale's output limit cuts a long list short, and a caller that reads
  only the last line still gets the counts. Used by most Roc recipes, the Go recipes
  ([cookbook/go/](../go/README.md)) and the prose lints.
- [pinned.shale](pinned.shale): `roc_bin(who, self_test)` gives the path
  of the Roc compiler that the repository's `roc-version` file names
  (installed at `tools:roc-<version>/roc`), and `SELF_TEST_PIN` is the
  version every self-test uses. Used by `check_boundaries`, `check_errors`, `rewrite_method_calls`,
  `hunt_names`, `wrap_long_lines` and `check_stacking`.
- [groups.shale](groups.shale): the stacking rule, the bracket groups and
  their items and tokens, `groups_in_lines(ls)`, what a rewrite does to
  them, `group_to_stack(l, tokens, width)` (with `group_to_wrap`, the group
  whose stacking brings a wide line within the width),
  `string_columns_on_lines(nodes, ls, who)`, and
  `groups_to_unstack(ls, tokens, width)`, and what it can't reach,
  `without_targets_section(ls)`. Used by `wrap_long_lines` and `check_stacking`, so the two
  read a group the same way.
- [types.shale](types.shale): runs
  [roc-types](https://github.com/showell/roc-types), the tool that asks
  Roc's type checker for the type of every expression and pattern in an app.
  `roc_types(root, roc)` reads one app; `roc_types_for_files(who, root,
  roc, paths, exempt)` gathers the types for a recipe's files from one or
  more apps and lists the files none of them reaches. Used by
  `check_boundaries`, `check_errors`, `rewrite_method_calls` and `hunt_names`.
- [peer_calls.shale](peer_calls.shale): which calls are peer calls, a
  symmetric function's with its first two arguments of one type:
  `peer_symmetric_table(text)`, the list of symmetric functions,
  `peer_aliases(trees)`, the aliases the trees declare, `peer_verdict(a, b,
  aliases)`, two types judged with aliases read through, and
  `peer_method_calls(t, symmetric)`. Used by `rewrite_method_calls`.
- [number_literals.shale](number_literals.shale): whether a value's type
  comes from unsuffixed number literals alone (`takes_literal_type`), so it
  defaults as a receiver. Used by `rewrite_method_calls`.

- [imports.shale](imports.shale): `unused_imports(nodes, src)`, a file's
  imports that no name uses, by its tree. Used by `find_unused_imports` and
  `rewrite_method_calls`.
- [method_text.shale](method_text.shale): `rewrite_method_calls`' trees with
  byte spans (`parse_trees`) and the text with chosen calls turned
  (`rewritten_text`).
- [declared.shale](declared.shale): what the naming recipes read out of a
  parse tree: `declarations_of(file)`, each type, value and record field a
  file declares, with `render_type`, `name_words` and `word_set`, and what
  `hunt_names` reads of a type in the tree or written as text, such as
  `union_shape` and `first_argument`. These sit here, not in a library of
  their own, because a library's functions can't call another library's and
  they need this one's. `hunt_names` judges a list's name here too
  (`list_name_finding`), with a project's terms (`project_terms`), reads
  each file's definitions (`file_definitions`), and asks whether a name
  starting with a preposition makes its own type (`reads_as_maker`). Used by
  `check_families`, `hunt_names`, `check_name_types`, `find_unclear_names` and
  `check_vocabulary`.
- [doc_names.shale](doc_names.shale): `hunt_names`' `stale_doc_name`, the
  backticked names in a file's comments and whether each still names
  something in the file or a module beside it.
- [naming_passes.shale](naming_passes.shale): `hunt_names`' checks judged
  over the whole tree once every file is read, `preposition_split`,
  `container`, a field's `short_name` and `where_name`, and the census
  `words=1` prints. They need no other library, so they sit apart.
- [naming_plants.shale](naming_plants.shale): `hunt_names`' self-test,
  which check each plant is for and what a run over them gets wrong.
- [gate_recipe.shale](gate_recipe.shale): what the recipes that exit
  nonzero on findings share: `verdict_of`, their counts and lines, and
  `check_plants`, a self-test that runs the recipe as a child program once
  for each row of a table, so a row judges the exit code as well as the
  findings. Used by `check_families`, `check_name_types`, `find_unclear_names`,
  `find_unused_imports` and `check_vocabulary`.

## Tools the recipes need

- [roc-ast](https://github.com/showell/roc-ast), Roc's own parser, gives
  the recipes their syntax trees through Shale's `roc_tree`. Its source is
  a repo of its own; install it by running `shale install.shale` in a
  checkout of that repo. It lands at `~/tools/roc-ast/bin/roc-ast`.
- [roc-types](https://github.com/showell/roc-types) gives the types. Its
  source is a repo of its own; install it by running `shale install.shale`
  in a checkout of that repo. It lands at `~/tools/roc-types/bin/roc-types`.
- The Roc compiler named in `roc-version`, at `tools:roc-<version>/roc`.

## Writing a recipe that edits Roc

roc-ast's columns count bytes, and a span's end is exclusive. Spans can
overlap: `type-mod` and `s-type-decl` both start at the type's name. The
file node starts after the leading comments, and a `|>` is an
`e-arrow-call`. `roc check` can report two errors on one line, or report
an error at the line of the definition around it, so match its errors to
an edit by span, not by line. A Shale run starts at most its `programs` budget
of programs, so a recipe that compiles once per file stops and resumes past
it, as `rewrite_method_calls.shale`'s `from=` does.
