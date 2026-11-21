## Text cut, trimmed and ordered as Shale's builtins do it (`lines`, `trim`,
## `sort`), and the words of names and comments as
## cookbook/roc/declared.shale reads them (`name_words`, `comment_on_line`),
## so the recipe's output is the Shale original's, byte for byte. Its
## trimming and byte order copy cookbook/roc/check_families/Text.roc, on
## purpose, until the Roc recipes share a library.
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

	## The text without the spaces, tabs, carriage returns and newlines at its
	## ends, as Shale's `trim` takes them.
	trim : Str -> Str
	trim = |text| {
		bytes = text.to_utf8()
		is_kept = |byte| !(is_space(byte))
		match bytes.find_first_index(is_kept) {
			Ok(start) => {
				end = bytes.find_last_index(is_kept) ?? start
				Str.from_utf8_lossy(bytes.sublist({ start, len: end + 1 - start }))
			}
			Err(_) => ""
		}
	}

	## Two texts in the order of their bytes, a text before every longer one
	## it starts.
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

	## The runs of letters, digits and underscores in the bytes, left to
	## right (the Shale original's `[A-Za-z0-9_]+`, every match).
	name_runs : List(U8) -> List(List(U8))
	name_runs = |bytes| {
		var $runs = []
		var $run = []
		for byte in bytes {
			if is_name_byte(byte) {
				$run = $run.append(byte)
			} else if !($run.is_empty()) {
				$runs = $runs.append($run)
				$run = []
			}
		}
		if $run.is_empty() {
			$runs
		} else {
			$runs.append($run)
		}
	}

	## A name's words, lowercased: split at its underscores and before each
	## capital that follows a letter or digit, so `TiltStep` and `tilt_step`
	## both give `tilt` and `step` (declared.shale's `name_words`).
	name_words : List(U8) -> List(Str)
	name_words = |name| {
		var $words = []
		var $word = []
		for byte in name {
			if byte == '_' {
				if !($word.is_empty()) {
					$words = $words.append(lowered($word))
				}
				$word = []
			} else if is_capital(byte) and !($word.is_empty()) {
				$words = $words.append(lowered($word))
				$word = [byte]
			} else {
				$word = $word.append(byte)
			}
		}
		if $word.is_empty() {
			$words
		} else {
			$words.append(lowered($word))
		}
	}

	## The bytes after the `#` that starts the line's comment, or `NoComment`.
	## A `#` inside a string or a character starts none, and a line of a `\\`
	## multiline string has no comment at all (declared.shale's
	## `comment_on_line`).
	comment_on_line : Str -> [Comment(List(U8)), NoComment]
	comment_on_line = |line| {
		bytes = line.to_utf8()
		if !(bytes.contains('#')) {
			return NoComment
		}
		count = bytes.len()
		start = bytes.find_first_index(|byte| !(is_space(byte))) ?? count
		if (bytes.get(start) ?? 0) == '\\' and (bytes.get(start + 1) ?? 0) == '\\' {
			return NoComment
		}
		var $at = 0
		while $at < count {
			byte = bytes.get($at) ?? 0
			if byte == '"' or byte == '\'' {
				$at = $at + 1
				while $at < count and (bytes.get($at) ?? 0) != byte {
					if (bytes.get($at) ?? 0) == '\\' {
						$at = $at + 1
					}
					$at = $at + 1
				}
			} else if byte == '#' {
				return Comment(bytes.drop_first($at + 1))
			}
			$at = $at + 1
		}
		NoComment
	}
}

## A byte Shale's `trim` takes off a text's ends: a space, a tab, a
## carriage return or a newline.
is_space : U8 -> Bool
is_space = |byte| byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n'

is_name_byte : U8 -> Bool
is_name_byte = |byte|
	is_capital(byte) or (byte >= 'a' and byte <= 'z') or (byte >= '0' and byte <= '9') or byte == '_'

is_capital : U8 -> Bool
is_capital = |byte| byte >= 'A' and byte <= 'Z'

## The bytes as text, their ASCII capitals made small, as Shale's `lower`
## does.
lowered : List(U8) -> Str
lowered = |bytes|
	Str.from_utf8_lossy(
		bytes.map(
			|byte|
				if is_capital(byte) {
					byte + 32
				} else {
					byte
				},
		),
	)
