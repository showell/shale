## The masker the splitter reads Shale's C with, and the byte classes it
## reads by. A line is its bytes, as Shale's strings are, so a column is a
## byte's place. The classes are the regular expressions' and the builtins'
## of c/build/c_items.shale, the Shale original: `\s` is a space, a tab, a
## newline, a carriage return, a vertical tab or a form feed; `\w` an ASCII
## letter, a digit or `_`; `trim` takes a space, a tab, a carriage return
## and a newline.
Mask := [].{

	## The line with its strings, character literals and comments blanked,
	## each to spaces between its quotes (a comment wholly to spaces), so every
	## column stays where it was. The quotes go first, leftmost first, so a
	## `"` in `'"'` is the literal's and a `//` or a `/*` left after them is a
	## comment's; a quote that nothing closes is left as it is.
	masked : List(U8) -> List(U8)
	masked = |line|
		if line.contains('"') or line.contains('\'') or line.contains('/') {
			masked_slowly(line)
		} else {
			line
		}

	## A regular expression's `\s`.
	is_space : U8 -> Bool
	is_space = |byte|
		byte == ' ' or byte == '\t' or byte == '\n' or byte == '\r' or byte == 0x0b or byte == 0x0c

	## A regular expression's `\w`: an ASCII letter, a digit or `_`.
	is_word : U8 -> Bool
	is_word = |byte|
		Mask.is_name_start(byte) or (byte >= '0' and byte <= '9')

	## The first byte of a C name: an ASCII letter or `_`.
	is_name_start : U8 -> Bool
	is_name_start = |byte|
		(byte >= 'a' and byte <= 'z') or (byte >= 'A' and byte <= 'Z') or byte == '_'

	## Whether the byte at `at` is a `\w`, past the end being none.
	is_word_at : List(U8), U64 -> Bool
	is_word_at = |bytes, at|
		match bytes.get(at) {
			Ok(byte) => Mask.is_word(byte)
			Err(_) => Bool.False
		}

	## Whether the byte at `at` is a `\s`, past the end being none.
	is_space_at : List(U8), U64 -> Bool
	is_space_at = |bytes, at|
		match bytes.get(at) {
			Ok(byte) => Mask.is_space(byte)
			Err(_) => Bool.False
		}

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

	## The first place at `at` or after it that is no `\s`: the end of `\s*`.
	spaces_end : List(U8), U64 -> U64
	spaces_end = |bytes, at| {
		var $end = at
		while Mask.is_space_at(bytes, $end) {
			$end = $end + 1
		}
		$end
	}

	## The first place at `at` or after it that is no `\w`: the end of `\w*`.
	word_end : List(U8), U64 -> U64
	word_end = |bytes, at| {
		var $end = at
		while Mask.is_word_at(bytes, $end) {
			$end = $end + 1
		}
		$end
	}

	## Whether the bytes hold the text at `at`.
	holds_at : List(U8), U64, Str -> Bool
	holds_at = |bytes, at, wanted_text| {
		wanted = wanted_text.to_utf8()
		bytes.sublist({ start: at, len: wanted.len() }) == wanted
	}

	## The place of the first two bytes `pair` at `from` or after it.
	pair_from : List(U8), (U8, U8), U64 -> Try(U64, [Missing])
	pair_from = |bytes, (first, second), from| {
		var $at = from
		while $at + 1 < bytes.len() {
			match Mask.byte_from(bytes, first, $at) {
				Ok(found) if bytes.get(found + 1) == Ok(second) => return Ok(found)
				Ok(found) => {
					$at = found + 1
				}
				Err(_) => return Err(Missing)
			}
		}
		Err(Missing)
	}

	## The place of the first `byte` at `from` or after it.
	byte_from : List(U8), U8, U64 -> Try(U64, [Missing])
	byte_from = |bytes, byte, from|
		match bytes.drop_first(from).find_first_index(|each| each == byte) {
			Ok(offset) => Ok(from + offset)
			Err(_) => Err(Missing)
		}

	## The bytes as text: Shale's C is UTF-8, and a mask cuts only at ASCII.
	text : List(U8) -> Str
	text = |bytes| Str.from_utf8_lossy(bytes)
}

## The masker's work on a line that holds a quote or a `/`.
masked_slowly : List(U8) -> List(U8)
masked_slowly = |line| {
	has_double = line.contains('"')
	has_single = line.contains('\'')
	quotes_blanked =
		if has_double or has_single {
			blank_quotes(line, { double: has_double, single: has_single })
		} else {
			line
		}
	comments_blanked =
		if Mask.pair_from(quotes_blanked, ('/', '*'), 0) != Err(Missing) {
			blank_block_comments(quotes_blanked)
		} else {
			quotes_blanked
		}
	match Mask.pair_from(comments_blanked, ('/', '/'), 0) {
		Ok(cut) => comments_blanked.take_first(cut).concat(List.repeat(' ', comments_blanked.len() - cut))
		Err(_) => comments_blanked
	}
}

## A byte Shale's `trim` takes off a text's ends.
is_blank : U8 -> Bool
is_blank = |byte| byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n'

## The line with each string (`double`) or character literal (`single`)
## blanked between its quotes, leftmost first: a quote opens one, `\` and the
## byte after it are inside it, and the same quote closes it. One that the
## line does not close is no literal, and the search goes on after its quote.
blank_quotes : List(U8), { double : Bool, single : Bool } -> List(U8)
blank_quotes = |line, kinds| {
	count = line.len()
	var $out = line
	var $at = 0
	while $at < count {
		byte = line.get($at) ?? 0
		is_open = (byte == '"' and kinds.double) or (byte == '\'' and kinds.single)
		closed = if is_open {
			quote_close(line, $at, byte)
		} else {
			Err(Unclosed)
		}
		match closed {
			Ok(close) => {
				$out = blanked($out, { start: $at + 1, end: close })
				$at = close + 1
			}
			Err(_) => {
				$at = $at + 1
			}
		}
	}
	$out
}

## Where the literal opened by `quote` at `open` closes: `[^"\\]*(?:\\.[^"\\]*)*"`.
quote_close : List(U8), U64, U8 -> Try(U64, [Unclosed])
quote_close = |line, open, quote| {
	count = line.len()
	var $at = open + 1
	while $at < count {
		byte = line.get($at) ?? 0
		if byte == quote {
			return Ok($at)
		}
		if byte == '\\' {
			# `\\.`: the escape takes any byte but a newline after it.
			if $at + 1 >= count or line.get($at + 1) == Ok('\n') {
				return Err(Unclosed)
			}
			$at = $at + 2
		} else {
			$at = $at + 1
		}
	}
	Err(Unclosed)
}

## The line with each `/* */` comment wholly spaces: `/\*[^*]*\*/`, so a
## comment that holds a `*` of its own is none.
blank_block_comments : List(U8) -> List(U8)
blank_block_comments = |line| {
	var $out = line
	var $at = 0
	while $at + 1 < line.len() {
		if line.get($at) == Ok('/') and line.get($at + 1) == Ok('*') {
			match Mask.byte_from(line, '*', $at + 2) {
				Ok(star) if line.get(star + 1) == Ok('/') => {
					$out = blanked($out, { start: $at, end: star + 2 })
					$at = star + 2
				}
				_ => {
					$at = $at + 1
				}
			}
		} else {
			$at = $at + 1
		}
	}
	$out
}

## The bytes with those from `start` up to `end` made spaces.
blanked : List(U8), { start : U64, end : U64 } -> List(U8)
blanked = |bytes, { start, end }|
	bytes.take_first(start).concat(List.repeat(' ', end - start)).concat(bytes.drop_first(end))
