# The Go cookbook

These Shale scripts hunt through a tree of Go code for what a reader may
want to look at twice. They are recipes, not builtins: read one, copy it,
and change it. Each script's header comment is its full documentation.

Each reads Go's own syntax tree through `go_tree(paths)`, Shale's builtin
over go-ast (`tools/go_ast`, the standard library's go/parser), so a
comment or a string never counts as code. Install go-ast once, from the
shale checkout:

    shale tools/go_ast/install.shale

Run a recipe from the root of the repository it reads. `glob=` names the
files (`**/*.go` by default; a glob with `*` in it goes on stdin with
`--args-stdin`), `tests=0` leaves out the `_test.go` files, and
`self_test=1`, from the shale checkout, runs the recipe over its plants:

    cd <repo> && shale <shale>/cookbook/go/name_hunt.shale tests=0
    cd <shale> && shale cookbook:go/name_hunt.shale self_test=1

Each finding is one line that starts `path:line:` and names its check,
between two copies of a summary of counts. A finding is a question for a
reader, not a verdict, so a run exits 0 on findings; it fails on a file
go-ast refuses, listed after the findings. Go's compiler already rejects
an unused import or variable, and gofmt settles layout, so no recipe here
repeats them.

## Checks

- [name_hunt.shale](name_hunt.shale) looks for awkward names, by the word
  tables the Zig and Roc name hunts read (`cookbook/words/`): a name of
  nothing but vague words (`vague`: `data`, `handleData`, `info`), a
  question that gives no bool or a bool named as a state (`question`), a
  negative flag (`negative`: `noCache`), a `*Count` or `*Path` of the
  wrong type (`name_type`), and a function that starts with a preposition
  and hides what it gives (`phrase_name`: `inView`, where `treesInView`
  names the thing).
- [nest_depth.shale](nest_depth.shale) finds calls nested three or more
  deep, like `f(g(h(x)))`, which read inside out. A method chain, an
  operator, a selector and a conversion the syntax shows (`string(b)`) add
  no level; a function literal and a composite literal start again. `min=`
  sets the depth to report.
- [smells.shale](smells.shale) finds long functions (`long_func`, `long=`
  80 lines), statements nested `deep=` 4 blocks deep (`deep_block`; an
  `else if` is no level), functions of `params=` 6 parameters or more
  (`many_params`) and `bool` parameters (`bool_param`).

## Plants

Each recipe's plants are in `plants/<recipe>/`: small `.go` files that
go-ast parses, never built. A line that must give a finding ends with a
mark, Go's own analysistest habit:

    func handleData(d Data) string { // want: vague

and one that must give two, `// want: vague, vague`. The self-test
(`check_want_marks` in [parsed.shale](parsed.shale)) holds every marked
line to exactly its marks, every unmarked line to none, a `quiet_*.go`
plant to no mark, and each check to firing on some plant, since a check no
plant fires could be blind. `zig build cookbook-test` runs them all.

## Libraries

- [parsed.shale](parsed.shale): `parse_go_files(paths)` and
  `fail_if_go_refused(who, refused)`, the files that parse and the ones
  go-ast refused; `go_paths(glob, tests)`; `kid_in` and `kids_in`, a node's
  kids by their `role`; `source_of(lines, node)`, a node's text;
  `go_words(name)`, a Go name's words (`parseURLPath` is `parse`, `url`,
  `path`); `go_word_table(file)`; and `check_want_marks`, the self-test.
- The recipes print with [roc/findings.shale](../roc/findings.shale)'s
  `print_findings`, which is no Roc's own.

## Why `go_tree` is a builtin

go-ast writes roc-ast's `--flat` format (a node a line, then its
attributes), so the host's reader, `src/shale_host_flat_tree.zig`, takes
both tools by a tag, and the prelude's `roc_tree` and `go_tree` share one
runner, `flat_tool_trees`. That is about thirty lines of Zig and a few of
prelude beyond what Roc already had, with the reader's tests shared. A
cookbook library that read the flat lines in Shale would cost a dozen steps
a line: essay-surface's sixteen files are 25,000 nodes and about 70,000
lines of go-ast's output, where the builtin reads them in one call, charged
a step a node, and the whole of a hunt over them runs in about half a
second.
