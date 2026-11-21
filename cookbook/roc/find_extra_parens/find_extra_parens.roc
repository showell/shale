## find_extra_parens, in Roc on Shale's platform (roc_platform/README.md):
## the parentheses and braceless `if`s that a code emitter leaves in Roc,
## which neither `roc check` nor `roc fmt` sees, found and, with `write=1`,
## rewritten. Its launcher, cookbook/roc/find_extra_parens.shale, is how it
## is run, and its header says the checks (`paren_if`, `wrapped_body`,
## `else_block`, `lambda_if`) and the rewrite. check_goldens.shale, beside
## this, holds its output on the plants to its goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/find_extra_parens.shale glob=chat/**/*.roc
##   cd <repo> && shale cookbook:roc/find_extra_parens.shale glob=chat/**/*.roc write=1
##   shale cookbook:roc/find_extra_parens.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `house` (`house=0` leaves out the
## house style, `paren_if` and `lambda_if`), `write` (`write=1` rewrites the
## files) and `self_test` (`self_test=1` checks the plants and rewrites
## copies of them).
##
## This root globs the files (`Shale.glob!`), rewrites them when asked
## (Rewriting, by Edits), reports (Report, by Checks) and prints the verdict
## (`Shale.print!`, `Shale.fail!`); SelfTest is the self-test. Finding, Text
## and Tree are the cookbook's Roc library, cookbook/roc/lib/.
## budget: steps=900000000 read_bytes=1000000000 arena_bytes=2000000000 output_bytes=4000000 programs=100
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import lib.Finding
import Report
import Rewriting
import SelfTest

main! : {} => {}
main! = |{}| {
	self_test = Shale.arg!("self_test", "") == "1"
	pattern = if self_test {
		"${SelfTest.plants}/*.roc"
	} else {
		Shale.arg!("glob", "**/*.roc")
	}
	write = Shale.arg!("write", "") == "1"
	paths = Shale.glob!(pattern)
	if write {
		refuse_house_rewrite!({})
		made = Rewriting.rewrite!(paths, Rewriting.roc_bin!(Bool.False))
		Shale.print!("find_extra_parens: ${made.to_str()} edits")
	}
	found = Report.make!(paths)
	print_findings!(found.summary, found.findings.map(Finding.line_of))
	fail_if_refused!(found.refused)
	if self_test {
		SelfTest.run!(paths, found)
	}
}

## A rewrite applies our house style, so `house=0` refuses one.
refuse_house_rewrite! : {} => {}
refuse_house_rewrite! = |{}|
	match Report.read_house!({}) {
		Kept => {}
		LeftOut => Shale.fail!("find_extra_parens: write=1 applies our house style, so it refuses house=0")
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

## The files Roc's parser refused, named, then a stop (cookbook/roc/parsed.shale's
## `fail_if_refused`); nothing when it refused none.
fail_if_refused! : List(Str) => {}
fail_if_refused! = |refused|
	if !(refused.is_empty()) {
		for refusal in refused {
			Shale.print!("refused: ${refusal}")
		}
		refusals = "roc-ast refused ${refused.len().to_str()} files (named above)"
		why = "which have no tree to judge; every other file was judged"
		Shale.fail!("find_extra_parens: ${refusals}, ${why}")
	}
