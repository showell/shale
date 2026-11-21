## count_comments counts how much a Zig tree's comments say, and how loudly:
## for each file its code lines, its doc lines (`///`, `//!`), its plain `//`
## lines, comment lines per 100 lines of code, words a comment line, and the
## loud and dated comment lines; the loudest first, then the whole's totals.
## Its launcher, cookbook/zig/count_comments.shale, is how it is run, and its
## header says the arguments and the rules.
##
##   cd <repo> && shale cookbook:zig/count_comments.shale top=10
##
## The work is plain Roc over Shale's effects: `Shale.arg!` reads `dir`,
## `files` and `top`; `Shale.glob!` and `Shale.exists!` name the files;
## `Shale.read!` gives each one's text; `Shale.print!` writes the table; and
## `Shale.fail!` stops the run when no file matches or `top` is no integer.
## Each line is read as bytes, so a word boundary is a regular expression's
## `\b`: an ASCII letter, digit or `_` against anything else.
## budget: steps=900000000 reads=100000 read_bytes=400000000 arena_bytes=2000000000
app [main!] { pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale

## What a line is, once its indent and its ends are trimmed: nothing, code
## (a comment after code counts as code), or a comment line alone.
LineKind : [Blank, Code, Doc, Plain]

## The counts of a file, or of the whole.
Tally : {
	code : U64,
	doc : U64,
	plain : U64,
	words : U64,
	loud : U64,
	dated : U64,
}

## A file's row: the path as the table shows it, and its counts.
Row : { file : Str, tally : Tally }

main! : {} => {}
main! = |{}| {
	folder = Shale.arg!("dir", ".")
	patterns = Shale.arg!("files", "src/**/*.zig,tools/*.zig,build.zig")
	top = read_top!({})
	paths = find_files!(folder, patterns)
	if paths.is_empty() {
		Shale.fail!("count_comments: no file matches ${patterns} under ${folder}")
	}
	var $rows = []
	for path in paths {
		tally = tally_lines(read_text!(path).to_utf8())
		$rows = $rows.append({ file: shown_path(folder, path), tally })
	}
	total = $rows.fold(no_lines, |sum, row| sum_of(sum, row.tally))
	loudest_first = $rows.sort_by_reversed(|row| row.tally.loud)
	shown_rows = if top > 0 {
		loudest_first.take_first(top.to_u64_wrap())
	} else {
		loudest_first
	}
	Shale.print!(rows_table(shown_rows))
	file_count = paths.len()
	Shale.print!(summary(file_count, total))
}

## The `top=` row count: 0, every row, unless given.
read_top! : {} => I64
read_top! = |{}| {
	given = Shale.arg!("top", "0")
	match I64.from_str(given) {
		Ok(count) => count
		Err(_) => {
			Shale.fail!("count_comments: top=${given} is not an integer")
			0
		}
	}
}

# ── The files

## The files the comma-separated globs name under the folder, each once, in
## the order named. A pattern with no `*` is a file's path, kept when it is
## there.
find_files! : Str, Str => List(Str)
find_files! = |folder, patterns| {
	var $paths = []
	var $seen = Set.empty()
	for pattern in patterns.split_on(",") {
		for path in find_pattern_files!(folder, trim_ends(pattern)) {
			if !($seen.contains(path)) {
				$seen = $seen.insert(path)
				$paths = $paths.append(path)
			}
		}
	}
	$paths
}

find_pattern_files! : Str, Str => List(Str)
find_pattern_files! = |folder, pattern| {
	full = if folder == "." {
		pattern
	} else {
		"${folder}/${pattern}"
	}
	if pattern.contains("*") {
		Shale.glob!(full)
	} else if Shale.exists!(full) {
		[full]
	} else {
		[]
	}
}

## The file's text. A file named a moment ago and gone now stops the run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("count_comments: ${path} went missing during the count")
			""
		}
	}

## The path as the table shows it: below the folder, without the folder.
shown_path : Str, Str -> Str
shown_path = |folder, path|
	if folder != "." and path.starts_with("${folder}/") {
		path.drop_prefix("${folder}/")
	} else {
		path
	}

## The text without the spaces, tabs, carriage returns and newlines at its
## ends, as Shale's `trim` takes them.
trim_ends : Str -> Str
trim_ends = |text| {
	kept = trimmed_bytes(text.to_utf8())
	Str.from_utf8(kept) ?? text
}

# ── Counting a file's lines

no_lines : Tally
no_lines = {
	code: 0,
	doc: 0,
	plain: 0,
	words: 0,
	loud: 0,
	dated: 0,
}

## A file's counts, from its bytes: each line, split at its newlines,
## trimmed and counted.
tally_lines : List(U8) -> Tally
tally_lines = |bytes|
	bytes.split_on('\n').fold(no_lines, |tally, line| with_line(tally, trimmed_bytes(line)))

## The tally with one trimmed line more.
with_line : Tally, List(U8) -> Tally
with_line = |tally, line|
	match line_kind(line) {
		Blank => tally
		Code => { ..tally, code: tally.code + 1 }
		Doc => with_comment({ ..tally, doc: tally.doc + 1 }, line)
		Plain => with_comment({ ..tally, plain: tally.plain + 1 }, line)
	}

