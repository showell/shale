## hunt_names, in Roc on Shale's platform (roc_platform/README.md): awkward
## names in every .roc file of a tree, from Roc's own parser's trees. Its
## launcher, cookbook/roc/hunt_names.shale, is how it is run, and its header
## says the checks, from `state_effect` to `stale_doc_name`.
## check_goldens.shale, beside this, holds its output on the plants to its
## goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/hunt_names.shale glob=app/**/*.roc
##   shale cookbook:roc/hunt_names.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `words` (`words=1` prints the census
## of effects' first words instead), `house` (`house=0` leaves out the house
## style), `types` (`1` asks Roc's type checker), `root` (the app or apps the
## types are read from), `terms` (a project's terms) and `self_test`
## (`self_test=1` checks the plants, the typed ones with the types).
##
## This root reads the word lists (Words), globs the files, gathers the
## types (Types), has Roc's parser read the files, reads each file's
## definitions (Reading), judges them (Judge, with ListNames and Makers),
## then the bindings (ShortNames), the docs (DocNames) and what is judged
## over every file (Passes), and prints the verdict; SelfTest judges the
## plants. Tree, Text, Finding and RocTypes are the cookbook's Roc library,
## cookbook/roc/lib/.
## budget: steps=800000000 arena_bytes=3000000000 output_bytes=4000000 roc_cpu_ms=600000 programs=100
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import lib.Finding
import lib.RocTypes
import lib.Text
import lib.Tree
import DocNames
import Facts
import Judge
import ListNames
import Passes
import Reading
import SelfTest
import ShortNames
import Types
import Words

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
		Shale.fail!("hunt_names: types=${types_asked}: 0 (the default) or 1")
	}
	# The self-test checks its typed plants with the types, and the rest
	# without.
	use_types = self_test or types_asked == "1"
	root = if self_test {
		"${plants}typed/QuietApp.roc"
	} else {
		Shale.arg!("root", "")
	}
	is_census = Shale.arg!("words", "") == "1"
	terms_file = if self_test {
		"${plants}terms.txt"
	} else {
		Shale.arg!("terms", "")
	}
	lists = Words.read!(terms_file)
	house_style = Words.house_style!({})
	paths = if self_test {
		Shale.glob!(glob).concat(Shale.glob!("${plants}typed/*.roc"))
	} else {
		Shale.glob!(glob)
	}
	types = if use_types {
		roc = Types.roc_bin!("hunt_names", self_test)
		# The self-test's untyped plants are imported by no root.
		exempt = if self_test {
			Set.from_list(Shale.glob!("${plants}*.roc"))
		} else {
			Set.empty()
		}
		Types.gather!({ who: "hunt_names", kind: Patterns, root, roc, paths, exempt })
	} else {
		{ typing: RocTypes.no_types, unreached: [] }
	}
	parsed = parse!(paths)
	read = definitions_of(parsed.files, types.typing)
	found = judged!({ files: parsed.files, read, lists, self_test })
	house = if !(found.is_empty()) or !(is_census) {
		read_house!({})
	} else {
		Kept
	}
	is_kept = |finding|
		match house {
			Kept => Bool.True
			LeftOut => !(house_style.contains(finding.check))
		}
	reported = found.keep_if(is_kept)
	finding_lines = if is_census {
		[]
	} else {
		reported.map(Finding.line_of)
	}
	file_count = paths.len().to_str()
	if is_census {
		census = Passes.census_lines(Passes.census(read.definitions, lists))
		print_findings!("hunt_names (Roc) census: ${file_count} files; ${census.summary}", census.lines)
	} else {
		house_note = match house {
			Kept => ""
			LeftOut => "; left out as house style: ${Str.join_with(house_style, ", ")}"
		}
		print_findings!("hunt_names (Roc): ${file_count} files; ${Finding.counts_of(reported, checks)}${house_note}", finding_lines)
	}
	fail_if_refused!(parsed.refused, types.unreached)
	if self_test {
		census = Passes.census(read.definitions, lists)
		SelfTest.run!({ paths, findings: reported, census, finding_lines }, checks)
	}
}

## The checks, in the order the summary counts them.
checks : List(Str)
checks = ["state_effect", "question", "vague", "name_type", "negative", "noun_effect", "command_tense", "mixed_tense", "container", "preposition_split", "short_name", "worker_name", "stacked_preposition", "taken_word", "bare_participle", "phrase_name", "singular_list", "world_verb", "where_name", "stale_doc_name"]

## The files Roc's parser read, each with its text cut at each newline, and
## a `path: why` line for each it refused.
parse! : List(Str) => { files : List(Reading.FileRead), refused : List(Str) }
parse! = |paths| {
	var $files = []
	var $refused = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			source = match Shale.read!(file.path) {
				Found(text) => text
				Missing => ""
			}
			$files = $files.append({ path: file.path, nodes: file.nodes, source_lines: source.split_on("\n") })
		} else if file.err.starts_with(file.path) {
			$refused = $refused.append(file.err)
		} else {
			$refused = $refused.append("${file.path}: ${file.err}")
		}
	}
	{ files: $files, refused: $refused }
}

