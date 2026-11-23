## find_retired_words, in Roc on Shale's platform (roc_platform/README.md): a
## project held to the words it has decided on. The project keeps a file of
## them, `kept: retired, retired` a line, and the recipe reports each
## identifier and comment in its .roc files that uses a retired word. Its
## launcher, cookbook/roc/find_retired_words.shale, is how it is run, and its
## header says the rules, `retired_name` and `retired_comment`.
## check_goldens.shale, beside this, holds its output on the plants to its
## goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/find_retired_words.shale words=vocabulary.txt
##   shale cookbook:roc/find_retired_words.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `words` (the project's file of
## decided words, which must be given) and `self_test` (`self_test=1` runs
## the plants, each through the launcher).
##
## This root reads the words file (Words), globs the files (`Shale.glob!`),
## has Roc's parser read them (`Shale.roc_tree!`) and each one's text
## (`Shale.read!`), and prints the verdict (`Shale.print!`, `Shale.fail!`).
## Its modules are beside it: Retired, the two checks, over Identifiers and
## Words; and SelfTest. Tree, Text and Finding are the cookbook's Roc
## library, cookbook/roc/lib/.
## budget: steps=800000000 arena_bytes=2000000000 output_bytes=4000000 programs=100 roc_cpu_ms=600000
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import lib.Finding
import Retired
import SelfTest
import Words

main! : {} => {}
main! = |{}| {
	is_self_test = Shale.arg!("self_test", "") == "1"
	pattern = Shale.arg!("glob", "**/*.roc")
	words_file = Shale.arg!("words", "")
	if is_self_test {
		SelfTest.run!(checks)
	} else {
		run_check!({ pattern, words_file })
	}
}

## The check over the glob's files: the verdict printed, then a stop when a
## file didn't parse, or else when there are findings, the summary the last
## line a caller reads.
run_check! : { pattern : Str, words_file : Str } => {}
run_check! = |{ pattern, words_file }| {
	if words_file == "" {
		Shale.fail!("find_retired_words: give words=<the project's file of decided words>")
	}
	decided = Words.read!(words_file)
	paths = Shale.glob!(pattern)
	var $findings = []
	var $refused = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			names = Retired.names({ path: file.path, syntax_nodes: file.nodes }, decided)
			comments = Retired.comments({ path: file.path, text: read_text!(file.path) }, decided)
			$findings = $findings.concat(names).concat(comments)
		} else if file.err.starts_with(file.path) {
			$refused = $refused.append(file.err)
		} else {
			$refused = $refused.append("${file.path}: ${file.err}")
		}
	}
	found = $findings.sort_with(Finding.order).map(Finding.line_of)
	summary = summary_of($findings, paths.len())
	print_findings!(summary, found)
	if !($refused.is_empty()) {
		for refusal in $refused {
			Shale.print!("refused: ${refusal}")
		}
		refusals = "roc-ast refused ${$refused.len().to_str()} files (named above)"
		why = "which have no tree to judge; every other file was judged"
		Shale.fail!("find_retired_words: ${refusals}, ${why}")
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
			Shale.fail!("find_retired_words: ${path} went missing during the check")
			""
		}
	}

## The checks, in the order the summary counts them.
checks : List(Str)
checks = ["retired_name", "retired_comment"]

## The counts, `find_retired_words (Roc): N files; retired_name N,
## retired_comment N`.
summary_of : List(Finding), U64 -> Str
summary_of = |findings, file_count| {
	"find_retired_words (Roc): ${file_count.to_str()} files; ${Finding.counts_of(findings, checks)}"
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
