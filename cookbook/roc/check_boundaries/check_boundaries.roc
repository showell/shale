## check_boundaries, in Roc on Shale's platform (roc_platform/README.md):
## whether each function's boundary earns its keep, and whether each of its
## parameters is really a parameter. Its launcher,
## cookbook/roc/check_boundaries.shale, is how it is run, and its header says
## the checks (`single`, `fixed`, `entry`, `constant`, `dead`, `unused_api`,
## `same_branches`, and `bool_param` under the types) and the arguments.
## check_goldens.shale, beside this, holds its output to its goldens, line
## for line.
##
##   cd <repo> && shale cookbook:roc/check_boundaries.shale glob='app/**/*.roc'
##   shale cookbook:roc/check_boundaries.shale self_test=1
##
## This root reads the arguments (`Shale.arg!`), globs the files
## (`Shale.glob!`), gathers the types (Types, through `Shale.run!`), has Roc's
## parser read the files (`Shale.roc_tree!`) and each package's main.roc
## (`Shale.read!`), and prints the verdict (`Shale.print!`, `Shale.fail!`).
## Its modules are beside it: the functions (Functions), what reaches them
## (References), the source texts compared (Source), the checks (Checks),
## Types and SelfTest; Forest, Tree, Text, Headers, Bytes, Finding and RocTypes
## are the cookbook's Roc library, cookbook/roc/lib/.
## budget: steps=400000000 arena_bytes=3000000000 output_bytes=8000000 programs=100 roc_cpu_ms=60000
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import Checks
import lib.Finding
import lib.Forest
import Functions
import lib.Headers
import References
import lib.RocTypes
import SelfTest
import lib.Text
import lib.Tree
import Types

main! : {} => {}
main! = |{}| {
	asked = read_asked!({})
	client_files = globbed!(asked.clients)
	client_set = Set.from_list(globbed!(asked.clients))
	checked_files = globbed!(asked.glob).drop_if(|path| client_set.contains(path))
	checked = Set.from_list(checked_files)
	test_paths = Set.from_list(globbed!(asked.tests))
	types = if asked.use_types {
		roc = Types.roc_bin!("check_boundaries", asked.self_test)
		exempt = if asked.self_test {
			Set.from_list(checked_files.drop_if(|path| path.starts_with(SelfTest.typed_plants)))
		} else {
			Set.empty()
		}
		Types.gather!({ who: "check_boundaries", kind: Patterns, root: asked.root, roc, paths: checked_files, exempt })
	} else {
		{ typing: RocTypes.no_types, unreached: [] }
	}
	read = read_files!(checked_files.concat(client_files))
	forest = Forest.join(read.parsed)
	ancestry = Functions.ancestry(forest)
	limits = { ways: asked.ways, nodes: asked.nodes }
	defined = Functions.read(forest, ancestry, { checked, lines_of: read.lines_of, limits })
	tallies = References.tally(forest, ancestry, defined, test_paths)
	clients_read = if client_files.is_empty() {
		""
	} else {
		" and ${client_files.len().to_str()} clients"
	}
	judging = {
		forest,
		lines_of: read.lines_of,
		defined,
		tallies,
		checked,
		headers: read.headers,
		exposed_modules: exposed_modules!(checked_files),
		typing: types.typing,
		files_read: " in the ${checked_files.len().to_str()} files read${clients_read}",
		is_dead_by_production: asked.is_dead_by_production,
	}
	is_house_off = read_house_is_off!({})
	is_reported = |finding| {
		is_house = is_house_off and finding.check == "single"
		is_unjudged_plant = asked.self_test and (finding.check == "dead" or finding.check == "unused_api") and !(finding.path.starts_with(SelfTest.dead_plants))
		!(is_house) and !(is_unjudged_plant)
	}
	findings = Checks.judge(judging).keep_if(is_reported)
	for finding in findings {
		Shale.print!(Finding.line_of(finding))
	}
	house_note = if is_house_off {
		"; left out as house style: single"
	} else {
		""
	}
	files_said = "${checked_files.len().to_str()} files${clients_read}, ${defined.by_key.len().to_str()} functions"
	Shale.print!("check_boundaries (Roc): ${files_said}; ${Finding.counts_of(findings, checks)}${house_note}")
	fail_if_refused!(read.refused, types.unreached)
	if asked.self_test {
		SelfTest.run!({ paths: checked_files, clients: client_files, test_count: test_paths.len(), findings }, checks)
	}
}

## The checks, in the order the summary counts them.
checks : List(Str)
checks = ["single", "fixed", "entry", "constant", "bool_param", "dead", "unused_api", "same_branches"]