## What the run asks of the files read: the files, their definitions and
## the facts their reading gathered, the word lists, and whether it is the
## self-test.
Asked : {
	files : List(Reading.FileRead),
	read : { definitions : List(Reading.Definition), facts : Facts.Gathered },
	lists : Words.Lists,
	self_test : Bool,
}

## Every file's definitions, in file order, and the facts their reading
## gathers.
definitions_of : List(Reading.FileRead), RocTypes.Typing -> { definitions : List(Reading.Definition), facts : Facts.Gathered }
definitions_of = |files, typing|
	files.fold(
		{ definitions: [], facts: Facts.none },
		|so_far, file| {
			pattern_types = pattern_types_of(typing, file.path, file.nodes)
			read = Reading.file_definitions(file, pattern_types)
			{ definitions: so_far.definitions.concat(read.definitions), facts: Facts.joined(so_far.facts, read.facts) }
		},
	)

## The checker's type of each plain-name pattern of the file at `path`, by
## its node's place: none when no root reached the file.
pattern_types_of : RocTypes.Typing, Str, List(Tree.Node) -> Dict(U64, Str)
pattern_types_of = |typing, path, nodes| {
	if !(typing.real.contains(path)) {
		return Dict.empty()
	}
	var $typed = Dict.empty()
	$place : U64
	var $place = 0
	for node in nodes {
		$place = $place + 1
		if node.kind == "p-ident" {
			match RocTypes.type_at(typing, path, node) {
				Ok(written) => {
					$typed = $typed.insert($place, written)
				}
				Err(NoType) => {}
			}
		}
	}
	$typed
}

## Every finding, in the Shale original's order: each definition's, each
## file's `where` clauses and bindings, each file's docs, the type and
## field names, and the preposition splits.
judged! : Asked => List(Finding)
judged! = |asked| {
	lists = asked.lists
	read = asked.read
	facts = read.facts
	by_definition = read.definitions.map(|definition| Judge.findings(definition, lists, facts)).join()
	by_file = asked.files
		.map(|file| Passes.where_name_findings(file.path, file.nodes, lists.fixed_names).concat(ShortNames.findings(file.path, file.nodes)))
		.join()
	sources = Words.word_set!("cookbook:words/cited_sources.txt")
	# Each plant opens by naming its check, so in the self-test the check
	# names, and this recipe's, count as names.
	known = if asked.self_test {
		Set.from_list(checks).insert("hunt_names")
	} else {
		Set.empty()
	}
	doc_files = asked.files.map(|file| { path: file.path, nodes: file.nodes, source_lines: file.source_lines })
	by_doc = DocNames.findings!(doc_files, DocNames.seen_of(doc_files, known, sources))
	by_name = facts.names
		.map(
			|named| {
				subject = { path: named.path, line: named.line, name: named.name, kind: "field", is_function: named.is_function, first: named.first, gives: named.gives }
				listed = if named.is_field {
					ListNames.findings(subject, lists)
				} else {
					[]
				}
				Passes.container_findings(named, lists).concat(listed)
			},
		)
		.join()
	splits = Passes.preposition_splits(read.definitions, lists, facts.keywords)
	by_definition.concat(by_file).concat(by_doc).concat(by_name).concat(splits)
}

## Whether `house=0` leaves the house style out (cookbook/roc/house.shale's
## `house_is_off`): `house` is read as an integer, 1 unless given.
read_house! : {} => [Kept, LeftOut]
read_house! = |{}| {
	given = Shale.arg!("house", "1")
	match integer_of(given) {
		Ok(0) => LeftOut
		Ok(1) => Kept
		Ok(other) => {
			Shale.fail!("house must be 1 (the default) or 0, not ${other.to_str()}")
			Kept
		}
		Err(NotAnInteger) => {
			Shale.fail!("`arg`: house=${given} is not an integer, and its default is one")
			Kept
		}
	}
}

## A launch value read as an integer, as Shale's `arg` reads one whose
## default is: digits, a `-` before them or not.
integer_of : Str -> Try(I64, [NotAnInteger])
integer_of = |text| {
	bytes = text.to_utf8()
	digits = match bytes {
		['-', .. as rest] => rest
		_ => bytes
	}
	if digits.is_empty() or !(digits.all(Text.is_digit)) {
		return Err(NotAnInteger)
	}
	value = digits.fold(0, |sum, digit| sum * 10 + (digit - '0').to_i64())
	if bytes.first() == Ok('-') {
		Ok(-value)
	} else {
		Ok(value)
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
		Shale.fail!("hunt_names: ${Str.join_with($why, "; ")}; every other file was judged")
	}
}
