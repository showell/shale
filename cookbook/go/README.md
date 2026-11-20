# The Go cookbook

These Shale scripts hunt through a tree of Go code for what a reader may
want to look at twice. They are recipes to read, copy and change. Each
script's header comment is its full documentation.

Each reads Go's own syntax tree through `go_tree(paths)`, Shale's builtin
over go-ast (`tools/go_ast`, the standard library's go/parser), so only
code counts: comments and strings are skipped. Install go-ast once, from
the shale checkout:

    shale tools/go_ast/install.shale

Run a recipe from the root of the repository it reads. `glob=` names the
files (`**/*.go` by default; a glob with `*` in it goes on stdin with
`--args-stdin`), `tests=0` skips the `_test.go` files, and `self_test=1`,
from the shale checkout, runs the recipe over its plants:

    cd <repo> && shale <shale>/cookbook/go/hunt_names.shale tests=0
    cd <shale> && shale cookbook:go/hunt_names.shale self_test=1

Each finding is one line that starts `path:line:` and names its check,
between two copies of a summary of counts. A finding is a question for a
reader, so a run with findings exits 0; it fails on a file go-ast refuses,
listed after the findings. The recipes leave unused imports and layout to
Go's compiler and gofmt.

## Checks

- [hunt_names.shale](hunt_names.shale): awkward names, by the word tables
  the Zig and Roc name hunts read (`cookbook/words/`).
- [find_deep_calls.shale](find_deep_calls.shale): calls nested three or
  more deep, like `f(g(h(x)))`, which read inside out.
- [find_smells.shale](find_smells.shale): long functions, deep nesting,
  many parameters and `bool` parameters.

## Plants

Each recipe's plants are in `plants/<recipe>/`: small `.go` files that
go-ast parses and Go never builds. A line that must give a finding ends
with a mark, Go's own analysistest habit:

    func handleData(d Data) string { // want: vague

and one that must give two, `// want: vague, vague`. The self-test
(`check_want_marks` in [parsed.shale](parsed.shale)) holds every marked
line to exactly its marks, every unmarked line to none, a `quiet_*.go`
plant to no finding, and each check to firing on some plant, so a blind
check shows. C's full tier (`shale c/build.shale full=1`) runs them all.

## Libraries

- [parsed.shale](parsed.shale): parsing, a node's kids by `role`, a node's
  source, a Go name's words, and the self-test.
- The recipes print with [findings.shale](../findings.shale)'s
  `print_findings`, which the whole cookbook shares.

## Why `go_tree` is a builtin

go-ast writes roc-ast's `--flat` format (a node a line, then its
attributes), so the host's reader, `c/host/flat_tree.c`, takes both tools,
and the prelude's `roc_tree` and `go_tree` share one runner,
`flat_tool_trees`. The host reads a whole tree in one call, charged a step
a node; a reader written in Shale would cost about a dozen steps a line.
