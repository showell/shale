## find_names_and_docs_that_say_too_little, in Roc on Shale's platform (roc_platform/README.md):
## names and top comments that say less than they should. Its launcher,
## cookbook/roc/find_names_and_docs_that_say_too_little.shale, is how it is run, and its header
## says the rules, `pure_mutator`, `abbreviation` and `module_doc`.
## check_goldens.shale, beside this, holds its output on the plants to its
## goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/find_names_and_docs_that_say_too_little.shale glob=app/**/*.roc
##   shale cookbook:roc/find_names_and_docs_that_say_too_little.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `abbreviations`, `words`, `accepted`,
## `terms`, `house` (`house=0` leaves out the house style, `abbreviation`) and
## `self_test` (`self_test=1` runs the plants, each through the launcher).
##
## This root reads the word lists, runs each check on each file and prints
## the verdict. Its modules are beside it: one a check (PureMutator,
## Abbreviation, ModuleDoc) and SelfTest. What they read (Tree, Declarations,
## Text) and Finding are the cookbook's Roc library, cookbook/roc/lib/.
## budget: steps=800000000 arena_bytes=2000000000 output_bytes=4000000 roc_cpu_ms=600000 programs=100
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import lib.Declarations
import lib.Finding
import lib.Text
import Abbreviation
import ModuleDoc
import PureMutator
import SelfTest

main! : {} => {}
main! = |{}| {
	if Shale.arg!("self_test", "") == "1" {
		SelfTest.run!(checks)
	} else {
		verdict = run_check!({})
		if !(List.is_empty(verdict.found)) {
			Shale.fail!(verdict.summary)
		}
	}
}

# ── The word lists ───────────────────────────────────────────────────────

read_word_list! : Str => Str
read_word_list! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("no word list ${path}")
			""
		}
	}

## A list of one word per line, any note after `--` cut off, as a set. A
## word listed twice, or a line of two words, stops the run.
word_set! : Str => Set(Str)
word_set! = |path| {
	var $set = Set.empty()
	for l in Text.lines(read_word_list!(path)) {
		word = Str.trim(Text.before_first(l, "--"))
		if Str.contains(word, " ") or Set.contains($set, word) {
			Shale.fail!("${path}: `${word}` is no single new word")
		}
		if word != "" {
			$set = Set.insert($set, word)
		}
	}
	$set
}

## The accepted terms file: a term a line, each with its reason after `--`.
## A line without a reason stops the run.
accepted_terms! : Str => Set(Str)
accepted_terms! = |path| {
	for l in Text.lines(read_word_list!(path)) {
		parts = Str.split_on(l, "--")
		term = Str.trim(List.first(parts) ?? "")
		reason = Str.trim(List.get(parts, 1) ?? "")
		if term != "" and reason == "" {
			Shale.fail!("${path}: `${term}` has no reason after `--`")
		}
	}
	word_set!(path)
}

# ── The run ──────────────────────────────────────────────────────────────

## Whether `house=0` leaves the house style out (cookbook/roc/house.shale).
house_is_off! : {} => Bool
house_is_off! = |{}| {
	house = Shale.arg!("house", "1")
	if house != "0" and house != "1" {
		Shale.fail!("house must be 1 (the default) or 0, not ${house}")
	}
	house == "0"
}

## The checks, in the order the summary counts them.
checks : List(Str)
checks = ["pure_mutator", "abbreviation", "module_doc"]

## The summary and the finding lines, sorted.
verdict_of : List(Finding), U64 -> { summary : Str, found : List(Str) }
verdict_of = |report, file_count| {
	sorted = List.sort_with(report, Finding.order)
	found = sorted.map(Finding.line_of)
	{ summary: "find_names_and_docs_that_say_too_little (Roc): ${U64.to_str(file_count)} files; ${Finding.counts_of(sorted, checks)}", found }
}

run_check! : {} => { summary : Str, found : List(Str) }
run_check! = |{}| {
	abbreviations = word_set!(Shale.arg!("abbreviations", "cookbook:words/abbreviations.txt"))
	words = word_set!(Shale.arg!("words", "cookbook:words/short_words.txt"))
	accepted_here = accepted_terms!("cookbook:words/accepted_terms.txt")
	project_accepted = Shale.arg!("accepted", "")
	accepted = if project_accepted == "" {
		accepted_here
	} else {
		Set.union(accepted_here, accepted_terms!(project_accepted))
	}
	lists = { abbreviations, words, accepted }
	terms_file = Shale.arg!("terms", "")
	terms = if terms_file == "" {
		Set.empty()
	} else {
		word_set!(terms_file)
	}
	paths = Shale.glob!(Shale.arg!("glob", "**/*.roc"))
	var $report = []
	var $refused = []
	for f in Shale.roc_tree!(paths) {
		if f.err != "" {
			$refused = $refused.append(
				if Str.starts_with(f.err, f.path) {
					f.err
				} else {
					"${f.path}: ${f.err}"
				},
			)
		} else {
			src = match Shale.read!(f.path) {
				Found(text) => Str.split_on(text, "\n")
				Missing => []
			}
			declared = Declarations.from_file({ path: f.path, nodes: f.nodes, source_lines: src })
			aliases = List.fold(
				declared.types,
				Dict.empty(),
				|m, d| if !(d.is_nominal) {
					Dict.insert(m, d.name, d.underlying)
				} else {
					m
				},
			)
			for d in declared.values {
				$report = List.concat($report, PureMutator.judge(f.path, d, aliases, terms))
			}
			if !(house_is_off!({})) {
				$report = List.concat($report, Abbreviation.judge(f.path, f.nodes, declared, lists))
			}
			$report = List.concat($report, ModuleDoc.judge!(f.path, f.nodes, declared, src))
		}
	}
	verdict = verdict_of($report, List.len(paths))
	summary = if house_is_off!({}) {
		"${verdict.summary}; left out as house style: abbreviation"
	} else {
		verdict.summary
	}
	print_findings!(summary, verdict.found)
	if !(List.is_empty($refused)) {
		for r in $refused {
			Shale.print!("refused: ${r}")
		}
		Shale.fail!("find_names_and_docs_that_say_too_little: roc-ast refused ${U64.to_str(List.len($refused))} files (named above), which have no tree to judge; every other file was judged")
	}
	{ summary, found: verdict.found }
}

## The summary, each finding, then the summary again (cookbook/findings.shale).
print_findings! : Str, List(Str) => {}
print_findings! = |summary, found| {
	Shale.print!(summary)
	if !(List.is_empty(found)) {
		for l in found {
			Shale.print!(l)
		}
		Shale.print!(summary)
	}
}
