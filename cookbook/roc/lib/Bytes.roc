## A line's bytes read as Shale's builtins read a string: a place counted
## from 1, cut as `sub` cuts, trimmed as `trim` trims, and a width that is
## the bytes with a tab counting four. A recipe that scans Roc a byte at a
## time (Groups, Stacking) reads its lines through these, so its places and
## widths are its Shale original's.
Bytes := [].{

	## A line's width: its bytes, a tab counting four.
	width_of : List(U8) -> U64
	width_of = |bytes| bytes.len() + 3 * bytes.count_if(|byte| byte == '\t')

	## The width of a line given as text.
	line_width : Str -> U64
	line_width = |text| Bytes.width_of(text.to_utf8())

	## Line `number` of the lines, counted from 1, as bytes: empty past the end.
	line_of : List(Str), U64 -> List(U8)
	line_of = |source_lines, number|
		if number == 0 {
			[]
		} else {
			(source_lines.get(number - 1) ?? "").to_utf8()
		}

	## The byte at `place`, counted from 1, or 0 past either end.
	byte_at : List(U8), U64 -> U8
	byte_at = |bytes, place|
		if place == 0 {
			0
		} else {
			bytes.get(place - 1) ?? 0
		}

	## Shale's `sub(s, first, last)` of bytes: the places from `first` to
	## `last`, counted from 1, cut to the bytes there are.
	span : List(U8), U64, U64 -> List(U8)
	span = |bytes, first, last| {
		start = if first == 0 {
			1
		} else {
			first
		}
		end = if last < bytes.len() {
			last
		} else {
			bytes.len()
		}
		if start > end {
			[]
		} else {
			bytes.sublist({ start: start - 1, len: end - start + 1 })
		}
	}

	## Shale's `sub(s, first)`: the bytes from place `first` to the end.
	rest_from : List(U8), U64 -> List(U8)
	rest_from = |bytes, first| Bytes.span(bytes, first, bytes.len())

	## The bytes without the spaces, tabs, carriage returns and newlines at
	## their ends, as Shale's `trim` takes them.
	trimmed : List(U8) -> List(U8)
	trimmed = |bytes|
		match bytes.find_first_index(|byte| !(is_blank(byte))) {
			Ok(start) => {
				end = bytes.find_last_index(|byte| !(is_blank(byte))) ?? start
				bytes.sublist({ start, len: end + 1 - start })
			}
			Err(_) => []
		}

	## The bytes without the blanks at their end, as Shale's `trim_end`.
	trimmed_end : List(U8) -> List(U8)
	trimmed_end = |bytes|
		match bytes.find_last_index(|byte| !(is_blank(byte))) {
			Ok(end) => bytes.take_first(end + 1)
			Err(_) => []
		}

	## Whether `byte` is one of `[A-Za-z0-9_]`.
	is_word_byte : U8 -> Bool
	is_word_byte = |byte| Bytes.is_letter(byte) or Bytes.is_digit(byte) or byte == '_'

	## Whether `byte` is an ASCII letter, small or capital.
	is_letter : U8 -> Bool
	is_letter = |byte| (byte >= 'a' and byte <= 'z') or (byte >= 'A' and byte <= 'Z')

	## Whether `byte` is a digit.
	is_digit : U8 -> Bool
	is_digit = |byte| byte >= '0' and byte <= '9'

	## Whether `byte` is a small letter or `_`, which starts a lower name.
	starts_lower_name : U8 -> Bool
	starts_lower_name = |byte| (byte >= 'a' and byte <= 'z') or byte == '_'

	## The last byte before `place` that is not a space, or 0.
	byte_before : List(U8), U64 -> U8
	byte_before = |bytes, place| {
		var $at = place - 1
		while $at > 0 and Bytes.byte_at(bytes, $at) == ' ' {
			$at = $at - 1
		}
		Bytes.byte_at(bytes, $at)
	}

	## The first place at `place` or after it that is no space: the end of ` *`.
	spaces_end : List(U8), U64 -> U64
	spaces_end = |bytes, place| {
		var $end = place
		while Bytes.byte_at(bytes, $end) == ' ' {
			$end = $end + 1
		}
		$end
	}

	## The first place at `place` or after it that is no `[A-Za-z0-9_]`.
	word_end : List(U8), U64 -> U64
	word_end = |bytes, place| {
		var $end = place
		while Bytes.is_word_byte(Bytes.byte_at(bytes, $end)) {
			$end = $end + 1
		}
		$end
	}
}

## A byte Shale's `trim` takes off a text's ends.
is_blank : U8 -> Bool
is_blank = |byte| byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n'
