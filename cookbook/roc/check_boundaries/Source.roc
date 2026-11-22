## The source text the checks compare: a node's text squeezed onto one line,
## a call's argument texts slot by slot and the texts its calls agree on, a
## branch's text without its braces, and whether a text is a literal.
import lib.Bytes
import lib.Forest
import lib.Text
import References

Source := [].{

	## What a slot of a function's calls holds: the text every call gives it,
	## or `Differs` where the calls differ.
	Slot : [Agreed(Str), Differs]

	## The node's source text, squeezed onto one line, `?` for a node with no
	## span. `lines_of` is each file's lines; a node's end column is one past
	## its last byte.
	text_of : Dict(Str, List(Str)), Forest, U64 -> Str
	text_of = |lines_of, forest, at| {
		node = Forest.node(forest, at)
		source_lines = lines_of.get(Forest.path(forest, at)) ?? []
		line_bytes = |number| Bytes.line_of(source_lines, number)
		before_end = if node.end_column == 0 {
			0
		} else {
			node.end_column - 1
		}
		if node.line == 0 {
			"?"
		} else if node.line == node.end_line {
			Str.from_utf8_lossy(Bytes.span(line_bytes(node.line), node.column, before_end))
		} else {
			var $parts = [Bytes.trimmed(Bytes.rest_from(line_bytes(node.line), node.column))]
			var $number = node.line + 1
			while $number < node.end_line {
				$parts = $parts.append(Bytes.trimmed(line_bytes($number)))
				$number = $number + 1
			}
			$parts = $parts.append(Bytes.trimmed(Bytes.span(line_bytes(node.end_line), 1, before_end)))
			Str.join_with($parts.map(Str.from_utf8_lossy), " ")
		}
	}

	## A call's argument texts by slot (`1`, `2`, ...), the receiver first. A
	## record argument gives its fields instead, as `N.field`, so that
	## `{ needle, at: 0 }` is judged field by field.
	argument_texts : Dict(Str, List(Str)), Forest, References.Call -> Dict(Str, Str)
	argument_texts = |lines_of, forest, call| {
		arguments = if call.receiver != 0 {
			[call.receiver].concat(call.args)
		} else {
			call.args
		}
		var $texts = Dict.empty()
		$slot : U64
		var $slot = 0
		for argument in arguments {
			$slot = $slot + 1
			if Forest.kind(forest, argument) == "e-record" {
				for field in Forest.kids(forest, argument) {
					field_name = if Forest.kind(forest, field) == "field" {
						Forest.attribute(forest, field, "field")
					} else {
						""
					}
					if field_name != "" {
						value = Forest.kid(forest, field, 1)
						text = if value != 0 {
							Source.text_of(lines_of, forest, value)
						} else {
							field_name
						}
						$texts = $texts.insert("${$slot.to_str()}.${field_name}", text)
					}
				}
			} else {
				$texts = $texts.insert($slot.to_str(), Source.text_of(lines_of, forest, argument))
			}
		}
		$texts
	}

	## For each slot of `calls`, the text every call gives it, or `Differs`.
	agreed : Dict(Str, List(Str)), Forest, List(References.Call) -> Dict(Str, Source.Slot)
	agreed = |lines_of, forest, calls| {
		var $agreed = Dict.empty()
		var $is_first = Bool.True
		for call in calls {
			texts = Source.argument_texts(lines_of, forest, call)
			if $is_first {
				for (slot, text) in texts.to_list() {
					$agreed = $agreed.insert(slot, Agreed(text))
				}
				$is_first = Bool.False
			} else {
				for (slot, was) in $agreed.to_list() {
					match was {
						Agreed(text) =>
							if texts.get(slot) != Ok(text) {
								$agreed = $agreed.insert(slot, Differs)
							}
						Differs => {}
					}
				}
				for (slot, _) in texts.to_list() {
					if !($agreed.contains(slot)) {
						$agreed = $agreed.insert(slot, Differs)
					}
				}
			}
		}
		$agreed
	}

	## The text of one branch of an `if`, its braces left out when it holds
	## one statement.
	branch_text : Dict(Str, List(Str)), Forest, U64 -> Str
	branch_text = |lines_of, forest, at| {
		is_one = |kid| Forest.kind(forest, kid) == "statements" and Forest.kids(forest, kid).len() == 1
		if Forest.kind(forest, at) == "e-block" {
			match Forest.kids(forest, at).find_first(is_one) {
				Ok(statements) => Source.text_of(lines_of, forest, Forest.kid(forest, statements, 1))
				Err(_) => Source.text_of(lines_of, forest, at)
			}
		} else {
			Source.text_of(lines_of, forest, at)
		}
	}

	## Whether a text is a literal: a number, a string, a character, a tag
	## (`Begin`) or a module's constant (`Limits.max`).
	is_literal : Str -> Bool
	is_literal = |text| {
		bytes = text.to_utf8()
		first = bytes.first() ?? 0
		second = bytes.get(1) ?? 0
		if first == '"' or first == '\'' or Text.is_digit(first) or (first == '-' and Text.is_digit(second)) {
			Bool.True
		} else if !(Text.is_capital(first)) {
			Bool.False
		} else {
			is_name_byte = |byte| Bytes.is_letter(byte) or Text.is_digit(byte)
			after = bytes.drop_first(1)
			rest = after.drop_first(after.find_first_index(|byte| !(is_name_byte(byte))) ?? after.len())
			is_lower_byte = |byte| Text.is_small_letter(byte) or Text.is_digit(byte) or byte == '_'
			starts_constant = (rest.first() ?? 0) == '.' and Bytes.starts_lower_name(rest.get(1) ?? 0)
			rest.is_empty() or (starts_constant and rest.drop_first(2).all(is_lower_byte))
		}
	}
}
