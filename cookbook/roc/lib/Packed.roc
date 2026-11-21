## The groups that stay packed whatever the stacking rule says, as
## cookbook/roc/groups.shale reads them: a row of literals, read at a glance,
## and numeric data, such as a polygon's points. A number is a literal or a
## negative written `(0.0 - 0.5)`; a flat number record is a record of
## numbers, `{ x: 0.5, y: (0.0 - 0.25) }`; and a number list is a list of two
## or more numbers or flat number records. The original's regular
## expressions are matched here by hand, a byte at a time, as a backtracker
## would match them.
import Bytes
import Groups

Packed := [].{

	## Whether a group on its line is a row of literals, read at a glance: a list,
	## or a tuple (a `(` not right after a name, `)` or `!`, so not a call's
	## arguments), whose items are all numbers, strings with nothing interpolated,
	## characters or bare tags, on a line within `width`.
	is_literal_row : { bytes : List(U8), group : Groups.Group }, U64 -> Bool
	is_literal_row = |at, width| {
		bytes = at.bytes
		group = at.group
		if bytes.len() > width or Bytes.width_of(bytes) > width {
			return Bool.False
		}
		opening = Bytes.byte_at(bytes, group.open)
		if opening == '(' {
			b = Bytes.byte_before(bytes, group.open)
			if group.open > 1 and Bytes.byte_at(
				bytes,
				group.open - 1,
			) != ' ' and (Bytes.is_word_byte(b) or b == ')' or b == '!') {
				return Bool.False
			}
		} else if opening != '[' {
			return Bool.False
		}
		inside = Bytes.trimmed(Bytes.span(bytes, group.open + 1, group.close - 1))
		pieces_of(inside, ',').all(
			|piece| {
				item = Bytes.trimmed(piece)
				item.is_empty() or is_literal_item(item)
			},
		)
	}

	## Whether the bytes hold a number list.
	holds_number_list : List(U8) -> Bool
	holds_number_list = |bytes| {
		zeroed = numbers_as_zero(bytes)
		var $at = 1
		while $at <= zeroed.len() {
			if Bytes.byte_at(zeroed, $at) == '[' and number_list_end(zeroed, $at) != Err(NoMatch) {
				return Bool.True
			}
			$at = $at + 1
		}
		Bool.False
	}

	## Whether a group on its line is numeric data: a number list, or a flat
	## number record on a line that holds a number list, as a point among a
	## polygon's points is.
	is_number_data : List(U8), Groups.Group -> Bool
	is_number_data = |bytes, group| {
		text = numbers_as_zero(Bytes.span(bytes, group.open, group.close))
		if number_list_end(text, 1) == Ok(text.len() + 1) {
			Bool.True
		} else {
			text == ['0'] and Packed.holds_number_list(bytes)
		}
	}
}

## The bytes split at each `separator`, as Shale's `split`: an empty text is
## one empty piece.
pieces_of : List(U8), U8 -> List(List(U8))
pieces_of = |bytes, separator| {
	var $pieces = []
	var $piece = []
	for byte in bytes {
		if byte == separator {
			$pieces = $pieces.append($piece)
			$piece = []
		} else {
			$piece = $piece.append(byte)
		}
	}
	$pieces.append($piece)
}

## Whether an item is a literal: `-?[0-9][0-9A-Za-z_.]*`, `"[^"\\$]*"`,
## `'[^']+'` or a bare tag `[A-Z][A-Za-z0-9]*`.
is_literal_item : List(U8) -> Bool
is_literal_item = |item| {
	count = item.len()
	first = Bytes.byte_at(item, 1)
	last = Bytes.byte_at(item, count)
	middle = Bytes.span(item, 2, count - 1)
	if number_end(item, 1) == Ok(count + 1) {
		Bool.True
	} else if first == '"' {
		is_kept_out = |byte| byte == '"' or byte == '\\' or byte == '$'
		count >= 2 and last == '"' and !(middle.any(is_kept_out))
	} else if first == '\'' {
		count >= 3 and last == '\'' and !(middle.contains('\''))
	} else {
		is_tag_byte = |byte| Bytes.is_letter(byte) or Bytes.is_digit(byte)
		first >= 'A' and first <= 'Z' and item.drop_first(1).all(is_tag_byte)
	}
}

## Whether a byte may follow a number's first digit: `[0-9A-Za-z_.]`.
is_number_byte : U8 -> Bool
is_number_byte = |byte| Bytes.is_word_byte(byte) or byte == '.'

## Where a number `-?[0-9][0-9A-Za-z_.]*` at `place` ends, one past it.
number_end : List(U8), U64 -> Try(U64, [NoMatch])
number_end = |bytes, place| {
	start = if Bytes.byte_at(bytes, place) == '-' {
		place + 1
	} else {
		place
	}
	unsigned_end(bytes, start)
}

## Where a number with no sign, `[0-9][0-9A-Za-z_.]*`, at `place` ends.
unsigned_end : List(U8), U64 -> Try(U64, [NoMatch])
unsigned_end = |bytes, place|
	if Bytes.is_digit(Bytes.byte_at(bytes, place)) {
		var $end = place + 1
		while is_number_byte(Bytes.byte_at(bytes, $end)) {
			$end = $end + 1
		}
		Ok($end)
	} else {
		Err(NoMatch)
	}

