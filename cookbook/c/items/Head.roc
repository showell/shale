## The readers of an item's opening line or head, each over masked bytes
## (Mask.masked), each the regular expression of cookbook/c/items.shale,
## the Shale original, done by hand: the one match a backtracking search
## would find, with its groups.
import Mask

Head := [].{

	## A head read as a function's, its return type and its name, or
	## `NoFunction` when it declares none (a variable, a function pointer, a
	## value whose `=` comes before its first `(`). Its storage and
	## `STATUS_MUST_USE` go first, then a leading `__attribute__((...))`; the
	## type is what is left before the name, `void` among them.
	function_head : List(U8) -> Try({ return_type : Str, name : Str }, [NoFunction])
	function_head = |code| {
		after_storage = code.drop_first(storage_end(code))
		head = after_storage.drop_first(attribute_end(after_storage))
		match Mask.byte_from(head, '(', 0) {
			Err(_) => Err(NoFunction)
			Ok(open) => {
				is_value = match Mask.byte_from(head, '=', 0) {
					Ok(equals) => equals < open
					Err(_) => Bool.False
				}
				before = Mask.trimmed(head.take_first(open))
				# `^(.*[\s*])([A-Za-z_]\w*)$`: the name is what follows the last
				# space or `*`.
				match before.find_last_index(|byte| Mask.is_space(byte) or byte == '*') {
					Ok(cut) if !(is_value) => {
						name = before.drop_first(cut + 1)
						return_type = Mask.trimmed(before.take_first(cut + 1))
						if is_name(name) and !(return_type.is_empty()) {
							Ok({ return_type: Mask.text(return_type), name: Mask.text(name) })
						} else {
							Err(NoFunction)
						}
					}
					_ => Err(NoFunction)
				}
			}
		}
	}

	## A `VEC_DEFINE(`, `MAP_DEFINE(` or `SLICE_TYPE(` line read as its macro,
	## the name it makes and the type it holds, or `NoGenerator` when it is
	## none or its arguments are not a name and a type:
	## `^(VEC_DEFINE|MAP_DEFINE|SLICE_TYPE)\(\s*(\w+)\s*,\s*([^)]*\S)\s*\)`.
	generator : List(U8) -> Try({ macro : Str, name : Str, held_type : Str }, [NoGenerator])
	generator = |code| {
		macros = ["VEC_DEFINE", "MAP_DEFINE", "SLICE_TYPE"]
		match macros.find_first(|macro| Mask.holds_at(code, 0, "${macro}(")) {
			Err(_) => Err(NoGenerator)
			Ok(macro) => {
				name_start = Mask.spaces_end(code, macro.count_utf8_bytes() + 1)
				name_end = Mask.word_end(code, name_start)
				comma = Mask.spaces_end(code, name_end)
				if name_end == name_start or code.get(comma) != Ok(',') {
					Err(NoGenerator)
				} else {
					held_start = Mask.spaces_end(code, comma + 1)
					match held_type_end(code, held_start) {
						Ok(held_end) => {
							name = Mask.text(code.sublist({ start: name_start, len: name_end - name_start }))
							held_type = Mask.text(code.sublist({ start: held_start, len: held_end + 1 - held_start }))
							Ok({ macro, name, held_type })
						}
						Err(_) => Err(NoGenerator)
					}
				}
			}
		}
	}

	## The name a data item's head declares: a function pointer's `(*name)`,
	## else the last word before its `=`, `[`, `;` or `{`, or "" for none.
	data_name : List(U8) -> Str
	data_name = |code|
		match Head.pointer_name(code, WithConst) {
			Ok(name) => name
			Err(_) => {
				is_stop = |byte| byte == '=' or byte == '[' or byte == ';' or byte == '{'
				head = match code.find_first_index(is_stop) {
					Ok(stop) => code.take_first(stop)
					Err(_) => code
				}
				trailing_word(Mask.trimmed(head))
			}
		}

	## The first function pointer's name in the text, `(*name)`, a `const`
	## before the name allowed when asked: `\(\s*\*\s*(const\s+)?(\w+)\s*\)`.
	pointer_name : List(U8), [WithConst, WithoutConst] -> Try(Str, [NoPointer])
	pointer_name = |code, consts| {
		var $at = 0
		while $at < code.len() {
			if code.get($at) == Ok('(') {
				star = Mask.spaces_end(code, $at + 1)
				if code.get(star) == Ok('*') {
					after = Mask.spaces_end(code, star + 1)
					is_const = consts == WithConst and Mask.holds_at(code, after, "const") and Mask.is_space_at(code, after + 5)
					with_const = if is_const {
						closed_word(code, Mask.spaces_end(code, after + 5))
					} else {
						Err(NoPointer)
					}
					match with_const {
						Ok(name) => return Ok(name)
						Err(_) =>
							match closed_word(code, after) {
								Ok(name) => return Ok(name)
								Err(_) => {}
							}
					}
				}
			}
			$at = $at + 1
		}
		Err(NoPointer)
	}

	## The type keyword a `typedef` or `enum` line opens with, after any
	## `typedef`: `^(typedef\s+)?(struct|enum|union)\b`.
	type_keyword : List(U8) -> [Struct, Enum, Union, NoKeyword]
	type_keyword = |code| {
		at = if Mask.holds_at(code, 0, "typedef") and Mask.is_space_at(code, 7) {
			Mask.spaces_end(code, 7)
		} else {
			0
		}
		is_keyword = |word| Mask.holds_at(code, at, word) and !(Mask.is_word_at(code, at + word.count_utf8_bytes()))
		if is_keyword("struct") {
			Struct
		} else if is_keyword("enum") {
			Enum
		} else if is_keyword("union") {
			Union
		} else {
			NoKeyword
		}
	}

	## The name a type's masked text declares last, before its closing `;`:
	## `(\w+)\s*(\[[^\]]*\])?\s*;\s*$`.
	declared_name : List(U8) -> Try(Str, [NoName])
	declared_name = |text| {
		match text.find_last_index(|byte| !(Mask.is_space(byte))) {
			Ok(semicolon) if text.get(semicolon) == Ok(';') =>
				match last_unspaced(text, semicolon) {
					Ok(before) if text.get(before) == Ok(']') => bracketed_name(text, before)
					Ok(before) if Mask.is_word_at(text, before) => Ok(word_ending_at(text, before))
					_ => Err(NoName)
				}
			_ => Err(NoName)
		}
	}

	## A named `enum`'s tag: `^enum\s+(\w+)`.
	enum_tag : List(U8) -> Try(Str, [NoTag])
	enum_tag = |code| {
		start = Mask.spaces_end(code, 4)
		end = Mask.word_end(code, start)
		if Mask.holds_at(code, 0, "enum") and Mask.is_space_at(code, 4) and end > start {
			Ok(Mask.text(code.sublist({ start, len: end - start })))
		} else {
			Err(NoTag)
		}
	}

	## A `#` line's directive and its first argument, "" for none:
	## `^#\s*(\w+)\s*(\w*)`.
	directive : List(U8) -> Try({ word : Str, argument : Str }, [NoDirective])
	directive = |code| {
		word_start = Mask.spaces_end(code, 1)
		word_end = Mask.word_end(code, word_start)
		argument_start = Mask.spaces_end(code, word_end)
		argument_end = Mask.word_end(code, argument_start)
		if code.get(0) == Ok('#') and word_end > word_start {
			word = Mask.text(code.sublist({ start: word_start, len: word_end - word_start }))
			argument = Mask.text(code.sublist({ start: argument_start, len: argument_end - argument_start }))
			Ok({ word, argument })
		} else {
			Err(NoDirective)
		}
	}

	## An `#include` line's path, from the raw line: `^#\s*include\s*["<]([^">]+)[">]`.
	include_path : List(U8) -> Try(Str, [NoPath])
	include_path = |raw| {
		keyword = Mask.spaces_end(raw, 1)
		open = Mask.spaces_end(raw, keyword + 7)
		is_open = raw.get(open) == Ok('"') or raw.get(open) == Ok('<')
		is_close = |byte| byte == '"' or byte == '>'
		is_framed = raw.get(0) == Ok('#') and Mask.holds_at(raw, keyword, "include") and is_open
		match raw.drop_first(open + 1).find_first_index(is_close) {
			Ok(length) if is_framed and length > 0 => Ok(Mask.text(raw.sublist({ start: open + 1, len: length })))
			_ => Err(NoPath)
		}
	}

	## A `TEST(name) {` line's name: `^TEST\(\s*(\w+)\s*\)\s*\{\s*$`.
	test_name : List(U8) -> Try(Str, [NoTest])
	test_name = |code| {
		start = Mask.spaces_end(code, 5)
		end = Mask.word_end(code, start)
		close = Mask.spaces_end(code, end)
		brace = Mask.spaces_end(code, close + 1)
		is_test =
			Mask.holds_at(code, 0, "TEST(")
				and end > start
				and code.get(close) == Ok(')')
				and code.get(brace) == Ok('{')
				and Mask.spaces_end(code, brace + 1) == code.len()
		if is_test {
			Ok(Mask.text(code.sublist({ start, len: end - start })))
		} else {
			Err(NoTest)
		}
	}

	## A test list's macro and its first argument, "" for none:
	## `^(\w+)\(\s*(\w*)`, of a line that starts with the macro and `(`.
	test_list : List(U8) -> { macro : Str, argument : Str }
	test_list = |code| {
		macro_end = Mask.word_end(code, 0)
		argument_start = Mask.spaces_end(code, macro_end + 1)
		argument_end = Mask.word_end(code, argument_start)
		{
			macro: Mask.text(code.take_first(macro_end)),
			argument: Mask.text(code.sublist({ start: argument_start, len: argument_end - argument_start })),
		}
	}
}

