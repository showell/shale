## ShaleJson is one JSON text read as Shale's json_read reads it
## (c/host/host_json.c), so a transcript line the Shale original read is
## read the same here, and a line it refused is refused here, in its words.
##
## Where Shale's values are not JSON's, it does as Shale does: a number
## with a fraction or an exponent is kept as its text; an integer past 64
## bits, a `null` in an array, a key twice in one object and a text nested
## past 64 levels are refused; a member that is `null` is left out of its
## object. `{}` and `[]` are both Shale's empty table, so `is_empty_table`
## says which values are one. A refusal is json_read's own sentence with
## its byte, line and column.
##
## Roc's builtin Json at the pin decodes into a record of known fields, and
## refuses none of what Shale refuses, so this reader is written out, held
## to what count_shale_stops reads: one value a line, decoded whole. Its
## strings are JsonString's.
import JsonString

ShaleJson := [
	Object(List((Str, ShaleJson))),
	Array(List(ShaleJson)),
	Text(Str),
	Integer(I64),
	Boolean(Bool),
	NoValue,
].{

	## The text's one value, or why json_read refuses it. `NoValue` is a
	## text that is `null`; inside a value, a `null` is left out or refused.
	read : Str -> Try(ShaleJson, Str)
	read = |text| {
		bytes = text.to_utf8()
		match value_at(bytes, { at: 0, depth_left: 64 }) {
			Ok(got) => ends_after(bytes, got).map_err(|problem| refusal(bytes, problem))
			Err(problem) => Err(refusal(bytes, problem))
		}
	}

	## Whether Shale holds the value as its empty table: `{}` or `[]`.
	is_empty_table : ShaleJson -> Bool
	is_empty_table = |value|
		match value {
			Object(members) => members.is_empty()
			Array(items) => items.is_empty()
			_ => False
		}
}

## A value read, and the byte after it.
Read : { decoded : ShaleJson, next : U64 }

## Where a value starts, and how much deeper the text may nest.
Place : { at : U64, depth_left : U64 }

## Why the text is refused, and the byte where it went wrong.
Problem : JsonString.Problem

## The value, when nothing but space comes after it.
ends_after : List(U8), Read -> Try(ShaleJson, Problem)
ends_after = |bytes, got| {
	after = skip_space(bytes, got.next)
	if after < bytes.len() {
		Err({ at: after, said: "more after the value: one text is one value" })
	} else {
		Ok(got.decoded)
	}
}

## The value at the place, after any space before it.
value_at : List(U8), Place -> Try(Read, Problem)
value_at = |bytes, place| {
	at = skip_space(bytes, place.at)
	match bytes.get(at) {
		Err(_) => Err({ at, said: "a value was expected and the text ended" })
		Ok('{') => object_at(bytes, { at, depth_left: place.depth_left })
		Ok('[') => array_at(bytes, { at, depth_left: place.depth_left })
		Ok('"') => {
			got = JsonString.read_at(bytes, at)?
			Ok({ decoded: ShaleJson.Text(got.text), next: got.next })
		}
		Ok('t') | Ok('f') | Ok('n') => literal_at(bytes, at)
		Ok(byte) =>
			if byte == '-' or is_digit(byte) {
				number_at(bytes, at)
			} else {
				unwanted = "${byte_name(byte)} (0x${JsonString.hex_pair(byte)})"
				Err({ at, said: "a value was expected, not ${unwanted}" })
			}
	}
}

## `true`, `false` or `null`, told by its first byte, spelled out whole.
literal_at : List(U8), U64 -> Try(Read, Problem)
literal_at = |bytes, at| {
	(word, meaning) =
		match bytes.get(at) {
			Ok('t') => ("true", ShaleJson.Boolean(True))
			Ok('f') => ("false", ShaleJson.Boolean(False))
			_ => ("null", ShaleJson.NoValue)
		}
	spelled = word.to_utf8()
	if bytes.sublist({ start: at, len: spelled.len() }) == spelled {
		Ok({ decoded: meaning, next: at + spelled.len() })
	} else {
		Err({ at, said: "`${word}` was expected" })
	}
}

## One level deeper, at the opening bracket, or the refusal past 64.
deeper : Place -> Try(U64, Problem)
deeper = |place|
	if place.depth_left == 0 {
		Err({ at: place.at, said: "the text nests past its depth budget" })
	} else {
		Ok(place.depth_left - 1)
	}

## The object whose `{` is at the place: its members in order, a `null`
## one left out, a key twice refused.
object_at : List(U8), Place -> Try(Read, Problem)
object_at = |bytes, place| {
	depth_left = deeper(place)?
	start = skip_space(bytes, place.at + 1)
	if bytes.get(start) == Ok('}') {
		return Ok({ decoded: ShaleJson.Object([]), next: start + 1 })
	}
	var $members = []
	var $at = start
	while True {
		key_at = skip_space(bytes, $at)
		if bytes.get(key_at) != Ok('"') {
			return Err({ at: key_at, said: "a member's name, a string, was expected" })
		}
		key = JsonString.read_at(bytes, key_at)?
		colon_at = skip_space(bytes, key.next)
		if bytes.get(colon_at) != Ok(':') {
			return Err({ at: colon_at, said: "a colon after a member's name was expected" })
		}
		member = value_at(bytes, { at: colon_at + 1, depth_left })?
		if $members.any(|(name, _)| name == key.text) {
			return Err({ at: key_at, said: "the key \"${key.text}\" is in this object twice" })
		}
		if has_value(member.decoded) {
			$members = $members.append((key.text, member.decoded))
		}
		after = skip_space(bytes, member.next)
		match bytes.get(after) {
			Err(_) => return Err({ at: after, said: "the text ended inside an object" })
			Ok(',') => {
				$at = after + 1
			}
			Ok('}') => return Ok({ decoded: ShaleJson.Object($members), next: after + 1 })
			Ok(byte) => {
				said = "a comma or `}` was expected, not ${byte_name(byte)}"
				return Err({ at: after, said })
			}
		}
	}
}

