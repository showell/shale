## find_unused_imports, in Roc on Shale's platform (roc_platform/README.md):
## the import lines a Roc file never uses, since Roc doesn't warn of one. Its
## launcher, cookbook/roc/find_unused_imports.shale, is how it is run, and
## its header says the rule, `unused_import`. check_goldens.shale, beside
## this, holds its output on the plants to its goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/find_unused_imports.shale glob=app/**/*.roc
##   shale cookbook:roc/find_unused_imports.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`; several, separated by commas) and
## `self_test` (`self_test=1` runs the plants, each through the launcher).
##
## This root globs the files, has Roc's parser read them (`Shale.roc_tree!`)
## and each one's text (`Shale.read!`), and prints the verdict
## (`Shale.print!`, `Shale.fail!`). SelfTest, beside it, is the plants' run.
## The rule is the cookbook's Roc library's Imports (cookbook/roc/lib/), its
## one home, with Tree, Text and Finding.
## budget: steps=800000000 arena_bytes=2000000000 output_bytes=4000000 programs=100
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import lib.Finding
import lib.Imports
import lib.Text
import SelfTest

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
## a caller reads.
run_check! : {} => {}
run_check! = |{}| {
	paths = list_files!(Shale.arg!("glob", "**/*.roc"))
	var $report = []
	var $refused = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			source_lines = read_text!(file.path).split_on("\n")
			for unused in Imports.unused_imports(file.nodes, source_lines) {
				what = "${unused.label} is imported and never used: delete the line"
				$report = $report.append({ path: file.path, line: unused.line, check: "unused_import", what })
			}
		} else if file.err.starts_with(file.path) {
			$refused = $refused.append(file.err)
		} else {
			$refused = $refused.append("${file.path}: ${file.err}")
		}
	}
	found = $report.sort_with(Finding.order).map(Finding.line_of)
	counts = "${paths.len().to_str()} files; unused_import ${found.len().to_str()}"
	summary = "find_unused_imports (Roc): ${counts}"
	print_findings!(summary, found)
	if !($refused.is_empty()) {
		for refusal in $refused {
			Shale.print!("refused: ${refusal}")
		}
		refused_count = $refused.len().to_str()
		refusals = "roc-ast refused ${refused_count} files (named above)"
		why = "which have no tree to judge; every other file was judged"
		Shale.fail!("find_unused_imports: ${refusals}, ${why}")
	}
	if !(found.is_empty()) {
		Shale.fail!(summary)
	}
}

## The files of each comma-separated glob, in the order given, a file twice
## when two globs name it.
list_files! : Str => List(Str)
list_files! = |patterns| {
	var $paths = []
	for pattern in patterns.split_on(",") {
		matching = Shale.glob!(Text.trim(pattern))
		$paths = $paths.concat(matching)
	}
	$paths
}

## The file's text. A file the parser read and the read finds gone stops the
## run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("find_unused_imports: ${path} went missing during the check")
			""
		}
	}

# ── The verdict

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
