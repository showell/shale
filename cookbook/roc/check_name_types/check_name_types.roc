## check_name_types, in Roc on Shale's platform (roc_platform/README.md):
## names that disagree with their types. A reader who sees `bull_dist` takes
## it for a `Distance`, one who sees `forward : F64` beside `forward :
## Distance` can't tell which is meant, and one who sees `no_frames : Steps`
## can't tell the case from a count, so each costs a wrong guess. Its
## launcher, cookbook/roc/check_name_types.shale, is how it is run, and its
## header says the rules, `name_type`, `bare_beside_unit` and `no_value`.
## check_goldens.shale, beside this, holds its output on the plants to its
## goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/check_name_types.shale table=name_types.txt
##   shale cookbook:roc/check_name_types.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `table` (the project's table of words
## and types) and `self_test` (`self_test=1` runs the plants, each through the
## launcher).
##
## This root reads the prepositions and the table, globs the files
## (`Shale.glob!`), has Roc's parser read them (`Shale.roc_tree!`) and each
## one's text (`Shale.read!`), and prints the verdict (`Shale.print!`,
## `Shale.fail!`). Its modules are beside it: a module a check (NameType,
## BareBesideUnit, NoValue), Words and SelfTest; what they read besides
## (Declarations, Tree, Text) and Finding are the cookbook's Roc library,
## cookbook/roc/lib/.
## budget: steps=800000000 arena_bytes=2000000000 output_bytes=4000000 programs=100
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import BareBesideUnit
import lib.Declarations
import lib.Finding
import NameType
import NoValue
import SelfTest
import Words

main! : {} => {}
main! = |{}| {
	if Shale.arg!("self_test", "") == "1" {
		SelfTest.run!(checks)
	} else {
		run_check!({})
	}
}

## The check over the glob's files: the verdict printed, then a stop when a
## file didn't parse, or when there are findings, the summary the last line
## a caller reads.
run_check! : {} => {}
run_check! = |{}| {
	words = Words.read!(Shale.arg!("table", ""))
	paths = Shale.glob!(Shale.arg!("glob", "**/*.roc"))
	var $declared = Declarations.none_yet
	var $refused = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			read = {
				path: file.path,
				nodes: file.nodes,
				source_lines: read_text!(file.path).split_on("\n"),
			}
			$declared = Declarations.concat($declared, Declarations.from_file(read))
		} else if file.err.starts_with(file.path) {
			$refused = $refused.append(file.err)
		} else {
			$refused = $refused.append("${file.path}: ${file.err}")
		}
	}
	findings = NameType.findings($declared, words)
		.concat(BareBesideUnit.findings($declared, words))
		.concat(NoValue.findings($declared))
	found = findings.sort_with(Finding.order).map(Finding.line_of)
	summary = summary_of(findings, paths.len())
	print_findings!(summary, found)
	if !($refused.is_empty()) {
		for refusal in $refused {
			Shale.print!("refused: ${refusal}")
		}
		refusals = "roc-ast refused ${$refused.len().to_str()} files (named above)"
		why = "which have no tree to judge; every other file was judged"
		Shale.fail!("check_name_types: ${refusals}, ${why}")
	}
	if !(found.is_empty()) {
		Shale.fail!(summary)
	}
}

## The file's text. A file the parser read and the read finds gone stops the
## run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("check_name_types: ${path} went missing during the check")
			""
		}
	}

## The checks, in the order the summary counts them.
checks : List(Str)
checks = ["name_type", "bare_beside_unit", "no_value"]

## The counts, `check_name_types (Roc): N files; name_type N, ...`.
summary_of : List(Finding), U64 -> Str
summary_of = |findings, file_count| {
	"check_name_types (Roc): ${file_count.to_str()} files; ${Finding.counts_of(findings, checks)}"
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
