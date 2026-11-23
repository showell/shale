## A function type's bare arguments, which no trailing comma can stack, read
## as the Shale original read them: an annotation (`name : A, B, C -> R`) or a
## type alias of a function (`Pick(a) : a, a, a -> a`) to its first arrow,
## over the lines `roc fmt` wraps it onto, and a record type's field whose
## type is a function (`{ on_pick : U8, U8, U8 -> U8 }`). The finding says
## what to do instead: a record for values that travel together, or a tag
## for a run of flags.
import lib.Bytes
import lib.Finding
import lib.Groups
import lib.Text

Annotations := [].{

	## An annotation's arguments: how many, its type's text up to the arrow
	## and that text shortened, the annotated name, and the annotation's last
	## line read.
	Annotation : {
		items : U64,
		shown : Str,
		text : Str,
		name : Str,
		last : U64,
	}

	## A record type's field whose type is a function: its name, how many
	## arguments, and the type's text up to the arrow.
	FieldFunction : {
		name : Str,
		items : U64,
		text : Str,
	}

	## Text cut to 60 bytes for a finding line, its end `...`.
	shortened : Str -> Str
	shortened = |text| {
		bytes = text.to_utf8()
		if bytes.len() > 60 {
			Str.from_utf8_lossy(bytes.take_first(57).concat(['.', '.', '.']))
		} else {
			text
		}
	}

	## The annotation line `number` of the lines starts, when it annotates a
	## function: its arguments before the first arrow outside any bracket,
	## counted by the commas outside any bracket. It goes on to the next line
	## while its text so far is empty, a bracket is open or its text ends in a
	## comma, passing over a comment on a line of its own; a blank line, or a
	## next line that starts another annotation, ends it.
	annotation_arguments : List(Str), U64 -> Try(Annotations.Annotation, [NoFunction])
	annotation_arguments = |source_lines, number| {
		first_line = Bytes.line_of(source_lines, number)
		var $piece = match start_text(type_line(first_line)) {
			Ok(piece) => piece
			Err(NoStart) => return Err(NoFunction)
		}
		name = Bytes.trimmed(before_colon(Bytes.trimmed(first_line)))
		var $type_text = []
		$depth : I64
		var $depth = 0
		$commas : U64
		var $commas = 0
		var $n = number
		while $n <= source_lines.len() {
			start = $type_text.len()
			$type_text = if start == 0 or $piece.is_empty() {
				$type_text.concat($piece)
			} else {
				$type_text.append(' ').concat($piece)
			}
			var $at = start + 1
			while $at <= $type_text.len() {
				c = Bytes.byte_at($type_text, $at)
				if Groups.closer_of(c) != 0 {
					$depth = $depth + 1
				}
				if Groups.is_closer(c) {
					$depth = $depth - 1
				}
				if $depth < 0 {
					return Err(NoFunction)
				}
				if $depth == 0 and c == ',' {
					$commas = $commas + 1
				}
				if $depth == 0 and is_arrow_at($type_text, $at) {
					text = Str.from_utf8_lossy(Bytes.trimmed(Bytes.span($type_text, 1, $at - 1)))
					return Ok({
						items: $commas + 1,
						shown: Annotations.shortened(text),
						text,
						name: Str.from_utf8_lossy(name),
						last: $n,
					})
				}
				$at = $at + 1
			}
			goes_on = $type_text.is_empty() or $depth > 0 or $type_text.last() == Ok(',')
			if !(goes_on) {
				return Err(NoFunction)
			}
			# The next line of code, past any comment on a line of its own.
			$n = $n + 1
			while $n <= source_lines.len() and !(
				Bytes.trimmed(
					Bytes.line_of(source_lines, $n),
				).is_empty()
			) and type_line(Bytes.line_of(source_lines, $n)).is_empty() {
				$n = $n + 1
			}
			if $n > source_lines.len() or Bytes.trimmed(
				Bytes.line_of(source_lines, $n),
			).is_empty() {
				return Err(NoFunction)
			}
			next_line = type_line(Bytes.line_of(source_lines, $n))
			if $depth == 0 and start_text(next_line) != Err(NoStart) {
				return Err(NoFunction)
			}
			$piece = next_line
		}
		Err(NoFunction)
	}

	## The function types among the fields of a record type whose inside,
	## between its braces, is `inner`: a field whose type has an arrow outside
	## any bracket, its arguments counted by the commas before it. A comma
	## starts the next field only when `name : ` follows it, so the commas of a
	## field's function type stay its own.
	record_field_functions : Str -> List(Annotations.FieldFunction)
	record_field_functions = |text| {
		inner = text.to_utf8()
		if !(starts_field(inner)) {
			return []
		}
		var $out = []
		$depth : I64
		var $depth = 0
		$commas : U64
		var $commas = 0
		var $type_start = 0
		var $field_start = 1
		var $name = []
		var $done = Bool.False
		var $at = 1
		while $at <= inner.len() {
			c = Bytes.byte_at(inner, $at)
			if Groups.closer_of(c) != 0 {
				$depth = $depth + 1
			}
			if Groups.is_closer(c) {
				$depth = $depth - 1
			}
			if $depth == 0 {
				if $type_start == 0 and Bytes.span(inner, $at, $at + 2) == [' ', ':', ' '] {
					$type_start = $at + 3
					$name = Bytes.trimmed(Bytes.span(inner, $field_start, $at - 1))
					$at = $at + 2
				} else if c == ',' and starts_field(Bytes.rest_from(inner, $at + 1)) {
					$commas = 0
					$type_start = 0
					$field_start = $at + 1
					$done = Bool.False
				} else if c == ',' {
					$commas = $commas + 1
				} else if !($done) and $type_start > 0 and Bytes.byte_at(
					inner,
					$at - 1,
				) == ' ' and is_arrow_at(inner, $at) {
					type_text = Bytes.trimmed(Bytes.span(inner, $type_start, $at - 1))
					$out = $out.append({
						name: Str.from_utf8_lossy($name),
						items: $commas + 1,
						text: Str.from_utf8_lossy(type_text),
					})
					$done = Bool.True
				}
			}
			$at = $at + 1
		}
		$out
	}

	## Whether every comma-separated item of a text is `Bool` or a Bool
	## literal, like `Bool, Bool, Bool` or `Bool.true, Bool.false, Bool.true`.
	are_all_flags : Str -> Bool
	are_all_flags = |text|
		text.split_on(",").all(
			|item| {
				flag = Text.trim(item)
				flag == "Bool" or flag == "Bool.true" or flag == "Bool.false" or flag == "Bool.True" or flag == "Bool.False"
			},
		)

	## What a flag's finding adds.
	flag_advice : Str
	flag_advice = ": a flag a caller passes is a tag, not a Bool (roc-apps docs/roc-style.md)"

	## The finding for a function type's bare arguments at a file's line.
	function_type_finding : { path : Str, line : U64 },
	{
		items : U64,
		shown : Str,
		text : Str,
	} -> Finding
	function_type_finding = |place, arguments| {
		advice = if Annotations.are_all_flags(arguments.text) {
			Annotations.flag_advice
		} else {
			": take a record (roc-apps docs/roc-style.md, Name what travels together)"
		}
		{
			path: place.path,
			line: place.line,
			check: "stack",
			what: "${arguments.items.to_str()} bare arguments in ${arguments.shown}, which no comma stacks${advice}",
		}
	}
}

