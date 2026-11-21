## The words and the cases of a C name, as hunt_names reads them: the
## regular expressions of cookbook/c/hunt_names.shale, the Shale original,
## done by hand over the name's bytes. A name of Shale's C is ASCII, and a
## byte past it is in no class here.
Names := [].{

	## A C name's words, lower case: `NodeInfo`, `node_info` and `NODE_INFO`
	## all `[node, info]`. A `_` goes between a lower-case letter or a digit
	## and the capital after it (`([a-z0-9])([A-Z])`), then the name is cut at
	## each `_`, an empty word left out.
	words_of : Str -> List(Str)
	words_of = |name| {
		bytes = name.to_utf8()
		var $spelled = []
		$at : U64
		var $at = 0
		while $at < bytes.len() {
			byte = bytes.get($at) ?? 0
			$spelled = $spelled.append(lowered(byte))
			next = bytes.get($at + 1) ?? 0
			if (is_lower(byte) or is_digit(byte)) and is_upper(next) {
				$spelled = $spelled.append('_')
			}
			$at = $at + 1
		}
		$spelled.split_on('_').keep_if(|word| !(word.is_empty())).map(|word| Str.from_utf8_lossy(word))
	}

	## Whether the name is `^[A-Z][A-Za-z0-9]*$`: a type's PascalCase.
	is_pascal_case : Str -> Bool
	is_pascal_case = |name|
		is_shaped(name, is_upper, |byte| is_upper(byte) or is_lower(byte) or is_digit(byte))

	## Whether the name is `^[a-z][a-z0-9_]*$`: a function's snake_case.
	is_snake_case : Str -> Bool
	is_snake_case = |name|
		is_shaped(name, is_lower, |byte| is_lower(byte) or is_digit(byte) or byte == '_')

	## Whether the name is `^[A-Z][A-Z0-9_]*$`: a constant's UPPER_SNAKE.
	is_upper_snake : Str -> Bool
	is_upper_snake = |name|
		is_shaped(name, is_upper, |byte| is_upper(byte) or is_digit(byte) or byte == '_')
}

## Whether the name's first byte is `first`'s and every byte after it is
## `rest`'s, an empty name being neither.
is_shaped : Str, (U8 -> Bool), (U8 -> Bool) -> Bool
is_shaped = |name, first, rest| {
	bytes = name.to_utf8()
	match bytes.first() {
		Ok(byte) => first(byte) and bytes.drop_first(1).all(rest)
		Err(_) => Bool.False
	}
}

is_upper : U8 -> Bool
is_upper = |byte| byte >= 'A' and byte <= 'Z'

is_lower : U8 -> Bool
is_lower = |byte| byte >= 'a' and byte <= 'z'

is_digit : U8 -> Bool
is_digit = |byte| byte >= '0' and byte <= '9'

## The byte lower case, as Shale's `lower` makes it: ASCII alone.
lowered : U8 -> U8
lowered = |byte|
	if is_upper(byte) {
		byte + 32
	} else {
		byte
	}
