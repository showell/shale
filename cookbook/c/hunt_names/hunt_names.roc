## hunt_names, in Roc on Shale's platform (roc_platform/README.md): Shale's C
## hunted for names that break c/docs/style.md's "Names" or say too little,
## from the splitter the C recipes share (cookbook/c/items/, a package). A
## finding is a question for a reader, never a verdict. Its launcher,
## cookbook/c/hunt_names.shale, is how it is run, and its header says the
## arguments and the checks. check_goldens.shale, beside this, holds its
## output on the plants to its goldens, line for line.
##
##   shale cookbook:c/hunt_names.shale dir=c/host
##   shale cookbook:c/hunt_names.shale self_test=1
##
## The effects: `Shale.arg!` for the arguments, `Shale.read!` for the word
## tables (cookbook/words/) and each file's text, `Shale.list!` and
## `Shale.glob!` for the files, and `Shale.print!` and `Shale.fail!` for the
## verdict. Checks, beside this, checks a file; Abbreviation, the tree's
## names; SelfTest holds it to the plants.
## budget: steps=200000000 arena_bytes=1000000000 roc_cpu_ms=600000
app [main!] {
	items: "../items/main.roc",
	pf: platform "../../../roc_platform/platform/main.roc",
}

import pf.Shale
import items.Item
import items.Mask
import items.Paths
import items.Split
import items.Words
import Abbreviation
import Checks
import SelfTest

main! : {} => {}
main! = |{}| {
	if integer_or_stop!("self_test", Shale.arg!("self_test", "0")) == 1 {
		SelfTest.run!(hunt!)
	} else {
		folder = Shale.arg!("dir", "c")
		hunted = hunt!(folder)
		for finding in hunted.found {
			Shale.print!(Checks.finding_line(finding))
		}
		Shale.print!(summary(folder, hunted))
		for line in by_folder(hunted.found) {
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
			Shale.fail!("hunt_names: ${name}=${given} is not an integer")
			0
		}
	}

## The findings over the folder, sorted by place, and how many files it
## read.
hunt! : Str => { found : List(Checks.Finding), files : U64 }
hunt! = |folder| {
	tables = tables!({})
	files = c_files_under!(folder)
	var $found = []
	var $named = []
	for path in files {
		text = read_text!(path)
		checked = Checks.of_file(path, Split.lines(text), split_or_stop!(path, text), tables)
		$found = $found.concat(checked.found)
		$named = $named.concat(checked.named)
	}
	all = $found.concat(Abbreviation.findings($named, tables.abbreviations))
	{ found: all.sort_with(place_order), files: files.len() }
}

## Findings in the order of their path, line, check and why.
place_order : Checks.Finding, Checks.Finding -> [Before, Same, After]
place_order = |a, b| {
	by_path = Words.byte_order(a.path, b.path)
	if by_path != Same {
		by_path
	} else if a.line < b.line {
		Before
	} else if a.line > b.line {
		After
	} else {
		by_check = Words.byte_order(a.check, b.check)
		if by_check != Same {
			by_check
		} else {
			Words.byte_order(a.why, b.why)
		}
	}
}

## The line of counts: the files, the findings and each check's.
summary : Str, { found : List(Checks.Finding), files : U64 } -> Str
summary = |folder, hunted| {
	parts = Checks.check_names.map(|check| "${check} ${hunted.found.count_if(|finding| finding.check == check).to_str()}")
	"hunt_names: ${hunted.files.to_str()} files under ${folder}; ${hunted.found.len().to_str()} findings: ${Str.join_with(parts, ", ")}"
}

## The findings counted by each file's folder and check, a line each, in the
## order of `<folder> <check>`.
by_folder : List(Checks.Finding) -> List(Str)
by_folder = |found| {
	no_counts : Dict(Str, U64)
	no_counts = Dict.empty()
	counts = found.fold(
		no_counts,
		|so_far, finding| {
			key = "${folder_of(finding.path)} ${finding.check}"
			so_far.insert(key, (so_far.get(key) ?? 0) + 1)
		},
	)
	counts
		.to_list()
		.sort_with(|(a, _), (b, _)| Words.byte_order(a, b))
		.map(|(key, count)| "  ${key}: ${count.to_str()}")
}

## The path's folder, as Shale's `dirname` gives it: up to its last `/`.
folder_of : Str -> Str
folder_of = |path| {
	bytes = path.to_utf8()
	match bytes.find_last_index(|byte| byte == '/') {
		Ok(slash) => Mask.text(bytes.take_first(slash))
		Err(_) => ""
	}
}

## The word tables of cookbook/words/: vague.txt's, containers.txt's with
## `data` and `info`, and c_abbreviations.txt's.
tables! : {} => Checks.Tables
tables! = |{}| {
	containers = word_table!("containers.txt").insert("data").insert("info")
	{ vague: word_table!("vague.txt"), containers, abbreviations: abbreviation_table!({}) }
}

## A table's text. A table that is not there stops the run.
table_text! : Str => Str
table_text! = |file|
	match Shale.read!("cookbook:words/${file}") {
		Found(text) => text
		Missing => {
			Shale.fail!("hunt_names: cookbook/words/${file} is not there")
			""
		}
	}

## A line's text before its note, the `--` that starts one, trimmed.
before_note : List(U8) -> List(U8)
before_note = |line| {
	kept = match Mask.pair_from(line, ('-', '-'), 0) {
		Ok(note) => line.take_first(note)
		Err(_) => line
	}
	Mask.trimmed(kept)
}

## A word table of cookbook/words/, a word a line. A word twice, or a line of
## two, is a mistake in the table: it stops the run.
word_table! : Str => Set(Str)
word_table! = |file| {
	var $words = Set.empty()
	for line in Split.lines(table_text!(file)) {
		word = Mask.text(before_note(line))
		if word.contains(" ") or $words.contains(word) {
			Shale.fail!("hunt_names: cookbook/words/${file}: `${word}` is no single new word")
		}
		if word != "" {
			$words = $words.insert(word)
		}
	}
	$words
}

## cookbook/words/c_abbreviations.txt: an abbreviation to its word, a pair a
## line. An abbreviation twice, or a line of one word or three, stops the
## run.
abbreviation_table! : {} => Dict(Str, Str)
abbreviation_table! = |{}| {
	file = "cookbook/words/c_abbreviations.txt"
	var $table = Dict.empty()
	for line in Split.lines(table_text!("c_abbreviations.txt")) {
		words = words_in(before_note(line))
		match (words.get(0), words.get(1)) {
			(Ok(short), Ok(full)) if words.len() == 2 => {
				if $table.get(short) != Err(KeyNotFound) {
					Shale.fail!("hunt_names: ${file}: `${short}` twice")
				}
				$table = $table.insert(short, full)
			}
			_ =>
				if !(words.is_empty()) {
					Shale.fail!("hunt_names: ${file}: `${Mask.text(line)}` is no pair of an abbreviation and its word")
				}
		}
	}
	$table
}

## The words of a line, as Shale's `words` cuts them: at each run of `\s`.
words_in : List(U8) -> List(Str)
words_in = |line| {
	var $words = []
	var $at = Mask.spaces_end(line, 0)
	while $at < line.len() {
		end = match line.drop_first($at).find_first_index(Mask.is_space) {
			Ok(offset) => $at + offset
			Err(_) => line.len()
		}
		$words = $words.append(Mask.text(line.sublist({ start: $at, len: end - $at })))
		$at = Mask.spaces_end(line, end)
	}
	$words
}

## The file's text. A file the walk named and the read finds gone stops the
## run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("hunt_names: ${path} went missing during the walk")
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
