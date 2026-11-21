## Text trimmed, lowered and ordered as Shale's builtins do it (`trim`,
## `lower`, `sort`, `basename`), so the recipe's output is the Shale
## original's, byte for byte. It copies part of
## cookbook/roc/find_unclear_names/Text.roc and the trimming and byte order of
## cookbook/roc/find_unused_imports/find_unused_imports.roc, on purpose, until
## the Roc recipes share a library.
Text := [].{

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

	## Shale's `basename`: the path after its last `/`.
	basename : Str -> Str
	basename = |path|
		match path.split_last("/") {
			Ok(parts) => parts.after
			Err(_) => path
		}

	## The texts put in Shale's `sort` order, bytewise.
	sort_bytewise : List(Str) -> List(Str)
	sort_bytewise = |texts| texts.sort_with(Text.byte_order)

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
}