## Where a negative written `(0.0 - 0.5)` at `place` ends:
## `\(-?[0-9][0-9A-Za-z_.]* - [0-9][0-9A-Za-z_.]*\)`.
negative_end : List(U8), U64 -> Try(U64, [NoMatch])
negative_end = |bytes, place| {
	if Bytes.byte_at(bytes, place) != '(' {
		return Err(NoMatch)
	}
	first = number_end(bytes, place + 1)?
	if Bytes.span(bytes, first, first + 2) != [' ', '-', ' '] {
		return Err(NoMatch)
	}
	second = unsigned_end(bytes, first + 3)?
	if Bytes.byte_at(bytes, second) == ')' {
		Ok(second + 1)
	} else {
		Err(NoMatch)
	}
}

## Where a field and its number, `[a-z_][A-Za-z0-9_]*: *-?[0-9]...`, at
## `place` end.
number_field_end : List(U8), U64 -> Try(U64, [NoMatch])
number_field_end = |bytes, place| {
	first = Bytes.byte_at(bytes, place)
	if !(Bytes.starts_lower_name(first)) {
		return Err(NoMatch)
	}
	end = Bytes.word_end(bytes, place + 1)
	if Bytes.byte_at(bytes, end) != ':' {
		return Err(NoMatch)
	}
	number_end(bytes, Bytes.spaces_end(bytes, end + 1))
}

## The items a list of numbers or a record of them holds.
ItemKind : [Number, NumberField]

## Where a run of ` *, *` and an item of the kind given, from place `from`,
## ends.
next_item_end : List(U8), { from : U64, kind : ItemKind } -> Try(U64, [NoMatch])
next_item_end = |bytes, at| {
	comma = Bytes.spaces_end(bytes, at.from)
	if Bytes.byte_at(bytes, comma) != ',' {
		return Err(NoMatch)
	}
	item_start = Bytes.spaces_end(bytes, comma + 1)
	match at.kind {
		Number => number_end(bytes, item_start)
		NumberField => number_field_end(bytes, item_start)
	}
}

## Where a group's end, ` *,? *` and its closing bracket, from place `from`,
## ends.
group_close_end : List(U8), { from : U64, closer : U8 } -> Try(U64, [NoMatch])
group_close_end = |bytes, at| {
	var $end = Bytes.spaces_end(bytes, at.from)
	if Bytes.byte_at(bytes, $end) == ',' {
		$end = Bytes.spaces_end(bytes, $end + 1)
	}
	if Bytes.byte_at(bytes, $end) == at.closer {
		Ok($end + 1)
	} else {
		Err(NoMatch)
	}
}

## Where a flat number record, `{ x: 0.5, y: 0 }`, at `place` ends.
number_record_end : List(U8), U64 -> Try(U64, [NoMatch])
number_record_end = |bytes, place| {
	if Bytes.byte_at(bytes, place) != '{' {
		return Err(NoMatch)
	}
	var $end = number_field_end(bytes, Bytes.spaces_end(bytes, place + 1))?
	var $more = Bool.True
	while $more {
		match next_item_end(bytes, { from: $end, kind: NumberField }) {
			Ok(end) => {
				$end = end
			}
			Err(_) => {
				$more = Bool.False
			}
		}
	}
	group_close_end(bytes, { from: $end, closer: '}' })
}

## Where a number list, `[` two or more numbers `]`, at `place` ends.
number_list_end : List(U8), U64 -> Try(U64, [NoMatch])
number_list_end = |bytes, place| {
	if Bytes.byte_at(bytes, place) != '[' {
		return Err(NoMatch)
	}
	first = number_end(bytes, Bytes.spaces_end(bytes, place + 1))?
	var $end = next_item_end(bytes, { from: first, kind: Number })?
	var $more = Bool.True
	while $more {
		match next_item_end(bytes, { from: $end, kind: Number }) {
			Ok(end) => {
				$end = end
			}
			Err(_) => {
				$more = Bool.False
			}
		}
	}
	group_close_end(bytes, { from: $end, closer: ']' })
}

## The bytes with each match of `pattern_end` replaced by `0`, leftmost
## first, as Shale's `re_replace` replaces.
with_matches_zeroed : List(U8), (List(U8), U64 -> Try(U64, [NoMatch])) -> List(U8)
with_matches_zeroed = |bytes, pattern_end| {
	var $out = []
	var $at = 1
	while $at <= bytes.len() {
		match pattern_end(bytes, $at) {
			Ok(end) => {
				$out = $out.append('0')
				$at = end
			}
			Err(_) => {
				$out = $out.append(Bytes.byte_at(bytes, $at))
				$at = $at + 1
			}
		}
	}
	$out
}

## The bytes with each negative number and each flat number record read as
## the number 0, so that a list of them reads as a list of numbers.
numbers_as_zero : List(U8) -> List(U8)
numbers_as_zero = |bytes| with_matches_zeroed(
	with_matches_zeroed(bytes, negative_end),
	number_record_end,
)
