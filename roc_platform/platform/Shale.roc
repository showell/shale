## Shale.roc: written by c/gen_builtins.shale from c/builtins.txt; never
## edit it. Shale's effects, as Roc's: each one is a thin shim in the host
## (roc_platform/host/hosted.gen.c, `shale_<name>`) over a Shale builtin, run
## by Shale's own VM, so the mounts, the guards, the refusals, the budgets and
## the report are Shale's.
##
## **A REFUSAL IS A STOP, AN OUTCOME IS A VALUE**: a path that escapes the
## root or a mount, a program `run` refuses, a budget run out: the run stops
## with Shale's own words, and the Roc never sees an answer. A missing file
## or a program's exit code is an answer.
##
## **EVERY PATH IS A SHALE PATH**: relative to the root the run starts in,
## or `name:rest` through a mount.
##
## **ONLY EFFECTS ARE HERE**: what Roc can do itself (lines, sorting, text)
## is Roc's, not Shale's.
Shale := [].{

	## The launch's `name=value`, or the fallback when it was not given.
	arg! : Str, Str => Str

	## Whether anything is at the path.
	exists! : Str => Bool

	## The run stopped, saying why (Shale's `fail`): never returns.
	fail! : Str => {}

	## The files a glob names, in the walk's order (Shale's `glob`).
	glob! : Str => List(Str)

	## Every line of the file holding the literal, numbered from 1 (Shale's
	## `grep_all`, any size of file).
	grep_all! : Str, Str => List({ line : U64, text : Str })

	## A folder's names, sorted, or `Missing` when nothing is at the path
	## (Shale's `ls`).
	list! : Str => [Found(List(Str)), Missing]

	## The text and a newline on the run's output (Shale's `print`).
	print! : Str => {}

	## The leftmost match of the pattern in the text (Shale's `re_find`): where
	## it starts (from 1), its text and each group's text, or `NoMatch`. A
	## pattern Shale cannot read stops the run.
	re_find! : Str, Str => [Found({ at : U64, text : Str, groups : List(Str) }), NoMatch]

	## Every match of the pattern in the text, left to right, as `re_find!` gives
	## each (Shale's `re_find_all`).
	re_find_all! : Str, Str => List({ at : U64, text : Str, groups : List(Str) })

	## The lines of the text holding a match of the pattern, numbered from 1
	## (Shale's `re_lines`).
	re_lines! : Str, Str => List({ line : U64, text : Str })

	## Whether the text holds a match of the pattern (Shale's `re_match`).
	re_match! : Str, Str => Bool

	## The text with every match of the pattern replaced (Shale's `re_replace`;
	## `\1` to `\9` in the replacement are a group's text). A pattern Shale
	## cannot read stops the run.
	re_replace! : Str, Str, Str => Str

	## The file's text, or `Missing` when nothing is at the path.
	read! : Str => [Found(Str), Missing]

	## Roc's own parser's tree of each file, in order (Shale's `roc_tree`,
	## through roc-ast): its nodes, or `err`, why roc-ast refused it (`no such
	## file` for a missing one), `""` for a file read. A node's `parent` and
	## `kids` are places in `nodes` counted from 1, `parent` 0 for the root;
	## `line` is 0 for a node with no span. `attrs` are the attributes Roc's tree
	## gives it (`raw`, `name`, `method`, `op`, ...), each by its name.
	roc_tree! : List(Str) => List({ path : Str, err : Str, nodes : List({ kind : Str, depth : U64, parent : U64, kids : List(U64), line : U64, col : U64, end_line : U64, end_col : U64, attrs : List({ name : Str, value : Str }) }) })

	## A program run to its end, or stopped at its timeout: its exit code (`-1`
	## when it did not exit) and what the keep kept of its output. `want` is
	## `run`'s `want`, `""` for none.
	run! : { argv : List(Str), keep : [Head(U64), Tail(U64), Whole(U64)], timeout_ms : U64, want : Str } => { code : I64, out : Str }

	## The file made or replaced, whole, with `text` (Shale's `replace`).
	write! : Str, Str => {}
}
