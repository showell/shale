## Text cut, trimmed and ordered as Shale's builtins and preludes do it
## (`lines`, `trim`, `sort`, `basename`, `dirname`, `path_under`), so the
## recipe's output is the Shale original's, byte for byte. It copies the
## trimming and byte order of cookbook/roc/check_families/Text.roc and the
## lines of check_families.roc, on purpose, until the Roc recipes share a
## library.
Text := [].{

	## A text's lines, as Shale's `lines` splits it: a last newline ends a
	## line, and an empty text has none.
	lines : Str -> List(Str)
	lines = |text| {
		parts = text.split_on("\n")
		if text.ends_with("\n") or text == "" {
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
		is_kept = |byte| !(byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n')
		match bytes.find_first_index(is_kept) {
			Ok(start) => {
				end = bytes.find_last_index(is_kept) ?? start
				Str.from_utf8_lossy(bytes.sublist({ start, len: end + 1 - start }))
			}
			Err(_) => ""
		}
	}

	## Shale's `basename`: the path after its last `/`.
	basename : Str -> Str
	basename = |path|
		match path.split_last("/") {
			Ok(parts) => parts.after
			Err(_) => path
		}

	## Shale's `dirname`: the path before its last `/`, `""` (the root) when
	## it has none.
	dirname : Str -> Str
	dirname = |path|
		match path.split_last("/") {
			Ok(parts) => parts.before
			Err(_) => ""
		}

	## Shale's `path_under`: a name below a folder, spelled as every call takes
	## it: in the root (`""`, `"."`) the name, in a mount's folder (`m:`,
	## `m:.`) `m:name`, else `dir/name`.
	path_under : Str, Str -> Str
	path_under = |folder, name|
		if folder == "" or folder == "." {
			name
		} else if folder.ends_with(":") {
			"${folder}${name}"
		} else if folder.ends_with(":.") {
			"${folder.drop_suffix(".")}${name}"
		} else {
			"${folder}/${name}"
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
