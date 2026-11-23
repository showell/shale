## check_and_fix_group_stacking, in Roc on Shale's platform (roc_platform/README.md): the
## groups in .roc files laid out against the stacking rule and the lines
## past the width, found and, with `write=1`, laid out by the rule. Its
## launcher, cookbook/roc/check_and_fix_group_stacking.shale, is how it is run, and its
## header says the rule, the findings and the rewrite. check_goldens.shale,
## beside this, holds its output on the plants to its goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/check_and_fix_group_stacking.shale glob=chat/**/*.roc
##   cd <repo> && shale cookbook:roc/check_and_fix_group_stacking.shale glob=chat/**/*.roc write=1
##   shale cookbook:roc/check_and_fix_group_stacking.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `skip` (files the glob finds to leave
## out, comma-separated), `width` (100), `tokens` (6), `dictated` (a table of
## functions whose arguments something outside the file dictates), `house`
## (`house=0` leaves out every finding, all of them house style), `write`
## (`write=1` rewrites the files) and `self_test` (`self_test=1` checks the
## plants and rewrites copies of them).
##
## This root reads the arguments, globs the files, rewrites them when asked
## (Rewrite) and prints the findings (Report, by Annotations) and the
## verdict; SelfTest is the self-test. Groups, Stacking, Headers, Finding
## and Text are the cookbook's Roc library, cookbook/roc/lib/.
## budget: steps=900000000 read_bytes=1000000000 arena_bytes=2000000000 output_bytes=4000000 programs=100 roc_cpu_ms=120000
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import lib.Finding
import lib.Stacking
import Report
import Rewrite
import SelfTest

main! : {} => {}
main! = |{}| {
	self_test = Shale.arg!("self_test", "") == "1"
	pattern = if self_test {
		"${SelfTest.plants}/*.roc"
	} else {
		Shale.arg!("glob", "**/*.roc")
	}
	skip = Shale.arg!("skip", "")
	dictated = if self_test {
		"${SelfTest.plants}/dictated.txt"
	} else {
		Shale.arg!("dictated", "")
	}
	width = integer_of!("width", Shale.arg!("width", "100"))
	write = Shale.arg!("write", "") == "1"
	tokens = integer_of!("tokens", Shale.arg!("tokens", Stacking.stack_tokens.to_str()))
	settings = { rule: { tokens, width }, dictated }
	if self_test {
		SelfTest.run!(chosen_files!(pattern, skip), settings)
	} else if write {
		refuse_house_rewrite!({})
		# Stop before any change if the tree's Roc is missing.
		roc = Rewrite.roc_bin!(Bool.False)
		paths = chosen_files!(pattern, skip)
		_first = Rewrite.trailing_commas!(paths, { rule: settings.rule, roc })
		judged = Report.findings_in!(paths, settings)
		left = judged.found.map(Finding.line_of)
		print_findings!("${left.len().to_str()} left, each a line the rewrite can't reach", left)
		SelfTest.fail_if_refused!(judged.refused)
	} else {
		paths = chosen_files!(pattern, skip)
		judged = Report.findings_in!(paths, settings)
		house_is_off = read_house_is_off!({})
		kept = if house_is_off {
			[]
		} else {
			judged.found
		}
		count_of = |check| kept.count_if(|finding| finding.check == check).to_str()
		counts = "${count_of("stack")} to stack, ${count_of("unstack")} to unstack, ${count_of("long")} long, ${count_of("wide string")} wide string"
		note = if house_is_off {
			"; left out as house style: stack, unstack, long, wide string"
		} else {
			""
		}
		summary = "${kept.len().to_str()} findings in ${paths.len().to_str()} files: ${counts}${note}"
		print_findings!(summary, kept.map(Finding.line_of))
		SelfTest.fail_if_refused!(judged.refused)
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
			Shale.fail!("check_and_fix_group_stacking: ${name}=${given} is below 0")
			0
		}
		Err(_) => {
			Shale.fail!("`arg`: ${name}=${given} is not an integer, and its default is one")
			0
		}
	}

## The files the glob finds, less the ones `skip` names. A name in `skip`
## that the glob doesn't find stops the run, so a renamed file can't go
## unchecked.
chosen_files! : Str, Str => List(Str)
chosen_files! = |pattern, skip| {
	found = Shale.glob!(pattern)
	if skip == "" {
		return found
	}
	skipped = skip.split_on(",")
	for name in skipped {
		if !(found.contains(name)) {
			Shale.fail!("check_and_fix_group_stacking: skip names ${name}, which the glob doesn't find")
		}
	}
	found.drop_if(|path| skipped.contains(path))
}

## Whether `house=0` leaves the house style out (cookbook/roc/house.shale):
## here that is every finding.
read_house_is_off! : {} => Bool
read_house_is_off! = |{}| {
	house = Shale.arg!("house", "1")
	number = match I64.from_str(house) {
		Ok(value) => value
		Err(_) => {
			Shale.fail!("`arg`: house=${house} is not an integer, and its default is one")
			1
		}
	}
	if number != 0 and number != 1 {
		Shale.fail!("house must be 1 (the default) or 0, not ${number.to_str()}")
	}
	number == 0
}

## A rewrite applies our house style, so `house=0` refuses one.
refuse_house_rewrite! : {} => {}
refuse_house_rewrite! = |{}| {
	if read_house_is_off!({}) {
		Shale.fail!("check_and_fix_group_stacking: write=1 applies our house style, so it refuses house=0")
	}
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
