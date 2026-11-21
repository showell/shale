## find_dead_code, in Roc on Shale's platform (roc_platform/README.md): the
## functions and generators in Shale's C that no program reaches, from the
## splitter the C recipes share (cookbook/c/items/, a package). Its
## launcher, cookbook/c/find_dead_code.shale, is how it is run, and its
## header says the arguments and the rules. check_goldens.shale, beside
## this, holds its output on the plants to its goldens, line for line.
##
##   shale cookbook:c/find_dead_code.shale dir=c/host
##   shale cookbook:c/find_dead_code.shale self_test=1
##
## The effects: `Shale.arg!` for the arguments, `Shale.glob!` for the files,
## `Shale.read!` for each one's text, and `Shale.print!` and `Shale.fail!`
## for the verdict. Graph, beside this, finds what no program reaches;
## SelfTest holds it to the plants.
## budget: steps=200000000 arena_bytes=2000000000 roc_cpu_ms=600000
app [main!] {
	items: "../items/main.roc",
	pf: platform "../../../roc_platform/platform/main.roc",
}

import pf.Shale
import items.Item
import items.Paths
import items.Split
import items.Words
import Graph
import SelfTest

main! : {} => {}
main! = |{}| {
	if integer_or_stop!("self_test", Shale.arg!("self_test", "0")) == 1 {
		SelfTest.run!(find!)
	} else {
		folder = Shale.arg!("dir", "c")
		found = find!(folder)
		for each in found.findings {
			Shale.print!(Graph.finding_line(each))
		}
		for line in summary(folder, found) {
			Shale.print!(line)
		}
	}
}

## A value given as `name=`, which must be an integer, as Shale's `arg` reads
## one whose default is.
integer_or_stop! : Str, Str => I64
integer_or_stop! = |name, given|
	match I64.from_str(given) {
		Ok(value) => value
		Err(_) => {
			Shale.fail!("find_dead_code: ${name}=${given} is not an integer")
			0
		}
	}

## What was found under a folder: the findings, and how many items were
## nodes.
Found : { findings : List(Graph.Finding), nodes : U64 }

## The findings over the folder.
find! : Str => Found
find! = |folder| {
	nodes = read_nodes!(folder)
	{ findings: Graph.findings(nodes), nodes: nodes.len() }
}

## The summary: the counts by rule, then by folder and rule.
summary : Str, Found -> List(Str)
summary = |folder, found| {
	rules = found.findings.fold(Dict.empty(), |counts, each| counted(counts, each.rule))
	folders = found.findings.fold(Dict.empty(), |counts, each| counted(counts, "${folder_of(each.path)} ${each.rule}"))
	parts = sorted_counts(rules).map(|(rule, count)| "${count.to_str()} ${rule}")
	counts = "${found.findings.len().to_str()} findings: ${Str.join_with(parts, ", ")}"
	head = "find_dead_code: ${found.nodes.to_str()} items under ${folder}; ${counts}"
	sorted_counts(folders).map(|(key, count)| "  ${key}: ${count.to_str()}").prepend(head)
}

## The counts with one more of `key`.
counted : Dict(Str, U64), Str -> Dict(Str, U64)
counted = |counts, key| counts.insert(key, (counts.get(key) ?? 0) + 1)

## The counts, by key in the order of their bytes.
sorted_counts : Dict(Str, U64) -> List((Str, U64))
sorted_counts = |counts| counts.to_list().sort_with(|(x, _), (y, _)| Words.byte_order(x, y))

## The folder a path is in, as Shale's `dirname` gives it: up to its last
## `/`, or "" when it has none.
folder_of : Str -> Str
folder_of = |path| {
	bytes = path.to_utf8()
	match bytes.find_last_index(|byte| byte == '/') {
		Ok(slash) => Str.from_utf8_lossy(bytes.take_first(slash))
		Err(_) => ""
	}
}

## The kinds of item that are nodes: each names others, or is named. A type
## is one, since a generator's type may be named only in a struct's field.
node_kinds : List(Item.Kind)
node_kinds = [Function, Test, TestList, Data, Macro, Generator, Struct, Union, Enum, Typedef]

## The kinds whose words are the whole item's text, not a body's uses.
whole_kinds : List(Item.Kind)
whole_kinds = [Data, Macro, Generator, Struct, Union, Enum, Typedef]

## The tree's nodes, one for each item of a node's kind, in file order.
read_nodes! : Str => List(Graph.Node)
read_nodes! = |folder| {
	var $nodes = []
	for path in c_files_under!(folder) {
		text = read_text!(path)
		lines = Split.lines(text)
		test = Paths.is_test_path(folder, path)
		for item in split_or_stop!(path, text) {
			if node_kinds.contains(item.kind) {
				words =
					if whole_kinds.contains(item.kind) {
						Words.uses(Split.masked_text(lines, { first: item.first, last: item.last }))
					} else {
						item.uses
					}
				node = { path, line: item.first, kind: item.kind, name: item.name, test, words, macro: item.macro, makes: item.makes }
				$nodes = $nodes.append(node)
			}
		}
	}
	$nodes
}

## The file's text. A file the glob named and the read finds gone stops the
## run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("find_dead_code: ${path} went missing during the walk")
			""
		}
	}

## The file's items, each body's uses read, or the run stopped with the line
## the dialect does not account for.
split_or_stop! : Str, Str => List(Item)
split_or_stop! = |path, text|
	match Split.items({ path, text, uses: Bool.True }) {
		Ok(items) => items
		Err(why) => {
			Shale.fail!(why)
			[]
		}
	}

## The C files under the folder, sorted, any folder named `out` (c/out, the
## build's output) left out. A folder that is not there stops the run, in
## the words of the Shale original's walk: a check that can't run fails.
c_files_under! : Str => List(Str)
c_files_under! = |folder| {
	if Shale.list!(folder) == Missing {
		Shale.fail!("`walk(\"${folder}\")`: not found (FileNotFound)")
	}
	paths = Shale.glob!("${folder}/**/*.c").concat(Shale.glob!("${folder}/**/*.h"))
	paths.keep_if(|path| !(Paths.is_in_out_folder(folder, path))).sort_with(Words.byte_order)
}
