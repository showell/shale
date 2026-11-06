# The Roc cookbook

These Shale scripts check and tidy a tree of Roc code. They are recipes,
not builtins: read one, copy it, and change it for your job. Each script's
header comment is its full documentation; this page says what each one is
for and how to start it.

Run a recipe from the root of the repository it works on. In the lines
below, `<repo>` is that repository and `<shale>` is the shale checkout:

    cd <repo> && shale <shale>/cookbook/roc/name_hunt.shale glob='app/**/*.roc'

Most recipes take `glob=`, the files to read (`**/*.roc` by default), and
`self_test=1`, which runs the recipe over its test files in
`plants/<recipe>/` and fails if any gives the wrong result. Run a
self-test from the shale checkout:

    cd <shale> && shale cookbook:roc/name_hunt.shale self_test=1

A recipe that runs the Roc compiler uses the version named in the
`roc-version` file where the run starts, so start it at the repository's
root.

## On someone else's repository

One command reads another project's Roc code, from that repository's root:

    cd <repo> && shale <shale>/cookbook/roc/foreign.shale

[foreign.shale](foreign.shale) runs every check below once over the whole
tree, as `families` and `boundaries` judge across folders. It only parses, so
it runs nothing from the tree and needs no compiler at the project's pin. It
prints each recipe's summary and then the findings to read first, and it keeps
each recipe's whole output in `out=` (`scratch:foreign` by default). A project's
own words go in as data: `roles=`, `terms=` and `abbreviations=`, as below.

Our house style is left out, because a careful project may do the opposite. A
check is house style when it says more about our habits than about their code:
[house.shale](house.shale) lists them, and `house=0` on any one recipe leaves
them out of that run. That is the whole of `stack` and `nest_depth`, `paren_if`
and `lambda_if` in `parens`, `single` in `boundaries`, `bare_participle` and
`container` in `name_hunt`, and `abbreviation` in `plain_names`. Our own trees
keep all of them, and `write=1` refuses `house=0`.

A project's guide outranks ours. Luke Boswell's roc-style.md asks for
`if (x) a else b` where our braces stand, and it asks for stacking where our
rule is only ours to hold. `foreign.shale` names any guide it sees, so read it,
and run a left-out recipe alone when the guide asks for what it checks.

## Checks

These recipes print findings and, except for the `write=1` of `stack.shale`
and `parens.shale`, change no files. All but the last four below exit 0 on findings. A finding is a question for a reader, not a verdict. Each
finding is one line that starts `path:line:` and names its check. The
findings are printed between two copies of a summary of counts
(`boundaries.shale` prints its summary once, after the findings). A file
that doesn't parse is listed at the end, and the run then fails.

- [name_hunt.shale](name_hunt.shale) looks for awkward names: an effect
  named like a state, a question that doesn't return a `Bool`, a name made
  only of vague words, a pure function named by a bare past participle
  (`added`, `named`), and eleven other checks. Its word lists are in
  `cookbook/words/`. `words=1` prints a count of the first words of
  effects instead of the findings.
- [errors.shale](errors.shale) checks how code reports failure: a shared
  alias of error tags where the tags belong in place, a tag named like an
  optional (`None`) beside a tag with a payload, a `List.set(...) ?? l` that
  copies the list, a `??` that falls back to a placeholder string, and one that
  falls back to a number after a lookup by index (`number_fallback`).
- [boundaries.shale](boundaries.shale) asks whether each function earns
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
- [nest_depth.shale](nest_depth.shale) finds calls nested three or more
  deep, like `f(g(h(x)))`, which read inside out. `min=` sets the depth to
  report.
- [parens.shale](parens.shale) finds the parentheses and braceless `if`s a
  code emitter leaves: `if (x) a else b`, parentheses around a whole body
  (`|x| (...)`), `else { if ... }`, and an `if` hanging off a lambda's `|`
  (`lambda_if`), which starts its own line. Roc's formatter keeps all four.
  With `write=1` it rewrites them and runs `roc fmt`. In a sweep it goes before
  `stack.shale`, because what it takes off can bring a group within a line.
