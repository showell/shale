## find_uses_after_a_consume, in Roc on Shale's platform (roc_platform/README.md):
## a value read after a write took it, within one function, which makes the
## write copy the whole list, string or dict. Its launcher,
## cookbook/roc/find_uses_after_a_consume.shale, is how it is run, and its
## header says the five checks; Consume, beside this, is the rule.
##
##   cd <repo> && shale cookbook:roc/find_uses_after_a_consume.shale glob='basic/roc/*.roc'
##   cd <repo> && shale cookbook:roc/find_uses_after_a_consume.shale glob='app/*.roc' types=1 root=app/main.roc
##   shale cookbook:roc/find_uses_after_a_consume.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `types` (`1` asks Roc's type checker,
## for `restored_refcounted`), `root` (the app or apps the types are read
## from) and `self_test` (`self_test=1` checks the plants, the typed ones
## with the types).
##
## The effects: `Shale.arg!` for the arguments, `Shale.glob!` for the files,
## `Shale.roc_tree!` for Roc's own parser's tree of each, Types (roc-types,
## through `Shale.run!`) for the types, and `Shale.print!` and `Shale.fail!`
## for the verdict.
## budget: steps=800000000 arena_bytes=3000000000 output_bytes=4000000 programs=100 roc_cpu_ms=600000
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import Consume
import lib.Finding
import lib.Forest
import lib.RocTypes
import SelfTest
import Types

main! : {} => {}
main! = |{}| {
	self_test = Shale.arg!("self_test", "") == "1"
	plants = SelfTest.plants
	glob = if self_test {
		"${plants}*.roc"
	} else {
		Shale.arg!("glob", "**/*.roc")
	}
	types_asked = Shale.arg!("types", "0")
	if types_asked != "0" and types_asked != "1" {
		Shale.fail!("find_uses_after_a_consume: types=${types_asked} must be 0 (the default) or 1")
	}
	use_types = self_test or types_asked == "1"
	root = if self_test {
		"${plants}typed/StringLists.roc"
	} else {
		Shale.arg!("root", "")
	}
	paths = if self_test {
		Shale.glob!(glob).concat(Shale.glob!("${plants}typed/*.roc"))
	} else {
		Shale.glob!(glob)
	}
	types = if use_types {
		roc = Types.roc_bin!("find_uses_after_a_consume", self_test)
		# The self-test's untyped plants are exempt from being reached by its
		# root.
		exempt = if self_test {
			Set.from_list(Shale.glob!(glob))
		} else {
			Set.empty()
		}
		Types.gather!({ who: "find_uses_after_a_consume", kind: Expressions, root, roc, paths, exempt })
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
	findings = Consume.judge(Forest.join($parsed), types.typing)
	summary = "find_uses_after_a_consume (Roc): ${paths.len().to_str()} files; ${Finding.counts_of(findings, Consume.checks)}"
	print_findings!(summary, findings.map(Finding.line_of))
	fail_if_refused!($refused, types.unreached)
	if self_test {
		SelfTest.run!({ paths, findings })
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

## After the findings: the files that didn't parse, and those no root of the
## types reaches (judged without them), named, then a stop.
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
		Shale.fail!("find_uses_after_a_consume: ${Str.join_with($why, "; ")}; every other file was judged")
	}
}
