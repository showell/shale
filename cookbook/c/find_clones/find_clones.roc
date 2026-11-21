## find_clones, in Roc on Shale's platform (roc_platform/README.md): the
## same code twice in Shale's C, from the splitter the C recipes share
## (cookbook/c/items/, a package), as twin functions of one shape and runs
## of lines alike as written. Its launcher, cookbook/c/find_clones.shale, is
## how it is run, and its header says the arguments and the rules.
## check_goldens.shale, beside this, holds its output on the plants to its
## goldens, line for line.
##
##   shale cookbook:c/find_clones.shale dir=c/host score=70
##   shale cookbook:c/find_clones.shale self_test=1
##
## The effects: `Shale.arg!` for the arguments, `Shale.glob!` for the files,
## `Shale.read!` for each one's text, and `Shale.print!` and `Shale.fail!`
## for the verdict. Shape, beside this, reads a function's counted lines;
## Clones finds the twins and the runs; SelfTest holds them to the plants.
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
import Clones
import SelfTest
import Shape

main! : {} => {}
main! = |{}| {
	if integer_or_stop!("self_test", Shale.arg!("self_test", "0")) == 1 {
		SelfTest.run!(find!)
	} else {
		settings = {
			tests: integer_or_stop!("tests", Shale.arg!("tests", "0")) == 1,
			k: integer_or_stop!("k", Shale.arg!("k", "3")),
			score: integer_or_stop!("score", Shale.arg!("score", "80")),
			min: integer_or_stop!("min", Shale.arg!("min", "8")),
			window: integer_or_stop!("window", Shale.arg!("window", "6")),
		}
		folder = Shale.arg!("dir", "c")
		top = integer_or_stop!("top", Shale.arg!("top", "40"))
		found = find!(folder, settings)
		print_top!(found.twins, { top, what: "twins" })
		print_top!(found.runs, { top, what: "runs" })
		which = if settings.tests {
			"tests included"
		} else {
			"tests left out"
		}
		twins = "${found.twins.len().to_str()} twins (structure ${settings.score.to_str()}%+ over runs of ${settings.k.to_str()})"
		runs = "${found.runs.len().to_str()} runs of ${settings.window.to_str()}+ lines alike"
		Shale.print!("find_clones: ${found.functions.to_str()} functions under ${folder}, ${which}; ${twins}, ${runs}")
	}
}

## The first `top` lines, then how many more there are.
print_top! : List(Str), { top : I64, what : Str } => {}
print_top! = |lines, { top, what }| {
	for line in lines.take_first(I64.max(top, 0).to_u64_wrap()) {
		Shale.print!(line)
	}
	more = lines.len().to_i64_wrap() - top
	if more > 0 {
		Shale.print!("... ${more.to_str()} more ${what} (top=)")
	}
}

## A value given as `name=`, which must be an integer, as Shale's `arg` reads
## one whose default is.
integer_or_stop! : Str, Str => I64
integer_or_stop! = |name, given|
	match I64.from_str(given) {
		Ok(value) => value
		Err(_) => {
			Shale.fail!("find_clones: ${name}=${given} is not an integer")
			0
		}
	}

## The findings over the folder.
find! : Str, Clones.Settings => Clones.Found
find! = |folder, settings| Clones.found(read_functions!(folder, settings.tests), settings)

## Each function's counted lines under the folder, a test's too with
## `tests`: every function with a body of its own lines, in a file that is
## not the tests' unless `tests`.
read_functions! : Str, Bool => List(Shape.Function)
read_functions! = |folder, tests| {
	var $functions = []
	for path in c_files_under!(folder) {
		text = read_text!(path)
		lines = Split.lines(text)
		is_kept_file = tests or !(Paths.is_test_path(folder, path))
		for item in split_or_stop!(path, text) {
			is_read = item.kind == Function or (tests and item.kind == Test)
			has_body = item.body_first != 0 and item.body_first != item.sig_last
			if is_kept_file and is_read and has_body {
				$functions = $functions.append(Shape.of_item(path, lines, item))
			}
		}
	}
	$functions
}

## The file's text. A file the glob named and the read finds gone stops the
## run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("find_clones: ${path} went missing during the walk")
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