## Where `^static\s+(inline\s+)?(STATUS_MUST_USE\s+)?` ends, or 0.
storage_end : List(U8) -> U64
storage_end = |code|
	if Mask.holds_at(code, 0, "static") and Mask.is_space_at(code, 6) {
		after_static = Mask.spaces_end(code, 6)
		after_inline = word_and_spaces_end(code, after_static, "inline")
		word_and_spaces_end(code, after_inline, "STATUS_MUST_USE")
	} else {
		0
	}

## Past `word\s+` at `at`, or `at` when it is not there.
word_and_spaces_end : List(U8), U64, Str -> U64
word_and_spaces_end = |code, at, word| {
	after = at + word.count_utf8_bytes()
	if Mask.holds_at(code, at, word) and Mask.is_space_at(code, after) {
		Mask.spaces_end(code, after)
	} else {
		at
	}
}

## Where `^__attribute__\(\([\w\s,]*\)\)\s+` ends, or 0.
attribute_end : List(U8) -> U64
attribute_end = |code| {
	is_inside = |byte| Mask.is_word(byte) or Mask.is_space(byte) or byte == ','
	inside_start = 15
	inside_end = match code.drop_first(inside_start).find_first_index(|byte| !(is_inside(byte))) {
		Ok(length) => inside_start + length
		Err(_) => code.len()
	}
	is_attribute =
		Mask.holds_at(code, 0, "__attribute__((")
			and Mask.holds_at(code, inside_end, "))")
			and Mask.is_space_at(code, inside_end + 2)
	if is_attribute {
		Mask.spaces_end(code, inside_end + 2)
	} else {
		0
	}
}

