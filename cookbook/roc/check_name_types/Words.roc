## The words the checks read beside the code: the prepositions
## (cookbook/words/prepositions.txt) and a project's table of words and the
## types its names must hold, as the `table=` file gives it. Each line of the
## table is `word: Type, Type`, `word: none` or `word: any`, such as `dist:
## Distance`. A blank line or one starting with `#` is skipped, and a
## malformed line or a word listed twice stops the run. The table belongs to
## the project, because what `dist` must be depends on the project's types.
import pf.Shale
import Text

Words := {
	## What the table promises of each word in it.
	promises : Dict(Str, Words.Promise),

	## The prepositions, which a name or a field may start with or be.
	prepositions : Set(Str),
}.{

	## What the table says of a word: no name may end in it, it holds no
	## promise at all, or a name ending in it holds one of the types.
	Promise : [NoNameMay, AnyType, OneOf(List(Str))]

	## The prepositions, then the table in the file at `table_path` (none when
	## it is `""`).
	read! : Str => Words
	read! = |table_path| {
		prepositions = read_word_set!("cookbook:words/prepositions.txt")
		{ promises: read_table!(table_path), prepositions }
	}

	## Whether the table gives `word` as `any`.
	promises_nothing : Words, Str -> Bool
	promises_nothing = |words, word|
		match words.promises.get(word) {
			Ok(AnyType) => Bool.True
			_ => Bool.False
		}
}

## The table in the file at `path`, none when `path` is `""`.
read_table! : Str => Dict(Str, Words.Promise)
read_table! = |path| {
	if path == "" {
		return Dict.empty()
	}
	if !(Shale.exists!(path)) {
		Shale.fail!("check_name_types: no table ${path}")
	}
	text = match Shale.read!(path) {
		Found(found) => found
		Missing => {
			Shale.fail!("check_name_types: no table ${path}")
			""
		}
	}
	var $promises = Dict.empty()
	var $number = 0
	for raw in Text.lines(text) {
		$number = $number + 1
		line = Text.trim(raw)
		if line != "" and !(line.starts_with("#")) {
			table_line = { path, number: $number, line }
			entry = read_entry!(table_line)
			if $promises.contains(entry.word) {
				fail_at!(table_line, "`${entry.word}` is listed twice")
			}
			$promises = $promises.insert(entry.word, entry.promise)
		}
	}
	$promises
}

## The line pattern: a word, then `none`, `any` or a list of types.
line_pattern : Str
line_pattern = "^([a-z0-9]+): *(none|any|[A-Z][A-Za-z0-9]*(?:, *[A-Z][A-Za-z0-9]*)*)$"

## One line of the table, with the file and its number to say where it is.
TableLine : {
	path : Str,
	number : U64,
	line : Str,
}

## Stops the run, saying where in the table and what is wrong there.
fail_at! : TableLine, Str => {}
fail_at! = |table_line, why| {
	place = "${table_line.path}:${table_line.number.to_str()}"
	Shale.fail!("check_name_types: ${place}: ${why}")
}

## The word and its promise on one line. A line that is neither stops the run.
read_entry! : TableLine => { word : Str, promise : Words.Promise }
read_entry! = |table_line| {
	groups = match Shale.re_find!(table_line.line, line_pattern) {
		Found(found) => found.groups
		NoMatch => {
			fail_at!(table_line, "expected `word: Type, Type`, `word: none` or `word: any`")
			[]
		}
	}
	match groups {
		[word, said] => { word, promise: promise_of(said) }
		_ => crash "check_name_types: a table line's pattern gave no two groups"
	}
}

## What `none`, `any` or a list of types promises.
promise_of : Str -> Words.Promise
promise_of = |said|
	if said == "none" {
		NoNameMay
	} else if said == "any" {
		AnyType
	} else {
		OneOf(said.split_on(",").map(Text.trim))
	}

## A list of one word a line, any note after `--` cut off, as a set
## (cookbook/roc/declared.shale's `word_set`). A missing file, a word listed
## twice, or a line of two words stops the run.
read_word_set! : Str => Set(Str)
read_word_set! = |path| {
	text = match Shale.read!(path) {
		Found(found) => found
		Missing => {
			Shale.fail!("no word list ${path}")
			""
		}
	}
	var $words = Set.empty()
	for line in Text.lines(text) {
		word = Text.trim(text_before_note(line))
		if word.contains(" ") or $words.contains(word) {
			Shale.fail!("${path}: `${word}` is no single new word")
		}
		if word != "" {
			$words = $words.insert(word)
		}
	}
	$words
}

## The line before any note after `--`.
text_before_note : Str -> Str
text_before_note = |line|
	match line.split_first("--") {
		Ok(parts) => parts.before
		Err(_) => line
	}
