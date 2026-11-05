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

## Checks

These recipes print findings and, except for `stack.shale`'s `write=1`,
change no files. A finding is a question for a reader, not a verdict. Each
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
  optional (`None`), a `List.set(...) ?? l` that copies the list, and a
  `??` that falls back to a placeholder string.
- [boundaries.shale](boundaries.shale) asks whether each function earns
  its boundary and each parameter is really a parameter: a small function
  with one caller, a parameter every caller passes the same literal, a
  function of `{}` that could be a constant, and (with `types=1`) a `Bool`
  parameter that callers pass as a literal. Give it the whole tree, tests
  included, because it counts callers only in the files it reads.
- [nest_depth.shale](nest_depth.shale) finds calls nested three or more
  deep, like `f(g(h(x)))`, which read inside out. `min=` sets the depth to
  report.
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
  `errors`, `name_hunt`, `nest_depth` and `stack`.
- [findings.shale](findings.shale): `print_findings(summary, lines)` prints
  the summary, the findings and the summary again. The first copy survives
  when Shale's output limit cuts a long list short, and a caller that reads
  only the last line still gets the counts. Used by `errors`, `methods`,
  `name_hunt`, `nest_depth`, `roc_commas` and `stack`.
- [pinned.shale](pinned.shale): `roc_bin(who, self_test)` gives the path
  of the Roc compiler that the repository's `roc-version` file names
  (installed at `tools:roc-<version>/roc`), and `SELF_TEST_PIN` is the
  version every self-test uses. Used by `boundaries`, `errors`, `methods`,
  `name_hunt`, `roc_commas` and `stack`.
- [groups.shale](groups.shale): the stacking rule, the bracket groups and
  their items and tokens, `groups_in_lines(ls)`, and what a rewrite does to
  them, `group_to_stack(l, tokens, width)` and
  `groups_to_unstack(ls, tokens, width)`. Used by `roc_commas` and `stack`, so
  the two read a group the same way.
- [types.shale](types.shale): runs roc-types, Shale's tool that asks Roc's
  type checker for the type of every expression and pattern in an app.
  `roc_types(root, roc)` reads one app; `roc_types_for_files(who, root,
  roc, paths, exempt)` gathers the types for a recipe's files from one or
  more apps and lists the files none of them reaches. Used by
  `boundaries`, `errors`, `methods` and `name_hunt`.

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
