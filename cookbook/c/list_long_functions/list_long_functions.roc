## list_long_functions, in Roc on Shale's platform (roc_platform/README.md):
## Shale's C functions past a length, longest first, from the splitter the
## C recipes share (cookbook/c/items/, a package). A function's length is
## its body's lines of code; its comment lines and blank lines are counted
## beside it, never in it. Its launcher, cookbook/c/list_long_functions.shale,
## is how it is run, and its header says the arguments and the rules.
## check_goldens.shale, beside this, holds its output on the plants to its
## goldens, line for line.
##
##   shale cookbook:c/list_long_functions.shale over=60
##   shale cookbook:c/list_long_functions.shale self_test=1
##
## The effects: `Shale.arg!` for the arguments, `Shale.glob!` for the files,
## `Shale.read!` for each one's text, and `Shale.print!` and `Shale.fail!`
## for the verdict. Length, beside this, measures and lists; SelfTest holds
## it to the plants.
## budget: steps=200000000 arena_bytes=1000000000 roc_cpu_ms=600000
app [main!] {
	items: "../items/main.roc",
	pf: platform "../../../roc_platform/platform/main.roc",
}

import pf.Shale
import items.Item
import items.Split
import items.Words
import Length
import SelfTest

main! : {} => {}
main! = |{}| {
	if integer_or_stop!("self_test", Shale.arg!("self_test", "0")) == 1 {
		SelfTest.run!(measure!)
	} else {
		folder = Shale.arg!("dir", "c")
		tests = integer_or_stop!("tests", Shale.arg!("tests", "0")) == 1
		over = integer_or_stop!("over", Shale.arg!("over", "0"))
		limit = if over > 0 {
			Given(over.to_u64_wrap())
		} else {
			Percentile
		}
		listing = match Length.listing(measure!(folder, tests), limit) {
			Ok(listed) => listed
			Err(why) => {
				Shale.fail!(why)
				{ summary: "", threshold: 0, lines: [] }
			}
		}
		which = if tests {
			"tests included"
		} else {
			"tests left out"
		}
		Shale.print!("list_long_functions: ${folder}, ${which}: ${listing.summary}")
		for line in listing.lines {
			Shale.print!(line)
		}
		how = match limit {
			Given(_) => "over="
			Percentile => "the 95th percentile"
		}
		past = "${listing.lines.len().to_str()} functions past ${listing.threshold.to_str()} code lines"
		Shale.print!("list_long_functions: ${past} (${how})")
	}
}

## A value given as `name=`, which must be an integer, as Shale's `arg` reads
## one whose default is.
integer_or_stop! : Str, Str => I64
integer_or_stop! = |name, given|
	match I64.from_str(given) {
		Ok(value) => value
		Err(_) => {
			Shale.fail!("list_long_functions: ${name}=${given} is not an integer")
			0
		}
	}

## Each function's body under the folder (and each test's, `tests`),
## measured.
measure! : Str, Bool => List(Length.Measured)
measure! = |folder, tests| {
	var $measured = []
	for path in c_files_under!(folder) {
		text = read_text!(path)
		lines = Split.lines(text)
		for item in split_or_stop!(path, text) {
			is_measured = item.kind == Function or (tests and item.kind == Test)
			if is_measured and item.body_first != 0 {
				$measured = $measured.append(Length.of_body(path, lines, item))
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
			Shale.fail!("list_long_functions: ${path} went missing during the walk")
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
