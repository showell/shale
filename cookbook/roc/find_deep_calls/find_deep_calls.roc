## find_deep_calls, in Roc on Shale's platform (roc_platform/README.md):
## calls nested three or more deep, like `f(g(h(x)))`, which have to be read
## from the inside out, where Roc's style guide (roc-apps docs/roc-style.md)
## asks for code that reads left to right: a method chain, a pipe or a named
## middle step. Its launcher, cookbook/roc/find_deep_calls.shale, is how it
## is run, and its header says the rule. check_goldens.shale, beside this,
## holds its output on the plants to its goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/find_deep_calls.shale glob=app/**/*.roc min=4
##   shale cookbook:roc/find_deep_calls.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `min` (3), `loose` (`loose=1` passes a
## record's, list's or tuple's depth through), `house` (`house=0` leaves out
## the house style, which here is the whole check) and `self_test`
## (`self_test=1` checks the plants' marks).
##
## The effects: `Shale.arg!` for the arguments, `Shale.glob!` for the files,
## `Shale.roc_tree!` for Roc's own parser's tree of each, `Shale.read!` for
## a finding's source line, and `Shale.print!` and `Shale.fail!` for the
## verdict. Nest, beside this, is the rule, over Tree; Finding is how a
## finding prints; SelfTest checks the plants.
## budget: steps=800000000 arena_bytes=3000000000 output_bytes=4000000 roc_cpu_ms=600000
app [main!] { pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import Finding
import Nest
import SelfTest
import Text
import Tree

## What a run checks: the files a glob names, and the rule, or the plants
## at the self-test's own `min`.
Settings : {
	pattern : Str,
	rule : Nest.Rule,
	house : [Kept, LeftOut],
}

main! : {} => {}
main! = |{}| {
	is_self_test = Shale.arg!("self_test", "") == "1"
	settings = read_settings!(is_self_test)
	checked = run_check!(settings)
	if is_self_test {
		SelfTest.run!(checked)
	}
}

# ── The arguments

## The settings the arguments give. `min` is read before the files are
## globbed and `house` after, as the Shale original read them, so a run
## given two bad values stops on the same one.
read_settings! : Bool => Settings
read_settings! = |is_self_test| {
	pattern = if is_self_test {
		"${SelfTest.plants}*.roc"
	} else {
		Shale.arg!("glob", "**/*.roc")
	}
	strictness = if Shale.arg!("loose", "") == "1" {
		Loose
	} else {
		Strict
	}
	min = if is_self_test {
		3
	} else {
		read_min!({})
	}
	{
		pattern,
		rule: { min, strictness },
		house: Kept,
	}
}

## The `min=` depth: 3 unless given, and any integer.
read_min! : {} => I64
read_min! = |{}| {
	given = Shale.arg!("min", "3")
	match I64.from_str(given) {
		Ok(min) => min
		Err(_) => {
			Shale.fail!("find_deep_calls: min=${given} is not an integer")
			0
		}
	}
}

## Whether `house=0` leaves the house style out (cookbook/roc/house.shale):
## here that is every finding, since `nested` is house style.
read_house! : {} => [Kept, LeftOut]
read_house! = |{}| {
	house = Shale.arg!("house", "1")
	if house != "0" and house != "1" {
		Shale.fail!("house must be 1 (the default) or 0, not ${house}")
	}
	if house == "0" {
		LeftOut
	} else {
		Kept
	}
}

# ── The run

## The check over the glob's files: the verdict printed, then a stop when a
## file didn't parse. The findings are a report, not a failure, so a run
## that parses every file exits 0; a caller holds the count.
run_check! : Settings => { paths : List(Str), findings : List(Finding) }
run_check! = |given| {
	paths = Shale.glob!(given.pattern)
	settings = { ..given, house: read_house!({}) }
	var $findings = []
	var $refused = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			parsed = { path: file.path, syntax_nodes: file.nodes }
			$findings = $findings.concat(find_file_findings!(parsed, settings))
		} else if file.err.starts_with(file.path) {
			$refused = $refused.append(file.err)
		} else {
			$refused = $refused.append("${file.path}: ${file.err}")
		}
	}
	summary_line = summary($findings, { file_count: paths.len(), settings })
	print_findings!(summary_line, $findings.map(Finding.finding_line))
	if !($refused.is_empty()) {
		for refusal in $refused {
			Shale.print!("refused: ${refusal}")
		}
		refusals = "roc-ast refused ${$refused.len().to_str()} files (named above)"
		why = "which have no tree to judge; every other file was judged"
		Shale.fail!("find_deep_calls: ${refusals}, ${why}")
	}
	{ paths, findings: $findings }
}

## A parsed file's findings. Its text is read only when it has a nest, for
## the source lines; a file the parser read and the read finds gone stops
## the run.
find_file_findings! : { path : Str, syntax_nodes : List(Tree.SyntaxNode) }, Settings => List(Finding)
find_file_findings! = |file, settings| {
	nests = match settings.house {
		Kept => Nest.nests_in(file.syntax_nodes, settings.rule)
		LeftOut => []
	}
	if nests.is_empty() {
		[]
	} else {
		source_lines = match Shale.read!(file.path) {
			Found(text) => text.split_on("\n")
			Missing => {
				Shale.fail!("find_deep_calls: ${file.path} went missing during the check")
				[]
			}
		}
		nests.map(
			|nest| {
				source_line = match source_lines.get(nest.line - 1) {
					Ok(text) => text
					Err(_) => crash "find_deep_calls: a nest starts past its file's last line"
				}
				{ path: file.path, nest, source_line }
			},
		)
	}
}

# ── The verdict

## `find_deep_calls (Roc): 9 files; nested 14 (min 3, strict); depth 3 11,
## depth 4 3`: the files, the findings, the rule, and how many at each depth,
## the depths in the order of their text, as the Shale original sorted them.
summary : List(Finding), { file_count : U64, settings : Settings } -> Str
summary = |findings, { file_count, settings }| {
	rule = settings.rule
	strictness = match rule.strictness {
		Strict => "strict"
		Loose => "loose"
	}
	counted = "nested ${findings.len().to_str()} (min ${rule.min.to_str()}, ${strictness})"
	depths = findings.map(|finding| finding.nest.depth.to_str())
	by_depth = distinct(depths).sort_with(Text.byte_order).map(
		|depth| "depth ${depth} ${depths.count_if(|d| d == depth).to_str()}",
	)
	counts = if by_depth.is_empty() {
		""
	} else {
		"; ${Str.join_with(by_depth, ", ")}"
	}
	house_note = match settings.house {
		Kept => ""
		LeftOut => "; left out as house style: nested"
	}
	"find_deep_calls (Roc): ${file_count.to_str()} files; ${counted}${counts}${house_note}"
}

distinct : List(Str) -> List(Str)
distinct = |texts|
	texts.fold(
		[],
		|seen, text|
			if seen.contains(text) {
				seen
			} else {
				seen.append(text)
			},
	)

## The summary, each finding, then the summary again (cookbook/findings.shale):
## the first copy survives a cut output, and a gate reads the last line.
print_findings! : Str, List(Str) => {}
print_findings! = |summary_line, finding_lines| {
	Shale.print!(summary_line)
	if !(finding_lines.is_empty()) {
		for line in finding_lines {
			Shale.print!(line)
		}
		Shale.print!(summary_line)
	}
}
