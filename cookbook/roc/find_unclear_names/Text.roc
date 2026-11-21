## Text cut as Shale's builtins cut it (`lines`, `split`, `between`,
## `dirname`), and bytes ordered as Shale's `sort` puts strings, so the
## recipe's output is the Shale original's, byte for byte.
Text := [].{

	## A text's lines, as Shale's `lines` splits it: a last newline ends a line.
	lines : Str -> List(Str)
	lines = |text| {
		parts = Str.split_on(text, "\n")
		if Str.ends_with(text, "\n") {
			List.drop_last(parts, 1)
		} else {
			parts
		}
	}

	## Line `k` of `src`, counted from 1, or `""` past its end.
	line_at : List(Str), U64 -> Str
	line_at = |src, k|
		if k == 0 {
			""
		} else {
			List.get(src, k - 1) ?? ""
		}

	## The text before the first `sep`, or all of it (Shale's `split(s, sep)[1]`).
	before_first : Str, Str -> Str
	before_first = |s, sep|
		match Str.split_first(s, sep) {
			Ok(parts) => parts.before
			Err(_) => s
		}

	## The text after the last `sep`, or all of it.
	after_last : Str, Str -> Str
	after_last = |s, sep|
		match Str.split_last(s, sep) {
			Ok(parts) => parts.after
			Err(_) => s
		}

	## Shale's `between(s, a, b)`: after the first `a`, before the next `b`.
	between : Str, Str, Str -> Try(Str, [NotFound])
	between = |s, a, b| {
		first = Str.split_first(s, a)?
		second = Str.split_first(first.after, b)?
		Ok(second.before)
	}

	## Shale's `dirname`: the path up to its last `/`, or `""`.
	dirname : Str -> Str
	dirname = |path|
		match Str.split_last(path, "/") {
			Ok(parts) => parts.before
			Err(_) => ""
		}

	basename : Str -> Str
	basename = |path| Text.after_last(path, "/")

	byte_count : Str -> U64
	byte_count = |s| List.len(Str.to_utf8(s))

	## The bytes of `s` from place `from` (counted from 0), `n` of them, as text.
	bytes_of : Str, U64, U64 -> Str
	bytes_of = |s, from, n| Str.from_utf8_lossy(List.sublist(Str.to_utf8(s), { start: from, len: n }))

	is_upper : U8 -> Bool
	is_upper = |b| b >= 65 and b <= 90

	is_lower : U8 -> Bool
	is_lower = |b| b >= 97 and b <= 122

	is_digit : U8 -> Bool
	is_digit = |b| b >= 48 and b <= 57

	lower_bytes : List(U8) -> Str
	lower_bytes = |bytes| Str.from_utf8_lossy(
		bytes.map(
			|b| if Text.is_upper(b) {
				b + 32
			} else {
				b
			},
		),
	)

	## Bytewise order, as Shale's `sort` puts strings.
	byte_order : Str, Str -> [Before, Same, After]
	byte_order = |a, b| {
		x = Str.to_utf8(a)
		y = Str.to_utf8(b)
		var $k = 0
		var $answer = Same
		var $going = Bool.True
		while $going {
			match (List.get(x, $k), List.get(y, $k)) {
				(Ok(p), Ok(q)) =>
					if p < q {
						$answer = Before
						$going = Bool.False
					} else if p > q {
						$answer = After
						$going = Bool.False
					} else {
						$k = $k + 1
					}
				(Err(_), Ok(_)) => {
					$answer = Before
					$going = Bool.False
				}
				(Ok(_), Err(_)) => {
					$answer = After
					$going = Bool.False
				}
				(Err(_), Err(_)) => {
					$going = Bool.False
				}
			}
		}
		$answer
	}
}