- [stack.shale](stack.shale) finds groups laid out against the stacking
  rule, both ways: a group of three or more items and six or more tokens
  that sits on one line (`stack`), and any other group that is stacked and
  would fit on one line (`unstack`). It finds lines wider than `width=` (100
  by default) too. A wide line
  is `wide string` when it would fit if the text of each string literal
  counted as empty, by Roc's parser, and `long` otherwise, so a gate can
  hold `long` at zero and leave the strings Roc can't split. With `write=1`
  it adds trailing commas and runs `roc fmt`, which lays each such group out
  one item a line.

Four more checks can gate a build, because they exit nonzero on any
finding. A project's own words stay with the project and are passed in.

- [vocabulary.shale](vocabulary.shale) holds a project to its decided
  words. `words=` names the project's file of `kept: retired, retired`
  lines, and each identifier or comment holding a retired word is a finding.
- [name_types.shale](name_types.shale) finds names that disagree with their
  types: `name_type`, from a project's `table=` of `word: Type` lines, on
  names and fields; `bare_beside_unit`, a field that is a bare number in one
  record and a unit in another; and `no_value`, a constant like `no_frames`
  whose ordinary value is kept to mean none. A `word: any` line says a word
  holds no promise, so a bare field of that name is no finding.
- [families.shale](families.shale) finds a module spelled unlike the
  others that share its last word (`family`), and one type name declared in
  two modules with different shapes (`twin_name`), so a copy and a namespace
  module are no finding. `roles=` names a project's file of type names each
  module declares for itself, as `Id` in `Message.Id`. Read the whole tree, as
  twins often sit in two folders.
- [bare_floats.shale](bare_floats.shale) finds a bare `F64` in a signature or a
  type alias's field, and answers each from a table of named exceptions, whose
  unused lines fail the run.
- [plain_names.shale](plain_names.shale) finds a pure function named like a
  mutation (`pure_mutator`), an abbreviation in a name
  (`abbreviation`, from `cookbook/words/abbreviations.txt`), and a module
  with no doc or with history in its top comment (`module_doc`). `terms=`
  names a project's file of fixed terms that open a name and only look like a
  verb, as `set_cookie` does.

`name_hunt`, `errors` and `boundaries` can also ask Roc's type checker
for types: add `types=1 root=<app>`, where the app imports the files
read. When no one app imports them all, name several apps separated by
commas. A file that no app imports is checked without types, listed at the
end, and fails the run.

    cd <repo> && shale <shale>/cookbook/roc/errors.shale glob='app/**/*.roc' types=1 root=app/main.roc,app/Specs.roc

## Rewrites and comparisons

- [roc_commas.shale](roc_commas.shale) shortens lines wider than `width=`
  in the folder `dir=` by adding a trailing comma to a group on each, so
  that `roc fmt` lays it out one item a line. It edits the files, then
  lists the lines it couldn't shorten. With `tokens=6` it also holds the
  folder to the stacking rule, stacking and joining groups whatever their
  width, reading groups as `stack.shale` does.

      cd <repo> && shale <shale>/cookbook/roc/roc_commas.shale dir=roc width=100

- [methods.shale](methods.shale) rewrites calls like `List.map(xs, f)` as
  method calls, `xs.map(f)`. Roc's type checker and compiler decide which
  rewrites are safe; each call it leaves is listed with the reason. It is a
  dry run unless given `apply=1`. Run it with `--undo-on-stop`, so a run
  that stops in the middle puts back the file it was trying. A run starts
  at most 64 programs, so on a large tree it stops and prints the `from=`
  argument for the next run.

      cd <repo> && shale --undo-on-stop <shale>/cookbook/roc/methods.shale glob='app/*.roc' root=app/main.roc,app/Specs.roc

- [same_code.shale](same_code.shale) compares two builds of a Roc archive
  and says whether they hold the same machine code. Use it to confirm that
  a change is only a reformat.

      cd <repo> && shale <shale>/cookbook/roc/same_code.shale old=<a.a> new=<b.a>

- [tree_patch.shale](tree_patch.shale) turns a changed copy of a folder
  into one patch that `patch -p1` applies from the repository's root.

      cd <repo> && shale <shale>/cookbook/roc/tree_patch.shale old=roc new=<copy>/roc under=roc out=<file>.patch

## Libraries

These files are loaded with `use("cookbook:roc/<file>.shale")` and are not
run on their own.

