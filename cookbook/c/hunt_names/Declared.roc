## The names a function declares, as hunt_names reads them from its masked
## lines: its parameters, from its head, and a body line's local. Each is a
## regular expression of cookbook/c/hunt_names.shale, the Shale original,
## done by hand, its alternatives tried in the order the expression's own
## matcher tries them, so the first match is the original's.
import items.Mask

Declared := [].{

	## The names a function's head declares as its parameters: each part's last
	## word, between the head's first `(` and its last `)`, `void` none. A
	## part's name is `(\w+)\s*(\[[^\]]*\])?\s*$`: a word, then at most one
	## `[...]`, at the part's end, so a function pointer's part has none.
	parameters : List(U8) -> List(Str)
	parameters = |head| {
		open = head.find_first_index(|byte| byte == '(')
		close = head.find_last_index(|byte| byte == ')')
		match (open, close) {
			(Ok(start), Ok(end)) if end > start => {
				inside = head.sublist({ start: start + 1, len: end - start - 1 })
				inside
					.split_on(',')
					.keep_oks(parameter_name)
					.keep_if(|name| name != "void")
			}
			_ => []
		}
	}

	## A body line's declared local, from its masked code: `Type name =`,
	## `Type *name;`, `const Type name[`, as the original's
	## `^\s*(?:const\s+|static\s+|struct\s+|unsigned\s+)*([A-Za-z_]\w*)(?:\s*\*+\s*|\s+)(?:const\s+)?\*?([a-z_]\w*)\s*(?:=|;|\[)`
	## matches it. A first word that is no type (`return x;`) declares
	## nothing.
	local : List(U8) -> Try(Str, [NoLocal])
	local = |code| {
		starts = keyword_starts(code, [Mask.spaces_end(code, 0)])
		# The `*` takes the most keywords first, then gives them back one by one.
		var $left = starts.len()
		while $left > 0 {
			$left = $left - 1
			match declared_from(code, starts.get($left) ?? 0) {
				Ok({ type_word, name }) => {
					if not_types.contains(type_word) {
						return Err(NoLocal)
					}
					return Ok(name)
				}
				Err(_) => {}
			}
		}
		Err(NoLocal)
	}
}

## A declaration's first word that is no type: `return x;` declares nothing.
not_types : List(Str)
not_types = ["return", "else", "case", "goto", "do", "sizeof", "typedef", "break", "continue"]

## The words a declaration may start with before its type, each with the
## spaces after it.
type_keywords : List(Str)
type_keywords = ["const", "static", "struct", "unsigned"]

## The bytes from `start` to `end` as text.
text_of : List(U8), U64, U64 -> Str
text_of = |bytes, start, end| Mask.text(bytes.sublist({ start, len: end - start }))

## A part of the parameters' name: the first run of `\w` whose rest is
## `\s*(\[[^\]]*\])?\s*$`. Every start inside one run leaves the same rest,
## so the leftmost match is a whole run.
parameter_name : List(U8) -> Try(Str, [NoName])
parameter_name = |part| {
	$at : U64
	var $at = 0
	while $at < part.len() {
		end = Mask.word_end(part, $at)
		if end == $at {
			$at = $at + 1
		} else if ends_part(part, end) {
			return Ok(text_of(part, $at, end))
		} else {
			$at = end
		}
	}
	Err(NoName)
}

## Whether what follows `at` is `\s*(\[[^\]]*\])?\s*$`: spaces, at most one
## `[...]`, spaces, and the part's end.
ends_part : List(U8), U64 -> Bool
ends_part = |part, at| {
	after = Mask.spaces_end(part, at)
	if after == part.len() {
		Bool.True
	} else if part.get(after) == Ok('[') {
		match Mask.byte_from(part, ']', after + 1) {
			Ok(close) => Mask.spaces_end(part, close + 1) == part.len()
			Err(_) => Bool.False
		}
	} else {
		Bool.False
	}
}

## Where the type word may start: `starts`, the first after the leading
## spaces, then after each keyword of `type_keywords` and its spaces, as
## many as follow one another.
keyword_starts : List(U8), List(U64) -> List(U64)
keyword_starts = |code, starts| {
	at = starts.last() ?? 0
	match type_keywords.find_first(|keyword| Mask.holds_at(code, at, keyword) and Mask.is_space_at(code, at + keyword.count_utf8_bytes())) {
		Ok(keyword) => keyword_starts(code, starts.append(Mask.spaces_end(code, at + keyword.count_utf8_bytes())))
		Err(_) => starts
	}
}

## The type word at `start` and the name after it: `([A-Za-z_]\w*)`, then
## `\s*\*+\s*` (tried first) or `\s+`, then the name.
declared_from : List(U8), U64 -> Try({ type_word : Str, name : Str }, [NoLocal])
declared_from = |code, start| {
	if !(Mask.is_name_start(code.get(start) ?? 0)) {
		return Err(NoLocal)
	}
	type_end = Mask.word_end(code, start)
	before_stars = Mask.spaces_end(code, type_end)
	with_stars =
		if code.get(before_stars) == Ok('*') {
			[Mask.spaces_end(code, stars_end(code, before_stars))]
		} else {
			[]
		}
	with_spaces =
		if Mask.is_space_at(code, type_end) {
			[Mask.spaces_end(code, type_end)]
		} else {
			[]
		}
	match with_stars.concat(with_spaces).keep_oks(|at| name_after(code, at)).first() {
		Ok(name) => Ok({ type_word: text_of(code, start, type_end), name })
		Err(_) => Err(NoLocal)
	}
}

## The first place at `at` or after it that is no `*`.
stars_end : List(U8), U64 -> U64
stars_end = |code, at| {
	var $end = at
	while code.get($end) == Ok('*') {
		$end = $end + 1
	}
	$end
}

## The name after a declaration's type and its stars:
## `(?:const\s+)?\*?([a-z_]\w*)\s*(?:=|;|\[)`, each optional part taken
## first, then left.
name_after : List(U8), U64 -> Try(Str, [NoLocal])
name_after = |code, at| {
	keyword_end = at + 5
	after_const =
		if Mask.holds_at(code, at, "const") and Mask.is_space_at(code, keyword_end) {
			[Mask.spaces_end(code, keyword_end)]
		} else {
			[]
		}
	starts = after_const.concat([at]).fold(
		[],
		|tried, place|
			if code.get(place) == Ok('*') {
				tried.concat([place + 1, place])
			} else {
				tried.append(place)
			},
	)
	match starts.keep_oks(|start| name_at(code, start)).first() {
		Ok(name) => Ok(name)
		Err(_) => Err(NoLocal)
	}
}

## The name at `start`, `[a-z_]\w*`, when `\s*` then `=`, `;` or `[`
## follows it.
name_at : List(U8), U64 -> Try(Str, [NoLocal])
name_at = |code, start| {
	first = code.get(start) ?? 0
	if !((first >= 'a' and first <= 'z') or first == '_') {
		return Err(NoLocal)
	}
	end = Mask.word_end(code, start)
	match code.get(Mask.spaces_end(code, end)) {
		Ok(byte) if byte == '=' or byte == ';' or byte == '[' => Ok(text_of(code, start, end))
		_ => Err(NoLocal)
	}
}