line_kind : List(U8) -> LineKind
line_kind = |line|
	if line.is_empty() {
		Blank
	} else if line.starts_with(['/', '/', '/']) or line.starts_with(['/', '/', '!']) {
		Doc
	} else if line.starts_with(['/', '/']) {
		Plain
	} else {
		Code
	}

## A comment line's words (after its first three bytes, the `//` and the
## space or mark after it), and whether it is loud or dated.
with_comment : Tally, List(U8) -> Tally
with_comment = |tally, line| {
	..tally,
	words: tally.words + word_count(line.drop_first(3)),
	loud: tally.loud + one_if(is_loud(line)),
	dated: tally.dated + one_if(is_dated(line)),
}

one_if : Bool -> U64
one_if = |holds|
	if holds {
		1
	} else {
		0
	}

sum_of : Tally, Tally -> Tally
sum_of = |sum, more| {
	code: sum.code + more.code,
	doc: sum.doc + more.doc,
	plain: sum.plain + more.plain,
	words: sum.words + more.words,
	loud: sum.loud + more.loud,
	dated: sum.dated + more.dated,
}

# ── Reading a line's bytes

## The bytes between the line's leading and trailing spaces, tabs, carriage
## returns and newlines.
trimmed_bytes : List(U8) -> List(U8)
trimmed_bytes = |bytes|
	match bytes.find_first_index(|byte| !(is_trimmed_byte(byte))) {
		Ok(start) => {
			end = bytes.find_last_index(|byte| !(is_trimmed_byte(byte))) ?? start
			bytes.sublist({ start, len: end + 1 - start })
		}
		Err(_) => []
	}

is_trimmed_byte : U8 -> Bool
is_trimmed_byte = |byte|
	byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n'

## The words, as Shale's `words` splits them: at spaces, tabs, newlines,
## carriage returns, vertical tabs and form feeds.
word_count : List(U8) -> U64
word_count = |bytes| {
	counted = bytes.fold(
		{ words: 0, in_word: Bool.False },
		|so_far, byte|
			if is_word_gap(byte) {
				{ ..so_far, in_word: Bool.False }
			} else if so_far.in_word {
				so_far
			} else {
				{ words: so_far.words + 1, in_word: Bool.True }
			},
	)
	counted.words
}

is_word_gap : U8 -> Bool
is_word_gap = |byte|
	is_trimmed_byte(byte) or byte == 0x0b or byte == 0x0c

## A letter, a digit or `_`: what a regular expression's `\w` matches.
is_word_byte : U8 -> Bool
is_word_byte = |byte|
	is_upper(byte) or (byte >= 'a' and byte <= 'z') or (byte >= '0' and byte <= '9') or byte == '_'

is_upper : U8 -> Bool
is_upper = |byte|
	byte >= 'A' and byte <= 'Z'

## A part of a pattern: a byte as it is, or any digit.
Part : [Byte(U8), Digit]

literal_parts : Str -> List(Part)
literal_parts = |text|
	text.to_utf8().map(|byte| Byte(byte))

## Whether `holds_at` holds at some place of a line of `length` bytes.
any_place : U64, (U64 -> Bool) -> Bool
any_place = |length, holds_at|
	U64.until(0, length).fold(Bool.False, |found, at| found or holds_at(at))

## Whether the bytes start with the pattern's parts.
starts_with_parts : List(U8), List(Part) -> Bool
starts_with_parts = |bytes, parts| {
	fits_each = |fits, offset| fits and part_fits(bytes.get(offset), parts.get(offset))
	parts.len() <= bytes.len() and U64.until(0, parts.len()).fold(Bool.True, fits_each)
}

part_fits : Try(U8, [OutOfBounds]), Try(Part, [OutOfBounds]) -> Bool
part_fits = |byte, part|
	match (byte, part) {
		(Ok(got), Ok(Byte(wanted))) => got == wanted
		(Ok(got), Ok(Digit)) => got >= '0' and got <= '9'
		_ => Bool.False
	}

## Whether the pattern's parts are anywhere in the bytes.
holds : List(U8), List(Part) -> Bool
holds = |bytes, parts|
	any_place(bytes.len(), |at| starts_with_parts(bytes.drop_first(at), parts))

## Whether the pattern is in the bytes as a word of its own, with no word
## byte just before it or just after it. Every pattern this asks of starts
## and ends with a word byte, so this is a regular expression's `\b...\b`.
holds_word : List(U8), List(Part) -> Bool
holds_word = |bytes, parts| {
	is_word_at_place = |at| {
		fits = starts_with_parts(bytes.drop_first(at), parts)
		fits and is_boundary(bytes, at) and is_boundary(bytes, at + parts.len())
	}
	any_place(bytes.len(), is_word_at_place)
}

## Whether the place before byte `at` is a regular expression's `\b`: a word
## byte on one side of it and none on the other, the line's ends counting as
## none.
is_boundary : List(U8), U64 -> Bool
is_boundary = |bytes, at| {
	word_before = if at == 0 {
		Bool.False
	} else {
		is_word_at(bytes, at - 1)
	}
	word_before != is_word_at(bytes, at)
}

