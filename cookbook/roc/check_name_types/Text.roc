## Text cut, trimmed, lowered and ordered as Shale's builtins do it (`lines`,
## `trim`, `lower`, `sub`, `sort`), so the recipe's output is the Shale
## original's, byte for byte. It copies parts of
## cookbook/roc/check_families/Text.roc and
## cookbook/roc/find_unclear_names/Text.roc, on purpose, until the Roc recipes
## share a library.
Text := [].{

	## A text's lines, as Shale's `lines` splits it: a last newline ends a line.
	lines : Str -> List(Str)
	lines = |text| {
		parts = text.split_on("\n")
		if text.ends_with("\n") {
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
			Err(_) => crash "check_name_types: the file has no line ${number.to_str()}"
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
	lower = |text| Str.from_utf8_lossy(text.to_utf8().map(Text.lower_byte))

	lower_byte : U8 -> U8
	lower_byte = |byte|
		if Text.is_capital(byte) {
			byte + 32
		} else {
			byte
		}

	is_capital : U8 -> Bool
	is_capital = |byte| byte >= 'A' and byte <= 'Z'

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