## Whether the bytes are a C name: `[A-Za-z_]\w*`, whole.
is_name : List(U8) -> Bool
is_name = |bytes|
	match bytes.first() {
		Ok(first) => Mask.is_name_start(first) and bytes.all(Mask.is_word)
		Err(_) => Bool.False
	}

## The last byte of a generator's held type, from `start`: `([^)]*\S)\s*\)`,
## the longest run of no `)` whose last byte is no space and after which a
## `)` follows, past any spaces.
held_type_end : List(U8), U64 -> Try(U64, [NoType])
held_type_end = |code, start|
	match Mask.byte_from(code, ')', start) {
		Err(_) => Err(NoType)
		Ok(first_close) => {
			var $end = first_close
			while $end >= start {
				closes = code.get(Mask.spaces_end(code, $end + 1)) == Ok(')')
				if !(Mask.is_space_at(code, $end)) and closes {
					return Ok($end)
				}
				if $end == 0 {
					return Err(NoType)
				}
				$end = $end - 1
			}
			Err(NoType)
		}
	}

## The word at `at`, when a `)` follows it past any spaces: `(\w+)\s*\)`.
closed_word : List(U8), U64 -> Try(Str, [NoPointer])
closed_word = |code, at| {
	end = Mask.word_end(code, at)
	if end > at and code.get(Mask.spaces_end(code, end)) == Ok(')') {
		Ok(Mask.text(code.sublist({ start: at, len: end - at })))
	} else {
		Err(NoPointer)
	}
}

## The `\w` run that ends the bytes, "" for none: `(\w+)$`.
trailing_word : List(U8) -> Str
trailing_word = |bytes|
	match bytes.find_last_index(|byte| !(Mask.is_word(byte))) {
		Ok(last_other) => Mask.text(bytes.drop_first(last_other + 1))
		Err(_) => Mask.text(bytes)
	}

## The last place before `end` that is no `\s`.
last_unspaced : List(U8), U64 -> Try(U64, [Missing])
last_unspaced = |bytes, end|
	match bytes.take_first(end).find_last_index(|byte| !(Mask.is_space(byte))) {
		Ok(at) => Ok(at)
		Err(_) => Err(Missing)
	}

## The `\w` run that ends at `last`, whole.
word_ending_at : List(U8), U64 -> Str
word_ending_at = |bytes, last| {
	before = bytes.take_first(last + 1)
	trailing_word(before)
}

## The name before a `[...]` that closes at `close`: the leftmost `[` of the
## brackets (no `]` between it and `close`) with a word before it, past any
## spaces, is the match a leftmost search finds.
bracketed_name : List(U8), U64 -> Try(Str, [NoName])
bracketed_name = |text, close| {
	var $opens = []
	var $at = close
	var $is_open = Bool.True
	while $is_open and $at > 0 {
		$at = $at - 1
		byte = text.get($at) ?? 0
		if byte == ']' {
			$is_open = Bool.False
		} else if byte == '[' {
			$opens = $opens.prepend($at)
		}
	}
	for open in $opens {
		match last_unspaced(text, open) {
			Ok(before) if Mask.is_word_at(text, before) => return Ok(word_ending_at(text, before))
			_ => {}
		}
	}
	Err(NoName)
}