is_word_at : List(U8), U64 -> Bool
is_word_at = |bytes, at|
	match bytes.get(at) {
		Ok(byte) => is_word_byte(byte)
		Err(_) => Bool.False
	}

## A loud line holds `**`, a `──` banner, or a heading of two upper-case
## words or more (`THE HUNT`).
is_loud : List(U8) -> Bool
is_loud = |line|
	holds(line, double_star_parts) or holds(line, banner_parts) or holds_heading(line)

double_star_parts : List(Part)
double_star_parts = literal_parts("**")

banner_parts : List(Part)
banner_parts = literal_parts("──")

## Whether a word starts with two capitals or more, then a space, then two
## capitals or more: the regular expression `\b[A-Z][A-Z]+ [A-Z][A-Z]+`.
holds_heading : List(U8) -> Bool
holds_heading = |line|
	any_place(line.len(), |at| is_heading_at(line, at))

is_heading_at : List(U8), U64 -> Bool
is_heading_at = |line, at| {
	capitals = capitals_from(line, at)
	capitals >= 2
		and is_boundary(line, at)
		and line.get(at + capitals) == Ok(' ')
		and capitals_from(line, at + capitals + 1) >= 2
}

## How many capitals in a row start at `at`.
capitals_from : List(U8), U64 -> U64
capitals_from = |line, at|
	match line.drop_first(at).find_first_index(|byte| !(is_upper(byte))) {
		Ok(count) => count
		Err(_) => line.len() - U64.min(at, line.len())
	}

## A dated line names a QUEUE or FEEDBACK item, a date (`2026-10-20`), or a
## person (`Steve`, `the box`, `CC`), each a word of its own but the items.
is_dated : List(U8) -> Bool
is_dated = |line|
	items.any(|item| holds(line, item)) or dated_words.any(|word| holds_word(line, word))

items : List(List(Part))
items = [literal_parts("QUEUE"), literal_parts("FEEDBACK")]

dated_words : List(List(Part))
dated_words = [
	date_parts,
	literal_parts("Steve"),
	literal_parts("the box"),
	literal_parts("CC"),
]

## `20` and six more digits as a year, a month and a day: `2026-10-20`.
date_parts : List(Part)
date_parts = [
	Byte('2'),
	Byte('0'),
	Digit,
	Digit,
	Byte('-'),
	Digit,
	Digit,
	Byte('-'),
	Digit,
	Digit,
]

# ── The table and the totals

## A share as `n.d`, of `part` per 100 of `whole`, rounded; `-` of nothing.
per100 : U64, U64 -> Str
per100 = |part, whole|
	if whole == 0 {
		"-"
	} else {
		tenths = (part * 1000 + whole // 2) // whole
		"${(tenths // 10).to_str()}.${(tenths % 10).to_str()}"
	}

## A column's heading and which side its cells line up on.
Column : { heading : Str, alignment : [Left, Right] }

columns : List(Column)
columns = [
	{ heading: "file", alignment: Left },
	{ heading: "code", alignment: Right },
	{ heading: "doc", alignment: Right },
	{ heading: "plain", alignment: Right },
	{ heading: "per100", alignment: Left },
	{ heading: "words", alignment: Left },
	{ heading: "loud", alignment: Right },
	{ heading: "dated", alignment: Right },
]

row_cells : Row -> List(Str)
row_cells = |row| {
	tally = row.tally
	comments = tally.doc + tally.plain
	[
		row.file,
		tally.code.to_str(),
		tally.doc.to_str(),
		tally.plain.to_str(),
		per100(comments, tally.code),
		per100(tally.words, comments * 100),
		tally.loud.to_str(),
		tally.dated.to_str(),
	]
}

## The rows as Shale's `table` prints them: a line of headings, then a line
## a file, each column as wide as its widest cell, columns two spaces
## apart, the counts right-aligned and the shares, which are text, left.
rows_table : List(Row) -> Str
rows_table = |rows| {
	cells = rows.map(row_cells).prepend(columns.map(|column| column.heading))
	widths = cells.fold(columns.map(|_| 0), with_row)
	layouts = List.map2(
		columns,
		widths,
		|column, width| { alignment: column.alignment, width },
	)
	table_lines = cells.map(|row| Str.join_with(List.map2(layouts, row, padded_cell), "  "))
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

## The whole's line under the table.
summary : U64, Tally -> Str
summary = |file_count, total| {
	comments = total.doc + total.plain
	lines = "${total.code.to_str()} code lines, ${comments.to_str()} comment lines"
	kinds = "${total.doc.to_str()} doc, ${total.plain.to_str()} plain"
	per_code = per100(comments, total.code)
	per_comment = per100(total.words, comments * 100)
	shares = "${per_code} per 100, ${per_comment} words a line"
	marks = "${total.loud.to_str()} loud, ${total.dated.to_str()} dated"
	"${file_count.to_str()} files: ${lines} (${kinds}), ${shares}, ${marks}"
}
