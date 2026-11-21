## count_roc is a test-density report for a tree of Roc projects: folder by
## folder, its modules and their lines, the same for its test modules, and
## its `expect`s. For one app's size, `roc loc main.roc` counts what its
## imports reach; count_roc counts every `.roc` file under each folder,
## with a row a folder, so it sees a whole tree of apps, the tests `roc loc`
## doesn't count, and the modules no import reaches (specs a test root
## alone imports). Its launcher,
## cookbook/roc/count_roc.shale, is how it is run, and says the arguments.
##
##   cd ~/showell_repos && shale cookbook:roc/count_roc.shale in=roc-chat,roc-markdown/package
##
## The work is plain Roc over two of Shale's effects: `Shale.glob!` names a
## folder's `.roc` files and `Shale.read!` gives each one's text. A folder
## that isn't there stops the run (`Shale.exists!`, `Shale.fail!`).
## budget: steps=400000000 reads=50000 arena_bytes=1000000000
app [main!] { pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale

## What one folder holds.
Count : {
	place : Str,
	modules : U64,
	lines : U64,
	test_modules : U64,
	test_lines : U64,
	expects : U64,
}

## The literals that pick out the modules to treat apart: a path holding one
## of `tests` is a test module's, and one holding one of `skips` is left out.
Markers : { tests : List(Str), skips : List(Str) }

main! : {} => {}
main! = |{}| {
	markers = {
		tests: literals(Shale.arg!("tests", default_tests)),
		skips: literals(Shale.arg!("skip", "")),
	}
	var $counts = []
	for place in literals(Shale.arg!("folders", ".")) {
		$counts = $counts.append(count_folder!(place, markers))
	}
	Shale.print!(count_table($counts))
}

## The literals of a test module's path unless `tests=` is given: the
## folders and file endings our Roc repos give their tests, specs and gates.
default_tests : Str
default_tests = "/gates/,/tests/,Tests.roc,Expects.roc,_test.roc,Spec.roc,Specs.roc,/specs"

## A comma-separated list's items, each trimmed, the empty ones dropped.
literals : Str -> List(Str)
literals = |text|
	text.split_on(",").map(Str.trim).drop_if(Str.is_empty)

# ── Counting a folder

count_folder! : Str, Markers => Count
count_folder! = |place, markers| {
	if !(Shale.exists!(place)) {
		Shale.fail!("count_roc: no folder `${place}`")
	}
	var $count = {
		place,
		modules: 0,
		lines: 0,
		test_modules: 0,
		test_lines: 0,
		expects: 0,
	}
	for path in Shale.glob!(roc_files_pattern(place)) {
		if !(is_build_output(path) or has_any_marker(path, markers.skips)) {
			match Shale.read!(path) {
				Found(text) => {
					is_test = has_any_marker(path, markers.tests)
					$count = with_file($count, { text, is_test })
				}
				Missing => Shale.fail!("count_roc: ${path} went missing during the count")
			}
		}
	}
	$count
}

## The glob of every `.roc` file below the folder. Shale's glob leaves out
## hidden names, so `.git` and `.zig-cache` are never walked.
roc_files_pattern : Str -> Str
roc_files_pattern = |place|
	if place == "." {
		"**/*.roc"
	} else {
		"${place}/**/*.roc"
	}

## Whether the path is under a build's `zig-out` folder.
is_build_output : Str -> Bool
is_build_output = |path|
	path.contains("/zig-out/")

has_any_marker : Str, List(Str) -> Bool
has_any_marker = |path, markers|
	markers.any(|marker| path.contains(marker))

## The count with one more file added, a module or a test module.
with_file : Count, { text : Str, is_test : Bool } -> Count
with_file = |count, file| {
	lines = line_count(file.text)
	expects = count.expects + expect_count(file.text)
	if file.is_test {
		{
			..count,
			test_modules: count.test_modules + 1,
			test_lines: count.test_lines + lines,
			expects,
		}
	} else {
		{
			..count,
			modules: count.modules + 1,
			lines: count.lines + lines,
			expects,
		}
	}
}

# ── Reading a module's text

## The newlines, and a last line that has none, as Shale's `count_lines`
## counts them.
line_count : Str -> U64
line_count = |text| {
	bytes = text.to_utf8()
	newlines = bytes.count_if(|byte| byte == '\n')
	match bytes.last() {
		Ok('\n') => newlines
		Ok(_) => newlines + 1
		Err(_) => 0
	}
}

## The lines that start an `expect`: the word itself after any indent, not
## a longer name such as `expected`.
expect_count : Str -> U64
expect_count = |text|
	text.split_on("\n").count_if(is_expect_line)

is_expect_line : Str -> Bool
is_expect_line = |line| {
	rest = bytes_after_indent(line.to_utf8())
	if rest.starts_with(Str.to_utf8("expect")) {
		match rest.get(6) {
			Ok(byte) => !(is_word_byte(byte))
			Err(_) => Bool.True
		}
	} else {
		Bool.False
	}
}

## The bytes after a line's indent: spaces, tabs and the other bytes a
## regular expression's `\s` matches.
bytes_after_indent : List(U8) -> List(U8)
bytes_after_indent = |bytes|
	match bytes.find_first_index(|byte| !(is_space_byte(byte))) {
		Ok(start) => bytes.drop_first(start)
		Err(_) => []
	}

is_space_byte : U8 -> Bool
is_space_byte = |byte|
	byte == ' ' or byte == '\t' or byte == '\r' or byte == 0x0b or byte == 0x0c

## A letter, a digit or `_`: what a regular expression's `\w` matches.
is_word_byte : U8 -> Bool
is_word_byte = |byte|
	is_ascii_letter(byte) or (byte >= '0' and byte <= '9') or byte == '_'

is_ascii_letter : U8 -> Bool
is_ascii_letter = |byte|
	(byte >= 'a' and byte <= 'z') or (byte >= 'A' and byte <= 'Z')

# ── The table

## A column's heading and which side its cells line up on.
Column : { heading : Str, alignment : [Left, Right] }

columns : List(Column)
columns = [
	{ heading: "place", alignment: Left },
	{ heading: "modules", alignment: Right },
	{ heading: "lines", alignment: Right },
	{ heading: "test_modules", alignment: Right },
	{ heading: "test_lines", alignment: Right },
	{ heading: "expects", alignment: Right },
]

count_cells : Count -> List(Str)
count_cells = |count| [
	count.place,
	count.modules.to_str(),
	count.lines.to_str(),
	count.test_modules.to_str(),
	count.test_lines.to_str(),
	count.expects.to_str(),
]

## The counts as Shale's `table` prints rows: a line of headings, then a line
## a folder, each column as wide as its widest cell, columns two spaces
## apart, the numbers right-aligned.
count_table : List(Count) -> Str
count_table = |counts| {
	rows = counts.map(count_cells).prepend(columns.map(|column| column.heading))
	widths = rows.fold(columns.map(|_| 0), with_row)
	layouts = List.map2(
		columns,
		widths,
		|column, width| { alignment: column.alignment, width },
	)
	table_lines = rows.map(|row| Str.join_with(List.map2(layouts, row, padded_cell), "  "))
	Str.join_with(table_lines, "\n")
}

## Each column's width so far, widened to fit the row's cell.
with_row : List(U64), List(Str) -> List(U64)
with_row = |widths, cells|
	List.map2(
		widths,
		cells,
		|width, cell| U64.max(width, cell.count_utf8_bytes()),
	)

padded_cell : { alignment : [Left, Right], width : U64 }, Str -> Str
padded_cell = |layout, cell| {
	gap = " ".repeat(layout.width - cell.count_utf8_bytes())
	match layout.alignment {
		Left => "${cell}${gap}"
		Right => "${gap}${cell}"
	}
}
