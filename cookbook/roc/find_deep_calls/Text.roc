## Text trimmed and ordered as Shale's builtins do it (`trim`, `sort`), so the
## recipe's output is the Shale original's, byte for byte. Both are
## cookbook/roc/find_unused_imports/find_unused_imports.roc's, copied until
## the recipes share a library.
Text := [].{

	## The text without the spaces, tabs, carriage returns and newlines at its
	## ends, as Shale's `trim` takes them.
	trim_ends : Str -> Str
	trim_ends = |text| {
		bytes = text.to_utf8()
		is_kept = |byte| !(byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n')
		match bytes.find_first_index(is_kept) {
			Ok(start) => {
				end = bytes.find_last_index(is_kept) ?? start
				Str.from_utf8(bytes.sublist({ start, len: end + 1 - start })) ?? text
			}
			Err(_) => ""
		}
	}

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
