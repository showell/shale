## map_file, in Roc on Shale's platform (roc_platform/README.md): a file of
## Shale's C mapped, each top-level item one line, `first-last kind name`,
## from the splitter the C recipes share (cookbook/c/items/, a package),
## which reads the dialect c/check_discipline.shale holds. Its launcher,
## cookbook/c/map_file.shale, is how it is run, and its header says the
## arguments. check_goldens.shale, beside this, holds its output on the
## plants to its goldens, line for line.
##
##   shale cookbook:c/map_file.shale file=c/script/vm.c
##   shale cookbook:c/map_file.shale tree=c
##   shale cookbook:c/map_file.shale self_test=1
##
## The effects: `Shale.arg!` for the arguments, `Shale.glob!` for a tree's
## files, `Shale.read!` for each one's text, and `Shale.print!` and
## `Shale.fail!` for the verdict. The splitting is the package's, plain
## Roc; SelfTest, beside this, holds it to the plants.
## budget: steps=50000000 arena_bytes=1000000000 roc_cpu_ms=600000
app [main!] {
	items: "../items/main.roc",
	pf: platform "../../../roc_platform/platform/main.roc",
}

import pf.Shale
import items.Item
import items.Split
import items.Words
import SelfTest

main! : {} => {}
main! = |{}| {
	if integer_or_stop!("self_test", Shale.arg!("self_test", "0")) == 1 {
		SelfTest.run!({})
	} else {
		file = Shale.arg!("file", "")
		tree = Shale.arg!("tree", "")
		if (file == "") == (tree == "") {
			Shale.fail!("map_file: give `file=<a .c or .h>` or `tree=<a folder>`, one of them")
		} else if file != "" {
			for line in map_lines(split_or_stop!(file, read_text!(file))) {
				Shale.print!(line)
			}
		} else {
			map_tree!(tree)
		}
	}
}

## A value given as `name=`, which must be an integer, as Shale's `arg` reads
## one whose default is.
integer_or_stop! : Str, Str => I64
integer_or_stop! = |name, given| {
	match I64.from_str(given) {
		Ok(value) => value
		Err(_) => {
			Shale.fail!("map_file: ${name}=${given} is not an integer")
			0
		}
	}
}

## The map of the items, one `first-last kind name` a line.
map_lines : List(Item) -> List(Str)
map_lines = |items|
	items.map(|item| "${item.first.to_str()}-${item.last.to_str()} ${Item.kind_name(item.kind)} ${item.name}")

## The file's text. A file that is not there stops the run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("map_file: ${path} is not there")
			""
		}
	}

## The file's items, or the run stopped with the line the dialect does not
## account for.
split_or_stop! : Str, Str => List(Item)
split_or_stop! = |path, text|
	match Split.items({ path, text, uses: Bool.False }) {
		Ok(items) => items
		Err(why) => {
			Shale.fail!(why)
			[]
		}
	}

## The C files under the folder, sorted, any folder named `out` (c/out, the
## build's output) left out.
c_files_under! : Str => List(Str)
c_files_under! = |folder| {
	paths = Shale.glob!("${folder}/**/*.c").concat(Shale.glob!("${folder}/**/*.h"))
	paths.keep_if(|path| !(is_in_out_folder(folder, path))).sort_with(Words.byte_order)
}

## Whether a folder below `folder` on the way to `path` is named `out`.
is_in_out_folder : Str, Str -> Bool
is_in_out_folder = |folder, path|
	path.drop_prefix("${folder}/").split_on("/").drop_last(1).contains("out")

## Every C file under the folder split and accounted for, in one line: the
## files, their lines and their items by kind.
map_tree! : Str => {}
map_tree! = |folder| {
	files = c_files_under!(folder)
	var $kinds = []
	$line_count : U64
	var $line_count = 0
	$item_count : U64
	var $item_count = 0
	for path in files {
		text = read_text!(path)
		items = split_or_stop!(path, text)
		match accounted(path, Split.lines(text), items) {
			Ok(count) => {
				$line_count = $line_count + count
			}
			Err(why) => Shale.fail!(why)
		}
		$kinds = $kinds.concat(items.map(|item| Item.kind_name(item.kind)))
		$item_count = $item_count + items.len()
	}
	names = distinct($kinds).sort_with(Words.byte_order)
	parts = names.map(|name| "${$kinds.count_if(|kind| kind == name).to_str()} ${name}")
	lines = "${$line_count.to_str()} lines, each in one item or between them"
	counts = "${$item_count.to_str()} items: ${Str.join_with(parts, ", ")}"
	Shale.print!("PASS c items: ${files.len().to_str()} files under ${folder}, ${lines}; ${counts}")
}

## Each line of the file in exactly one of the items, or blank, or a `//`
## comment line between them: the count of its lines, or the error naming
## the line that is not.
accounted : Str, List(List(U8)), List(Item) -> Try(U64, Str)
accounted = |path, lines, items| {
	# Each line's owner, by its number (place 0 is no line's).
	var $owners = List.repeat(Unowned, lines.len() + 1)
	for item in items {
		var $at = item.first
		while $at <= item.last {
			match $owners.get($at) {
				Ok(Owned(owner)) => return Err("${path}:${$at.to_str()}: in two items, `${owner}` and `${item.name}`")
				_ => {}
			}
			$owners = match $owners.set($at, Owned(item.name)) {
				Ok(owners) => owners
				Err(_) => crash "map_file: an item past its file's last line"
			}
			$at = $at + 1
		}
	}
	$number : U64
	var $number = 0
	for raw in lines {
		$number = $number + 1
		is_owned = $owners.get($number) != Ok(Unowned)
		if !(is_owned) and !(raw.is_empty()) and !(raw.starts_with(['/', '/'])) {
			return Err("${path}:${$number.to_str()}: in no item: ${Str.from_utf8_lossy(raw)}")
		}
	}
	Ok($number)
}

## Each text once, in the order first seen.
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