## The array whose `[` is at the place: its items in order, a `null`
## refused.
array_at : List(U8), Place -> Try(Read, Problem)
array_at = |bytes, place| {
	depth_left = deeper(place)?
	start = skip_space(bytes, place.at + 1)
	if bytes.get(start) == Ok(']') {
		return Ok({ decoded: ShaleJson.Array([]), next: start + 1 })
	}
	var $items = []
	var $at = start
	while True {
		item_at = skip_space(bytes, $at)
		item = value_at(bytes, { at: item_at, depth_left })?
		if !(has_value(item.decoded)) {
			cannot_hold = "which a Shale list cannot hold"
			said = "a null in an array, ${cannot_hold}: `{null = v}` names a value to stand for it"
			return Err({ at: item_at, said })
		}
		$items = $items.append(item.decoded)
		after = skip_space(bytes, item.next)
		match bytes.get(after) {
			Err(_) => return Err({ at: after, said: "the text ended inside an array" })
			Ok(',') => {
				$at = after + 1
			}
			Ok(']') => return Ok({ decoded: ShaleJson.Array($items), next: after + 1 })
			Ok(byte) => {
				said = "a comma or `]` was expected, not ${byte_name(byte)}"
				return Err({ at: after, said })
			}
		}
	}
}

## The number at `start`: an integer, or the text of one with a fraction
## or an exponent.
number_at : List(U8), U64 -> Try(Read, Problem)
number_at = |bytes, start| {
	digits_from = if bytes.get(start) == Ok('-') {
		start + 1
	} else {
		start
	}
	if !(digit_at(bytes, digits_from)) {
		return Err({ at: start, said: "a number needs a digit after its sign" })
	}
	if bytes.get(digits_from) == Ok('0') and digit_at(bytes, digits_from + 1) {
		return Err({ at: start, said: "a number with a leading zero" })
	}
	var $at = digits_end(bytes, digits_from)
	var $is_whole = True
	if bytes.get($at) == Ok('.') {
		$is_whole = False
		if !(digit_at(bytes, $at + 1)) {
			return Err({ at: start, said: "a number needs a digit after its point" })
		}
		$at = digits_end(bytes, $at + 1)
	}
	if bytes.get($at) == Ok('e') or bytes.get($at) == Ok('E') {
		$is_whole = False
		signed = bytes.get($at + 1) == Ok('+') or bytes.get($at + 1) == Ok('-')
		exponent_at = if signed {
			$at + 2
		} else {
			$at + 1
		}
		if !(digit_at(bytes, exponent_at)) {
			return Err({ at: start, said: "a number needs a digit in its exponent" })
		}
		$at = digits_end(bytes, exponent_at)
	}
	text = Str.from_utf8_lossy(bytes.sublist({ start, len: $at - start }))
	if !$is_whole {
		return Ok({ decoded: ShaleJson.Text(text), next: $at })
	}
	match I64.from_str(text) {
		Ok(number) => Ok({ decoded: ShaleJson.Integer(number), next: $at })
		Err(_) => Err({ at: start, said: "the integer ${text} is past 64 bits" })
	}
}

## Whether the value read is one Shale holds: a `null` is none.
has_value : ShaleJson -> Bool
has_value = |value|
	match value {
		NoValue => False
		_ => True
	}

digit_at : List(U8), U64 -> Bool
digit_at = |bytes, at|
	match bytes.get(at) {
		Ok(byte) => is_digit(byte)
		Err(_) => False
	}

## Where the run of digits from `at` ends.
digits_end : List(U8), U64 -> U64
digits_end = |bytes, from| {
	var $at = from
	while digit_at(bytes, $at) {
		$at = $at + 1
	}
	$at
}

is_digit : U8 -> Bool
is_digit = |byte| byte >= '0' and byte <= '9'

## Past the space JSON allows between tokens.
skip_space : List(U8), U64 -> U64
skip_space = |bytes, from| {
	var $at = from
	while is_space(bytes.get($at)) {
		$at = $at + 1
	}
	$at
}

is_space : Try(U8, [OutOfBounds]) -> Bool
is_space = |got|
	match got {
		Ok(' ') | Ok('\t') | Ok('\n') | Ok('\r') => True
		_ => False
	}

## How json_read names a byte it did not want.
byte_name : U8 -> Str
byte_name = |byte|
	match byte {
		'{' => "`{`"
		'}' => "`}`"
		'[' => "`[`"
		']' => "`]`"
		',' => "a comma"
		':' => "a colon"
		'"' => "a quote"
		_ => "a byte"
	}

## json_read's refusal: why, then where, by byte (from 0) and by line and
## column (each byte before it a column, a newline a line).
refusal : List(U8), Problem -> Str
refusal = |bytes, { at, said }| {
	before = bytes.take_first(at)
	line = before.count_if(|byte| byte == '\n') + 1
	column =
		match before.find_last_index(|byte| byte == '\n') {
			Ok(last_newline) => at - last_newline
			Err(_) => at + 1
		}
	location = "at byte ${at.to_str()} (line ${line.to_str()}, column ${column.to_str()})"
	"`json_read`: ${said} ${location}"
}
