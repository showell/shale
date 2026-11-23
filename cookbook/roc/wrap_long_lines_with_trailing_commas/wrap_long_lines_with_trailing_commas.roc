## wrap_long_lines_with_trailing_commas, in Roc on Shale's platform (roc_platform/README.md): long
## Roc lines shortened, and with `tokens=N` groups laid out by the stacking
## rule, by trailing commas that `roc fmt` reads. Its launcher,
## cookbook/roc/wrap_long_lines_with_trailing_commas.shale, is how it is run, and its header says
## the rule, the rewrite and the list it prints. check_goldens.shale, beside
## this, holds its output and the files it rewrites to its goldens.
##
##   cd <repo> && shale cookbook:roc/wrap_long_lines_with_trailing_commas.shale dir=roc width=100
##   cd <repo> && shale cookbook:roc/wrap_long_lines_with_trailing_commas.shale dir=app,platform tokens=6
##   shale cookbook:roc/wrap_long_lines_with_trailing_commas.shale self_test=1
##
## The arguments: `dir` (`roc`; several, comma-separated), `width` (100),
## `tokens` (0, wide lines alone), `write` (`write=0` reports and writes
## nothing), `self_test` and `work_base`, the folder the work copies go in,
## which the launcher gives.
##
## This root reads the arguments, rewrites each folder (Rewrite) and prints
## the lines left wide (WideLines); SelfTest is the self-test. Groups,
## Stacking, Packed and Bytes are the cookbook's Roc library,
## cookbook/roc/lib/, the one home of the stacking rule.
## budget: steps=1000000000 read_bytes=2000000000 arena_bytes=2000000000 output_bytes=4000000 changes=5000 roc_cpu_ms=600000
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import lib.Stacking
import lib.Text
import Rewrite
import SelfTest
import WideLines

main! : {} => {}
main! = |{}| {
	self_test = Shale.arg!("self_test", "") == "1"
	work_base = Shale.arg!("work_base", "scratch:")
	dirs_given = Shale.arg!("dir", "roc")
	width = integer_of!("width", Shale.arg!("width", "100"))
	tokens = if self_test {
		Stacking.stack_tokens
	} else {
		integer_of!("tokens", Shale.arg!("tokens", "0"))
	}
	write_given = Shale.arg!("write", "1")
	if write_given != "0" and write_given != "1" {
		Shale.fail!("wrap_long_lines_with_trailing_commas: write=${write_given}: write must be 1 (the default) or 0")
	}
	write = self_test or write_given == "1"
	roc = Rewrite.roc_bin!(self_test)
	settings = { rule: { tokens, width }, roc, work_base }
	if self_test {
		SelfTest.run!(settings)
	} else {
		var $report = WideLines.fresh
		for dir in dirs_of!(dirs_given) {
			work = Rewrite.work_of!(dir, { self_test: Bool.False, work_base })
			done = match Rewrite.rewrite_in_work!(dir, { work, settings }) {
				Ok(finished) => finished
				Err(StillChanging(touched)) => {
					Shale.fail!(Rewrite.still_changing(touched))
					{ work, paths: [] }
				}
			}
			at = if write {
				Rewrite.write_back!(dir, done)
				dir
			} else {
				done.work
			}
			$report = WideLines.report_wide_lines!($report, { dir, at, paths: done.paths, width })
			Shale.remove_tree!(done.work)
		}
		remove_work_base!(work_base)
		report = $report
		said = if write {
			""
		} else {
			"report, nothing written: "
		}
		print_findings!("${said}${WideLines.summary(report, width)}", report.long)
		fail_if_refused!(report.refused)
	}
	if self_test {
		remove_work_base!(work_base)
	}
}

## The whole number an argument gives; another value stops the run, as
## Shale's `arg` does for an integer default. (Each `Shale.arg!` names its
## argument as a literal, so the launcher's check of the values finds it.)
integer_of! : Str, Str => U64
integer_of! = |name, given|
	match I64.from_str(given) {
		Ok(value) if value >= 0 => value.to_u64_wrap()
		Ok(_) => {
			Shale.fail!("wrap_long_lines_with_trailing_commas: ${name}=${given} is below 0")
			0
		}
		Err(_) => {
			Shale.fail!("`arg`: ${name}=${given} is not an integer, and its default is one")
			0
		}
	}

## The folders `dir=` names, each held to be a path and not an option.
dirs_of! : Str => List(Str)
dirs_of! = |given| {
	var $dirs = []
	for piece in given.split_on(",") {
		dir = Text.trim(piece)
		if dir.starts_with("-") {
			Shale.fail!("`wrap_long_lines_with_trailing_commas`'s `dir` is \"${dir}\": a value that starts with `-` would be one of the program's options")
		}
		if dir != "" {
			$dirs = $dirs.append(dir)
		}
	}
	if $dirs.is_empty() {
		Shale.fail!("wrap_long_lines_with_trailing_commas: dir= names no folder")
	}
	$dirs
}

## The work folder, when the launcher made a fresh one for want of a
## `scratch:` mount, removed once the run has finished.
remove_work_base! : Str => {}
remove_work_base! = |work_base| {
	if work_base != "scratch:" {
		Shale.remove_tree!(work_base)
	}
}

## The summary, each line listed, then the summary again
## (cookbook/findings.shale): the first copy survives a cut output, and a
## gate reads the last line.
print_findings! : Str, List(Str) => {}
print_findings! = |summary, listed| {
	Shale.print!(summary)
	if !(listed.is_empty()) {
		for line in listed {
			Shale.print!(line)
		}
		Shale.print!(summary)
	}
}

## The files Roc's parser refused, named, then a stop
## (cookbook/roc/parsed.shale's `fail_if_refused`); nothing when it refused
## none.
fail_if_refused! : List(Str) => {}
fail_if_refused! = |refused| {
	if !(refused.is_empty()) {
		for refusal in refused {
			Shale.print!("refused: ${refusal}")
		}
		refusals = "roc-ast refused ${refused.len().to_str()} files (named above)"
		why = "which have no tree to judge; every other file was judged"
		Shale.fail!("wrap_long_lines_with_trailing_commas: ${refusals}, ${why}")
	}
}