## A line without a `#` comment, trimmed. A type has no string, so the first
## `#` starts the comment.
type_line : List(U8) -> List(U8)
type_line = |bytes|
	match bytes.find_first_index(|byte| byte == '#') {
		Ok(at) => Bytes.trimmed(bytes.take_first(at))
		Err(_) => Bytes.trimmed(bytes)
	}

## The text of a trimmed line before its first ` :`, or all of it.
before_colon : List(U8) -> List(U8)
before_colon = |bytes| {
	var $at = 1
	while $at < bytes.len() {
		if Bytes.byte_at(bytes, $at) == ' ' and Bytes.byte_at(bytes, $at + 1) == ':' {
			return Bytes.span(bytes, 1, $at - 1)
		}
		$at = $at + 1
	}
	bytes
}

## Whether `->` or `=>` stands at `place`.
is_arrow_at : List(U8), U64 -> Bool
is_arrow_at = |bytes, place| {
	byte = Bytes.byte_at(bytes, place)
	(byte == '-' or byte == '=') and Bytes.byte_at(bytes, place + 1) == '>'
}

## The type text after the start of a function's annotation (`name :`) or of
## a type alias (`Handler :`, `Pick(a) :`) on a comment-free line, which is
## empty when the type starts on the next line:
## `^[a-z_][A-Za-z0-9_]*!? :(?: (.*))?$` or
## `^[A-Z][A-Za-z0-9_]*(?:\([^()]*\))? :(?: (.*))?$`.
start_text : List(U8) -> Try(List(U8), [NoStart])
start_text = |bytes| {
	first = Bytes.byte_at(bytes, 1)
	name_end =
		if (first >= 'a' and first <= 'z') or first == '_' {
			end = Bytes.word_end(bytes, 2)
			if Bytes.byte_at(bytes, end) == '!' {
				end + 1
			} else {
				end
			}
		} else if first >= 'A' and first <= 'Z' {
			end = Bytes.word_end(bytes, 2)
			if Bytes.byte_at(bytes, end) == '(' {
				match bytes.drop_first(end).find_first_index(|byte| byte == '(' or byte == ')') {
					Ok(offset) if Bytes.byte_at(bytes, end + offset + 1) == ')' => end + offset + 2
					_ => end
				}
			} else {
				end
			}
		} else {
			return Err(NoStart)
		}
	rest = Bytes.rest_from(bytes, name_end)
	if rest == [' ', ':'] {
		Ok([])
	} else if rest.starts_with([' ', ':', ' ']) {
		Ok(rest.drop_first(3))
	} else {
		Err(NoStart)
	}
}

## Whether the bytes start a record type's field: `^ *[a-z_][A-Za-z0-9_]* : `.
starts_field : List(U8) -> Bool
starts_field = |bytes| {
	var $at = 1
	while Bytes.byte_at(bytes, $at) == ' ' {
		$at = $at + 1
	}
	first = Bytes.byte_at(bytes, $at)
	if (first >= 'a' and first <= 'z') or first == '_' {
		end = Bytes.word_end(bytes, $at + 1)
		Bytes.span(bytes, end, end + 2) == [' ', ':', ' ']
	} else {
		Bool.False
	}
}
