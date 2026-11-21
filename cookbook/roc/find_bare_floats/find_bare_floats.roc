## find_bare_floats, in Roc on Shale's platform (roc_platform/README.md):
## every bare `F64` in a project's declarations, for a project that keeps its
## floats inside unit types, where a `Metres` says what an `F64` doesn't.
## What must stay a float is named in the project's table, with a reason. Its
## launcher, cookbook/roc/find_bare_floats.shale, is how it is run, and its
## header says the rules, `bare_float` and `unused_exception`.
## check_goldens.shale, beside this, holds its output on the plants to its
## goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/find_bare_floats.shale table=bare_floats.txt
##   shale cookbook:roc/find_bare_floats.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `table` (the project's exceptions)
## and `self_test` (`self_test=1` runs the plants, each through the
## launcher).
##
## This root reads the table (`Shale.read!`), globs the files
## (`Shale.glob!`), has Roc's parser read them (`Shale.roc_tree!`), and
## prints the verdict (`Shale.print!`, `Shale.fail!`). Its modules are
## beside it: the rule (Floats), what it spares (TestHelpers, Exceptions),
## what they read (Tree, Text), Finding and SelfTest.
## budget: steps=800000000 arena_bytes=2000000000 output_bytes=4000000 programs=100
app [main!] { pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import Exceptions
import Finding
import Floats
import SelfTest
import Tree

main! : {} => {}
main! = |{}| {
	if Shale.arg!("self_test", "") == "1" {
		SelfTest.run!({})
	} else {
		run_check!({})
	}
}

## The check over the glob's files: the verdict printed, then a stop when a
## file didn't parse, or when there are findings, the summary the last line
## a caller reads. The table is read before the files are globbed, so a bad
## table stops the run before anything is judged.
run_check! : {} => {}
run_check! = |{}| {
	exceptions = Exceptions.read!(Shale.arg!("table", ""))
	paths = Shale.glob!(Shale.arg!("glob", "**/*.roc"))
	var $findings = []
	var $covered = []
	var $read_modules = Set.empty()
	$in_helpers : U64
	var $in_helpers = 0
	var $refused = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			$read_modules = $read_modules.insert(Tree.module_name(file.path))
			judged = Floats.judge_file({ path: file.path, syntax_nodes: file.nodes }, exceptions)
			$findings = $findings.concat(judged.findings)
			$covered = $covered.concat(judged.covered)
			$in_helpers = $in_helpers + judged.in_helpers
		} else if file.err.starts_with(file.path) {
			$refused = $refused.append(file.err)
		} else {
			$refused = $refused.append("${file.path}: ${file.err}")
		}
	}
	unused = Exceptions.find_unused!(
		exceptions,
		{ read_modules: $read_modules, used: Set.from_list($covered) },
	)
	if $in_helpers > 0 {
		spared = "bare `F64`s are in helpers only tests use"
		Shale.print!("${$in_helpers.to_str()} ${spared}, and aren't judged")
	}
	report = $findings.concat(unused)
	found = report.sort_with(Finding.order).map(Finding.line_of)
	summary = summary_of(report, paths.len())
	print_findings!(summary, found)
	if !($refused.is_empty()) {
		for refusal in $refused {
			Shale.print!("refused: ${refusal}")
		}
		refusals = "roc-ast refused ${$refused.len().to_str()} files (named above)"
		why = "which have no tree to judge; every other file was judged"
		Shale.fail!("find_bare_floats: ${refusals}, ${why}")
	}
	if !(found.is_empty()) {
		Shale.fail!(summary)
	}
}

## The counts, `find_bare_floats (Roc): N files; bare_float N,
## unused_exception N`.
summary_of : List(Finding), U64 -> Str
summary_of = |findings, file_count| {
	count_of = |check| findings.count_if(|finding| finding.check == check).to_str()
	counts = Finding.checks.map(|check| "${check} ${count_of(check)}")
	"find_bare_floats (Roc): ${file_count.to_str()} files; ${Str.join_with(counts, ", ")}"
}

## The summary, each finding, then the summary again (cookbook/findings.shale):
## the first copy survives a cut output, and a gate reads the last line.
print_findings! : Str, List(Str) => {}
print_findings! = |summary, finding_lines| {
	Shale.print!(summary)
	if !(finding_lines.is_empty()) {
		for line in finding_lines {
			Shale.print!(line)
		}
		Shale.print!(summary)
	}
}
