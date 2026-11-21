## Text cut, trimmed, lowered and ordered as Shale's builtins and preludes do
## it (`lines`, `words`, `trim`, `lower`, `sub`, `split`, `between`,
## `basename`, `dirname`, `path_under`, `sort`), and a name's words as
## cookbook/roc/declared.shale splits them, so a recipe's output is its
## Shale original's, byte for byte.
Text := [].{

	## A text's lines, as Shale's `lines` splits it: a last newline ends a
	## line, and an empty text has none.
	lines : Str -> List(Str)
	lines = |text| {
		parts = text.split_on("\n")
		if text.ends_with("\n") or text == "" {
			parts.drop_last(1)
		} else {
			parts
		}
	}

	## Line `number` of `source_lines`, counted from 1. A line the file lacks
	## stops the run, as Shale's index past a list's end does.
	line_at : List(Str), U64 -> Str
	line_at = |source_lines, number|
		match source_lines.get(number - 1) {
			Ok(line) => line
			Err(_) => crash "Text.line_at: the file has no line ${number.to_str()}"
		}

	## The bytes of `text` from place `start` (counted from 0), `count` of them
	## or as many as there are, as text.
	bytes_of : Str, { start : U64, count : U64 } -> Str
	bytes_of = |text, span| {
		bytes = text.to_utf8().sublist({ start: span.start, len: span.count })
		Str.from_utf8_lossy(bytes)
	}

	## The text without the spaces, tabs, carriage returns and newlines at its
	## ends, as Shale's `trim` takes them.
	trim : Str -> Str
	trim = |text| {
		bytes = text.to_utf8()
		is_kept = |byte| !(byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n')
		match bytes.find_first_index(is_kept) {
			Ok(start) => {
				end = bytes.find_last_index(is_kept) ?? start
				Str.from_utf8_lossy(bytes.sublist({ start, len: end + 1 - start }))
			}
			Err(_) => ""
		}
	}

	## The text with its ASCII capitals made small, as Shale's `lower` does.
	lower : Str -> Str
	lower = |text| Str.from_utf8_lossy(text.to_utf8().map(lower_byte))

	is_capital : U8 -> Bool
	is_capital = |byte| byte >= 'A' and byte <= 'Z'

	is_small_letter : U8 -> Bool
	is_small_letter = |byte| byte >= 'a' and byte <= 'z'

	is_digit : U8 -> Bool
	is_digit = |byte| byte >= '0' and byte <= '9'

	## The text before the first `separator`, or all of it (Shale's
	## `split(s, separator)[1]`).
	before_first : Str, Str -> Str
	before_first = |text, separator|
		match text.split_first(separator) {
			Ok(parts) => parts.before
			Err(_) => text
		}

	## The text after the last `separator`, or all of it.
	after_last : Str, Str -> Str
	after_last = |text, separator|
		match text.split_last(separator) {
			Ok(parts) => parts.after
			Err(_) => text
		}

	## Shale's `between(text, opening, closing)`: after the first `opening`,
	## before the next `closing`.
	between : Str, Str, Str -> Try(Str, [NotFound])
	between = |text, opening, closing| {
		after_opening = text.split_first(opening)?
		inside = after_opening.after.split_first(closing)?
		Ok(inside.before)
	}

	## Shale's `basename`: the path after its last `/`.
	basename : Str -> Str
	basename = |path| Text.after_last(path, "/")

	## Shale's `dirname`: the path before its last `/`, `""` (the root) when
	## it has none.
	dirname : Str -> Str
	dirname = |path|
		match path.split_last("/") {
			Ok(parts) => parts.before
			Err(_) => ""
		}

	## Shale's `path_under`: a name below a folder, spelled as every call takes
	## it: in the root (`""`, `"."`) the name, in a mount's folder (`m:`,
	## `m:.`) `m:name`, else `dir/name`.
	path_under : Str, Str -> Str
	path_under = |folder, name|
		if folder == "" or folder == "." {
			name
		} else if folder.ends_with(":") {
			"${folder}${name}"
		} else if folder.ends_with(":.") {
			"${folder.drop_suffix(".")}${name}"
		} else {
			"${folder}/${name}"
		}

	## A name's words, lowercased: split at its underscores and before each
	## capital that follows a letter or digit, so `TiltStep` and `tilt_step`
	## both give `tilt` and `step` (cookbook/roc/declared.shale's
	## `name_words`).
	name_words : Str -> List(Str)
	name_words = |name| {
		var $words = []
		var $word = []
		for byte in name.to_utf8() {
			if byte == '_' {
				$words = with_word($words, $word)
				$word = []
			} else if Text.is_capital(byte) and !($word.is_empty()) {
				$words = with_word($words, $word)
				$word = [byte]
			} else {
				$word = $word.append(byte)
			}
		}
		with_word($words, $word)
	}

	## A text's words, as Shale's `words` splits it: the runs between spaces,
	## tabs, carriage returns and newlines, none empty.
	words : Str -> List(Str)
	words = |text| {
		var $said = []
		var $word = []
		for byte in text.to_utf8() {
			if byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n' {
				$said = with_raw_word($said, $word)
				$word = []
			} else {
				$word = $word.append(byte)
			}
		}
		with_raw_word($said, $word)
	}

	## The texts put in Shale's `sort` order, bytewise.
	sort_bytewise : List(Str) -> List(Str)
	sort_bytewise = |texts| texts.sort_with(Text.byte_order)

	## Two texts in the order of their bytes, a text before every longer one
	## it starts, as Shale's `sort` puts strings.
	byte_order : Str, Str -> [Before, Same, After]
	byte_order = |first, second| {
		pairs = List.map2(
			first.to_utf8(),
			second.to_utf8(),
			|x, y| (x, y),
		)
		match pairs.find_first(|(x, y)| x != y) {
			Ok((x, y)) =>
				if x < y {
					Before
				} else {
					After
				}
			Err(_) =>
				if first.count_utf8_bytes() < second.count_utf8_bytes() {
					Before
				} else if first.count_utf8_bytes() > second.count_utf8_bytes() {
					After
				} else {
					Same
				}
		}
	}
}

lower_byte : U8 -> U8
lower_byte = |byte|
	if Text.is_capital(byte) {
		byte + 32
	} else {
		byte
	}

## `said` with `bytes` after it as a word, as they are, when there are any.
with_raw_word : List(Str), List(U8) -> List(Str)
with_raw_word = |said, bytes|
	if bytes.is_empty() {
		said
	} else {
		said.append(Str.from_utf8_lossy(bytes))
	}

## `words` with `bytes` after them as a lowercased word, when there are any.
with_word : List(Str), List(U8) -> List(Str)
with_word = |words, bytes|
	if bytes.is_empty() {
		words
	} else {
		words.append(Text.lower(Str.from_utf8_lossy(bytes)))
	}