- [parsed.shale](parsed.shale): the recipes check every .roc file in a
  tree, and some files may not parse. This library lets a recipe check the
  files that do parse, print what it found, and then fail with a list of
  the files it couldn't parse and why: `parse_roc_files(paths)` and
  `fail_if_refused(who, refused, unreached?)`. Used by `boundaries`,
  `errors`, `name_hunt`, `nest_depth`, `parens` and `stack`.
- [headers.shale](headers.shale): who chooses a file's names:
  `header_names(nodes)`, what an app's or platform's header dictates and what
  a `module` header exposes; `exposed_by_folder(dir)`, `is_exposed_module(path)`
  and `is_private_module(path)`, what a package or platform exposes; and
  `LANGUAGE_FIXED_NAMES`, the methods Roc calls by name. Used by `boundaries`,
  `families`, `name_hunt`, `plain_names` and `stack`.
- [house.shale](house.shale): `HOUSE_CHECKS`, the checks that are only our
  house style, and `is_left_out(recipe, check)`, which `house=0` turns on. Used
  by `boundaries`, `foreign`, `name_hunt`, `nest_depth`, `parens`,
  `plain_names` and `stack`.
- [findings.shale](findings.shale): `print_findings(summary, lines)` prints
  the summary, the findings and the summary again. The first copy survives
  when Shale's output limit cuts a long list short, and a caller that reads
  only the last line still gets the counts. Used by `errors`, `methods`,
  `name_hunt`, `nest_depth`, `parens`, `roc_commas` and `stack`.
- [pinned.shale](pinned.shale): `roc_bin(who, self_test)` gives the path
  of the Roc compiler that the repository's `roc-version` file names
  (installed at `tools:roc-<version>/roc`), and `SELF_TEST_PIN` is the
  version every self-test uses. Used by `boundaries`, `errors`, `methods`,
  `name_hunt`, `roc_commas` and `stack`.
- [groups.shale](groups.shale): the stacking rule, the bracket groups and
  their items and tokens, `groups_in_lines(ls)`, what a rewrite does to
  them, `group_to_stack(l, tokens, width)` and
  `groups_to_unstack(ls, tokens, width)`, and what it can't reach,
  `without_targets_section(ls)`. Used by `roc_commas` and `stack`, so the two
  read a group the same way.
- [types.shale](types.shale): runs roc-types, Shale's tool that asks Roc's
  type checker for the type of every expression and pattern in an app.
  `roc_types(root, roc)` reads one app; `roc_types_for_files(who, root,
  roc, paths, exempt)` gathers the types for a recipe's files from one or
  more apps and lists the files none of them reaches. Used by
  `boundaries`, `errors`, `methods` and `name_hunt`.

- [declared.shale](declared.shale): what the naming recipes read out of a
  parse tree: `declarations_of(file)`, each type, value and record field a
  file declares, with `render_type`, `name_words` and `word_set`. Used by
  `families`, `name_hunt`, `name_types`, `plain_names` and `vocabulary`.
- [gate_recipe.shale](gate_recipe.shale): what the recipes that exit
  nonzero on findings share: `verdict_of`, their counts and lines, and
  `check_plants`, a self-test that runs the recipe as a child program once
  for each row of a table, so a row judges the exit code as well as the
  findings. Used by `families`, `name_types`, `plain_names` and `vocabulary`.

## Tools the recipes need

Run the install scripts from the shale checkout.

- roc-ast, Roc's own parser, gives the recipes their syntax trees through
  Shale's `roc_tree`. Install it with `shale tools/roc_ast/install.shale`.
- roc-types gives the types. Install it with
  `shale tools/roc_types/install.shale`.
- The Roc compiler named in `roc-version`, at `tools:roc-<version>/roc`.

## Writing a recipe that edits Roc

roc-ast's columns count bytes, and a span's end is exclusive. Spans can
overlap: `type-mod` and `s-type-decl` both start at the type's name. The
file node starts after the leading comments, and a `|>` is an
`e-arrow-call`. `roc check` can report two errors on one line, or report
an error at the line of the definition around it, so match its errors to
an edit by span, not by line. A Shale run starts at most 64 programs, so a
recipe that compiles once per file must stop and resume, as
`methods.shale`'s `from=` does.
