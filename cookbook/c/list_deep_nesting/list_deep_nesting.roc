## list_deep_nesting, in Roc on Shale's platform (roc_platform/README.md):
## Shale's C functions by how deeply their blocks nest, deepest first, from
## the splitter the C recipes share (cookbook/c/items/, a package). The
## kernel's style guide calls more than 3 levels of indentation a smell, so
## the line is depth 4 unless `min=` moves it. Its launcher,
## cookbook/c/list_deep_nesting.shale, is how it is run, and its header says
## the arguments and how depth is counted. check_goldens.shale, beside this,
## holds its output on the plants to its goldens, line for line.
##
##   shale cookbook:c/list_deep_nesting.shale min=5
##   shale cookbook:c/list_deep_nesting.shale self_test=1
##
## The effects: `Shale.arg!` for the arguments, `Shale.glob!` for the files,
## `Shale.read!` for each one's text, and `Shale.print!` and `Shale.fail!`
## for the verdict. Depth, beside this, measures and lists; SelfTest holds
## it to the plants.
## budget: steps=200000000 arena_bytes=1000000000 roc_cpu_ms=600000
app [main!] {
	items: "../items/main.roc",
	pf: platform "../../../roc_platform/platform/main.roc",
}

import pf.Shale
import items.Item
import items.Paths
import items.Split
import items.Words
import Depth
import SelfTest

main! : {} => {}
main! = |{}| {
	if integer_or_stop!("self_test", Shale.arg!("self_test", "0")) == 1 {
		SelfTest.run!(measure!)
	} else {
		folder = Shale.arg!("dir", "c")
		tests = integer_or_stop!("tests", Shale.arg!("tests", "0")) == 1
		min = integer_or_stop!("min", Shale.arg!("min", "4"))
		listing = match Depth.listing(measure!(folder, tests), min) {
			Ok(listed) => listed
			Err(why) => {
				Shale.fail!(why)
				{ summary: "", lines: [] }
			}
		}
		for line in listing.lines {
			Shale.print!(line)
		}
		which = if tests {
			"tests included"
		} else {
			"tests left out"
		}
		Shale.print!("list_deep_nesting: ${folder}, ${which}: ${listing.summary}")
	}
}

## A value given as `name=`, which must be an integer, as Shale's `arg` reads
## one whose default is.
integer_or_stop! : Str, Str => I64
integer_or_stop! = |name, given|
	match I64.from_str(given) {
		Ok(value) => value
		Err(_) => {
			Shale.fail!("list_deep_nesting: ${name}=${given} is not an integer")
			0
		}
	}

## Each function's body under the folder (and each test's, `tests`),
## measured. A body whose braces don't balance stops the run, naming it.
measure! : Str, Bool => List(Depth.Measured)
measure! = |folder, tests| {
	var $measured = []
	for path in c_files_under!(folder) {
		text = read_text!(path)
		lines = Split.lines(text)
		for item in split_or_stop!(path, text) {
			is_measured = item.kind == Function or (tests and item.kind == Test)
			if is_measured and item.body_first != 0 {
				match Depth.of_body(path, lines, item) {
					Ok(measured) => {
						$measured = $measured.append(measured)
					}
					Err(why) => Shale.fail!(why)
				}
			}
		}
	}
	$measured
}

## The file's text. A file the glob named and the read finds gone stops the
## run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("list_deep_nesting: ${path} went missing during the walk")
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
	paths.keep_if(|path| !(Paths.is_in_out_folder(folder, path))).sort_with(Words.byte_order)
}