## The arguments, read and refused in the Shale original's order. The
## self-test reads every plant, those under `dead/clients/` as clients and
## those under `dead/gates/` as tests, with the types under
## `typed/QuietApp.roc` and `test_reach=dead`.
read_asked! : {} => {
	self_test : Bool,
	glob : Str,
	ways : I64,
	nodes : I64,
	use_types : Bool,
	root : Str,
	clients : Str,
	tests : Str,
	is_dead_by_production : Bool,
}
read_asked! = |{}| {
	self_test = Shale.arg!("self_test", "") == "1"
	plants = SelfTest.plants
	glob_asked = Shale.arg!("glob", "**/*.roc")
	ways = integer_of!("ways", Shale.arg!("ways", "2"))
	nodes = integer_of!("nodes", Shale.arg!("nodes", "24"))
	types_asked = Shale.arg!("types", "0")
	if types_asked != "0" and types_asked != "1" {
		Shale.fail!("check_boundaries: types must be 0 (the default) or 1, not ${types_asked}")
	}
	root_asked = Shale.arg!("root", "")
	clients_asked = Shale.arg!("clients", "")
	tests_asked = Shale.arg!("tests", "")
	reach = Shale.arg!("test_reach", "keeps")
	if reach != "keeps" and reach != "dead" {
		Shale.fail!("check_boundaries: test_reach must be keeps (the default) or dead, not ${reach}")
	}
	if self_test {
		{
			self_test,
			glob: "${plants}**/*.roc",
			ways,
			nodes,
			use_types: Bool.True,
			root: "${SelfTest.typed_plants}QuietApp.roc",
			clients: "${SelfTest.dead_plants}clients/*.roc",
			tests: "${SelfTest.dead_plants}gates/*.roc",
			is_dead_by_production: Bool.True,
		}
	} else {
		{
			self_test,
			glob: glob_asked,
			ways,
			nodes,
			use_types: types_asked == "1",
			root: root_asked,
			clients: clients_asked,
			tests: tests_asked,
			is_dead_by_production: reach == "dead",
		}
	}
}

## The argument `name`, given as `given`, read as an integer, as Shale's
## `arg` reads one whose default is an integer. Each is read by its own
## `Shale.arg!`, which the launcher finds by name.
integer_of! : Str, Str => I64
integer_of! = |name, given|
	match I64.from_str(given) {
		Ok(number) => number
		Err(_) => {
			Shale.fail!("`arg`: ${name}=${given} is not an integer, and its default is one")
			0
		}
	}

## Whether `house=0` leaves the house style, `single`, out
## (cookbook/roc/house.shale).
read_house_is_off! : {} => Bool
read_house_is_off! = |{}| {
	house = integer_of!("house", Shale.arg!("house", "1"))
	if house != 0 and house != 1 {
		Shale.fail!("house must be 1 (the default) or 0, not ${house.to_str()}")
	}
	house == 0
}

## The files the globs match, a list separated by commas, each file as
## often as a glob matches it.
globbed! : Str => List(Str)
globbed! = |globs| {
	var $paths = []
	for named in globs.split_on(",") {
		pattern = Text.trim(named)
		if pattern != "" {
			$paths = $paths.concat(Shale.glob!(pattern))
		}
	}
	$paths
}

## The files Roc's parser reads, with each file's lines and header names,
## and a `path: reason` line for each it refuses.
read_files! : List(Str) => {
	parsed : List({ path : Str, nodes : List(Tree.Node) }),
	lines_of : Dict(Str, List(Str)),
	headers : Dict(Str, { dictated : Set(Str), exposed : Set(Str) }),
	refused : List(Str),
}
read_files! = |paths| {
	var $parsed = []
	var $lines_of = Dict.empty()
	var $headers = Dict.empty()
	var $refused = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			text = match Shale.read!(file.path) {
				Found(found) => found
				Missing => {
					Shale.fail!("check_boundaries: ${file.path} went missing while it was read")
					""
				}
			}
			names = Headers.header_names(file.nodes)
			$parsed = $parsed.append({ path: file.path, nodes: file.nodes })
			$lines_of = $lines_of.insert(file.path, text.split_on("\n"))
			$headers = $headers.insert(file.path, { dictated: Set.from_list(names.dictated), exposed: Set.from_list(names.exposed) })
		} else if file.err.starts_with(file.path) {
			$refused = $refused.append(file.err)
		} else {
			$refused = $refused.append("${file.path}: ${file.err}")
		}
	}
	{ parsed: $parsed, lines_of: $lines_of, headers: $headers, refused: $refused }
}

## The modules each checked file's folder's main.roc exposes, when it is a
## package's or a platform's (cookbook/roc/lib/'s `Headers.exposed_by`), by
## folder.
exposed_modules! : List(Str) => Dict(Str, List(Str))
exposed_modules! = |paths| {
	var $by_folder = Dict.empty()
	for path in paths {
		folder = Text.dirname(path)
		if !($by_folder.contains(folder)) {
			main = if folder == "" or folder == "." {
				"main.roc"
			} else {
				"${folder}/main.roc"
			}
			exposed = match Shale.read!(main) {
				Found(text) => Headers.exposed_by(text).modules
				Missing => []
			}
			$by_folder = $by_folder.insert(folder, exposed)
		}
	}
	$by_folder
}

## After the summary: the files that didn't parse, and those no root of the
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
		Shale.fail!("check_boundaries: ${Str.join_with($why, "; ")}; every other file was judged")
	}
}
