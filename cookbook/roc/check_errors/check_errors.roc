## check_errors, in Roc on Shale's platform (roc_platform/README.md): how Roc
## code reports failure, against the rules in roc-apps docs/roc-style.md
## ("Errors and failure"). Its launcher, cookbook/roc/check_errors.shale, is
## how it is run, and its header says the five checks, `error_alias`,
## `optional_tag`, `set_fallback`, `placeholder` and `number_fallback`.
## check_goldens.shale, beside this, holds its output on the plants to its
## goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/check_errors.shale glob='app/**/*.roc'
##   shale cookbook:roc/check_errors.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `types` (`1` asks Roc's type
## checker), `root` (the app or apps the types are read from) and
## `self_test` (`self_test=1` checks the plants, the typed ones with the
## types).
##
## This root reads the optionals' words (`Shale.read!`), globs the files
## (`Shale.glob!`), gathers the types (Types, through `Shale.run!`), has
## Roc's parser read the files (`Shale.roc_tree!`) and prints the verdict
## (`Shale.print!`, `Shale.fail!`). Its modules are beside it: a module a
## check or two (ErrorAlias, OptionalTag, Fallbacks), Types and SelfTest;
## Tree, Forest (the files' trees joined), Text, Finding and RocTypes are the
## cookbook's Roc library, cookbook/roc/lib/.
## budget: steps=400000000 arena_bytes=3000000000 output_bytes=4000000 programs=100
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import ErrorAlias
import Fallbacks
import lib.Finding
import lib.Forest
import OptionalTag
import lib.RocTypes
import SelfTest
import lib.Text
import Types

main! : {} => {}
main! = |{}| {
	self_test = Shale.arg!("self_test", "") == "1"
	plants = SelfTest.plants
	glob_asked = Shale.arg!("glob", "**/*.roc")
	glob = if self_test {
		"${plants}*.roc"
	} else {
		glob_asked
	}
	types_asked = Shale.arg!("types", "0")
	if types_asked != "0" and types_asked != "1" {
		Shale.fail!("check_errors: types=${types_asked} must be 0 (the default) or 1")
	}
	# The self-test checks its typed plants with the types, and the rest
	# without.
	use_types = self_test or types_asked == "1"
	root_asked = Shale.arg!("root", "")
	root = if self_test {
		"${plants}typed/QuietApp.roc"
	} else {
		root_asked
	}
	optionals = read_word_set!("optionals.txt")
	paths = if self_test {
		Shale.glob!(glob).concat(Shale.glob!("${plants}typed/*.roc"))
	} else {
		Shale.glob!(glob)
	}
	types = if use_types {
		roc = Types.roc_bin!("check_errors", self_test)
		# The self-test's untyped plants are exempt from being reached by its
		# root.
		exempt = if self_test {
			Set.from_list(Shale.glob!("${plants}*.roc"))
		} else {
			Set.empty()
		}
		Types.gather!({ who: "check_errors", kind: Expressions, root, roc, paths, exempt })
	} else {
		{ typing: RocTypes.no_types, unreached: [] }
	}
	var $parsed = []
	var $refused = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			$parsed = $parsed.append({ path: file.path, nodes: file.nodes })
		} else if file.err.starts_with(file.path) {
			$refused = $refused.append(file.err)
		} else {
			$refused = $refused.append("${file.path}: ${file.err}")
		}
	}
	forest = Forest.join($parsed)
	aliases = ErrorAlias.judge(forest)
	optional_found = OptionalTag.judge(forest, optionals)
	findings = aliases.findings
		.concat(optional_found.map(|found| found.finding))
		.concat(Fallbacks.judge!(forest, types.typing))
	unjudged = Text.sort_bytewise(aliases.unjudged.keys())
	counted = unjudged.map(|name| "${name} (${(aliases.unjudged.get(name) ?? 0).to_str()})")
	unjudged_said = if counted.is_empty() {
		""
	} else {
		"; error_alias not judged, declared outside the files hunted: ${Str.join_with(counted, ", ")}"
	}
	summary = "check_errors (Roc): ${paths.len().to_str()} files; ${Finding.counts_of(findings, checks)}${unjudged_said}"
	print_findings!(summary, findings.map(line_of))
	fail_if_refused!($refused, types.unreached)
	if self_test {
		judged = { paths, findings, optionals: optional_found, unjudged }
		SelfTest.run!(judged, checks)
	}
}

## The checks, in the order the summary counts them.
checks : List(Str)
checks = ["error_alias", "optional_tag", "set_fallback", "placeholder", "number_fallback"]

## A finding as its line, `path:line: check: what`, its line `?` when no node
## from the finding's on has one.
line_of : Finding -> Str
line_of = |finding|
	if finding.line == 0 {
		"${finding.path}:?: ${finding.check}: ${finding.what}"
	} else {
		Finding.line_of(finding)
	}

## The words listed in cookbook/words/<file>, one a line, `--` starting a
## note. A line of two words, or a word listed twice, stops the run.
read_word_set! : Str => Set(Str)
read_word_set! = |file| {
	text = match Shale.read!("cookbook:words/${file}") {
		Found(found) => found
		Missing => {
			Shale.fail!("check_errors: no cookbook/words/${file}")
			""
		}
	}
	var $words = Set.empty()
	for line in Text.lines(text) {
		word = Text.trim(Text.before_first(line, "--"))
		if word.contains(" ") or $words.contains(word) {
			Shale.fail!("check_errors: cookbook/words/${file}: `${word}` is not a single word, or is listed twice")
		}
		if word != "" {
			$words = $words.insert(word)
		}
	}
	$words
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

## After the findings: the files that didn't parse, and those no root of the
## types reaches (judged without them), named, then a stop
## (cookbook/roc/parsed.shale's `fail_if_refused`).
fail_if_refused! : List(Str), List(Str) => {}
fail_if_refused! = |refused, unreached| {
	if !(refused.is_empty()) or !(unreached.is_empty()) {
		for refusal in refused {
			Shale.print!("refused: ${refusal}")
		}
		for missed in unreached {
			Shale.print!(missed)
		}
		var $why = []
		if !(refused.is_empty()) {
			$why = $why.append("roc-ast refused ${refused.len().to_str()} files (named above), which have no tree to judge")
		}
		if !(unreached.is_empty()) {
			$why = $why.append("${unreached.len().to_str()} files (named above) are reached by no root of the types, so were judged without them")
		}
		Shale.fail!("check_errors: ${Str.join_with($why, "; ")}; every other file was judged")
	}
}
