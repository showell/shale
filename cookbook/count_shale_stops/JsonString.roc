## JsonString reads one JSON string as Shale's json_read reads it
## (c/host/host_json.c): its escapes decoded to UTF-8, a `\u` high
## surrogate taking the low one after it, and the four hex digits of a `\u`
## read as Zig's parseInt reads them. A refusal says why and at which byte,
## for ShaleJson to put as json_read's sentence.
JsonString := [].{

	## Why a text is refused, and the byte where it went wrong.
	Problem : { at : U64, said : Str }

	## The string whose opening quote is at `open`, its escapes decoded, and
	## the byte after its closing quote. A string with no escape is its own
	## bytes; one with escapes is built a stretch at a time.
	read_at : List(U8), U64 -> Try({ text : Str, next : U64 }, JsonString.Problem)
	read_at = |bytes, open| {
		var $at = open + 1
		var $copied_to = $at
		var $decoded = []
		var $has_escape = False
		while True {
			match bytes.get($at) {
				Err(_) => return Err({ at: open, said: "a string that never closes" })
				Ok('"') => {
					stretch = { start: $copied_to, len: $at - $copied_to }
					content =
						if $has_escape {
							$decoded.append_sublist(bytes, stretch)
						} else {
							bytes.sublist(stretch)
						}
					return match Str.from_utf8(content) {
						Ok(text) => Ok({ text, next: $at + 1 })
						Err(_) => Err({ at: open, said: "a string that is not UTF-8" })
					}
				}
				Ok('\\') => {
					escape = escape_at(bytes, $at)?
					stretch = { start: $copied_to, len: $at - $copied_to }
					$decoded = $decoded.append_sublist(bytes, stretch).concat(escape.decoded)
					$has_escape = True
					$at = escape.next
					$copied_to = $at
				}
				Ok(byte) =>
					if byte < 0x20 {
						written = "which JSON writes as an escape"
						code = JsonString.hex_pair(byte)
						said = "a control byte (0x${code}) inside a string, ${written}"
						return Err({ at: $at, said })
					} else {
						$at = $at + 1
					}
			}
		}
	}

	## A byte as two lowercase hex digits.
	hex_pair : U8 -> Str
	hex_pair = |byte| Str.from_utf8_lossy([hex_digit_byte(byte // 16), hex_digit_byte(byte % 16)])
}

## A string's bytes decoded from an escape, and the byte after it.
Escape : { decoded : List(U8), next : U64 }

## The escape whose backslash is at `at`. A `\u` high surrogate takes the
## low one after it.
escape_at : List(U8), U64 -> Try(Escape, JsonString.Problem)
escape_at = |bytes, at| {
	plain = |byte| Ok({ decoded: [byte], next: at + 2 })
	match bytes.get(at + 1) {
		Err(_) => Err({ at, said: "a string that ends in a backslash" })
		Ok('"') => plain('"')
		Ok('\\') => plain('\\')
		Ok('/') => plain('/')
		Ok('b') => plain(8)
		Ok('f') => plain(12)
		Ok('n') => plain('\n')
		Ok('r') => plain('\r')
		Ok('t') => plain('\t')
		Ok('u') => unicode_escape_at(bytes, at)
		Ok(byte) => {
			escaped = Str.from_utf8_lossy([byte])
			Err({ at, said: "the escape `\\${escaped}`, which JSON has not" })
		}
	}
}

## A `\u` escape at `at`, with the low surrogate's own escape after a high
## one, as UTF-8.
unicode_escape_at : List(U8), U64 -> Try(Escape, JsonString.Problem)
unicode_escape_at = |bytes, at| {
	high = hex_four(bytes, at)?
	after = at + 6
	if high >= 0xD800 and high <= 0xDBFF {
		if bytes.sublist({ start: after, len: 2 }) != ['\\', 'u'] {
			return Err({ at, said: "a high surrogate with no low one after it" })
		}
		low = hex_four(bytes, after)?
		if low < 0xDC00 or low > 0xDFFF {
			return Err({ at: after, said: "a high surrogate followed by no low one" })
		}
		code_point = 0x10000 + (high - 0xD800) * 0x400 + (low - 0xDC00)
		Ok({ decoded: utf8_of(code_point), next: after + 6 })
	} else if high >= 0xDC00 and high <= 0xDFFF {
		Err({ at, said: "a low surrogate with no high one before it" })
	} else {
		Ok({ decoded: utf8_of(high), next: after })
	}
}

## The four bytes after the `\u` at `at`, read as Zig's
## `std.fmt.parseInt(u16, s, 16)` reads them, as Shale's reader does: a
## `+` or `-` may lead (a `-` only before zero), and `_` may stand between
## digits.
hex_four : List(U8), U64 -> Try(U32, JsonString.Problem)
hex_four = |bytes, at| {
	refused = Err({ at, said: "`\\u` needs four hex digits" })
	four = bytes.sublist({ start: at + 2, len: 4 })
	if four.len() < 4 {
		return refused
	}
	is_negative = four.first() == Ok('-')
	digits =
		if is_negative or four.first() == Ok('+') {
			four.drop_first(1)
		} else {
			four
		}
	if digits.is_empty() or digits.first() == Ok('_') or digits.last() == Ok('_') {
		return refused
	}
	$code_point : U32
	var $code_point = 0
	for byte in digits {
		if byte != '_' {
			match hex_digit(byte) {
				Ok(digit) => {
					$code_point = $code_point * 16 + digit
				}
				Err(_) => return refused
			}
		}
	}
	if is_negative and $code_point != 0 {
		return refused
	}
	Ok($code_point)
}

hex_digit : U8 -> Try(U32, [NotHex])
hex_digit = |byte|
	if byte >= '0' and byte <= '9' {
		Ok((byte - '0').to_u32())
	} else if byte >= 'a' and byte <= 'f' {
		Ok((byte - 'a' + 10).to_u32())
	} else if byte >= 'A' and byte <= 'F' {
		Ok((byte - 'A' + 10).to_u32())
	} else {
		Err(NotHex)
	}

## The lowercase hex digit for a value under 16.
hex_digit_byte : U8 -> U8
hex_digit_byte = |value|
	if value < 10 {
		'0' + value
	} else {
		'a' + value - 10
	}

## A code point's UTF-8 bytes: its bits six at a time, after a lead byte
## that says how many follow.
utf8_of : U32 -> List(U8)
utf8_of = |code_point| {
	if code_point < 0x80 {
		[code_point.to_u8_wrap()]
	} else if code_point < 0x800 {
		[(0xC0 + code_point // 0x40).to_u8_wrap(), low_six(code_point)]
	} else if code_point < 0x10000 {
		[
			(0xE0 + code_point // 0x1000).to_u8_wrap(),
			low_six(code_point // 0x40),
			low_six(code_point),
		]
	} else {
		[
			(0xF0 + code_point // 0x40000).to_u8_wrap(),
			low_six(code_point // 0x1000),
			low_six(code_point // 0x40),
			low_six(code_point),
		]
	}
}

## A continuation byte: the low six bits of the code point shifted down.
low_six : U32 -> U8
low_six = |shifted| (0x80 + shifted % 0x40).to_u8_wrap()
