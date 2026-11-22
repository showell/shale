## What hunt_names reads of a type, in the tree or written as text
## (cookbook/roc/declared.shale's readers, and types.shale's
## `roc_type_parts`): a tag union's tags, an argument's shape, the types an
## annotation mentions, a function type's parts, a `Try`'s success and a
## list's element, a tag's first word and words joined as a type's name; and
## where a node sits: its source text, the sibling before it, and whether a
## definition calls itself.
import lib.Text
import lib.Tree

TypeText := [].{

	## A tag union's tag names (`Unpinned` for `Unpinned(Str)` too), and
	## whether any tag carries a payload.
	Union : { names : List(Str), has_payload : Bool }

	## An argument's type as `command_tense` and `mixed_tense` need it: a tag
	## union written in place, a named type by its last part, or neither; and
	## whether it was a `List` of that.
	ArgumentShape : {
		shape : [Written(TypeText.Union), Named(Str), Unread],
		is_list : Bool,
	}

	## A function type's parts as text: whether the type is a function, the
	## text before its arrow and the text after it, its whole text when it is
	## no function.
	Parts : { is_function : Bool, arguments : Str, result : Str }

	## The tags of the tag union at `at`.
	union_shape : List(Tree.Node), U64 -> TypeText.Union
	union_shape = |nodes, at| {
		var $names = []
		var $has_payload = Bool.False
		for part in Tree.node_at(nodes, at).kids {
			part_node = Tree.node_at(nodes, part)
			if part_node.kind == "tags" {
				for tag in part_node.kids {
					tag_node = Tree.node_at(nodes, tag)
					name = if tag_node.kind == "ty-apply" {
						$has_payload = Bool.True
						Tree.attribute(Tree.node_at(nodes, Tree.first_kid(tag_node)), "name")
					} else {
						Tree.attribute(tag_node, "name")
					}
					if name != "" {
						$names = $names.append(name)
					}
				}
			}
		}
		{ names: $names, has_payload: $has_payload }
	}

	## The shape of the argument type at `at`; a `List(T)` gives `T`'s.
	argument_shape : List(Tree.Node), U64 -> TypeText.ArgumentShape
	argument_shape = |nodes, at| {
		node = Tree.node_at(nodes, at)
		unread = { shape: Unread, is_list: Bool.False }
		if node.kind == "ty-tag-union" {
			{ shape: Written(TypeText.union_shape(nodes, at)), is_list: Bool.False }
		} else if node.kind == "ty-apply" {
			match node.kids {
				[head, element] if Tree.attribute(Tree.node_at(nodes, head), "name") == "List" =>
					{ ..TypeText.argument_shape(nodes, element), is_list: Bool.True }
				_ => unread
			}
		} else {
			name = Tree.attribute(node, "name")
			if name == "" {
				unread
			} else {
				{ shape: Named(Tree.last_part(name)), is_list: Bool.False }
			}
		}
	}

	## The names of the types the type at `at` mentions, each by its last
	## part, added to `into`.
	mentioned_type_names : List(Tree.Node), U64, Set(Str) -> Set(Str)
	mentioned_type_names = |nodes, at, into| {
		node = Tree.node_at(nodes, at)
		name = Tree.attribute(node, "name")
		here = if node.kind.starts_with("ty") and name != "" {
			into.insert(Tree.last_part(name))
		} else {
			into
		}
		node.kids.fold(here, |names, kid| TypeText.mentioned_type_names(nodes, kid, names))
	}

	## The first argument in a function type's argument text, `a, b`.
	first_argument : Str -> Str
	first_argument = |arguments| {
		bytes = arguments.to_utf8()
		$depth : I64
		var $depth = 0
		$at : U64
		var $at = 0
		for byte in bytes {
			$depth = $depth + depth_change(byte)
			if $depth == 0 and byte == ',' {
				return Text.trim(Str.from_utf8_lossy(bytes.take_first($at)))
			}
			$at = $at + 1
		}
		Text.trim(arguments)
	}

	## `Try(Fraction, [OutOfRange])` gives `Fraction`, and any other type
	## itself, trimmed.
	success_type : Str -> Str
	success_type = |written| {
		text = Text.trim(written)
		if text.starts_with("Try(") and text.ends_with(")") {
			TypeText.first_argument(inside(text, 4))
		} else {
			text
		}
	}

	## The element type's text of a `List(...)` or a `Try` of one.
	list_element : Str -> Try(Str, [NoList])
	list_element = |written| {
		text = TypeText.success_type(written)
		if text.starts_with("List(") and text.ends_with(")") {
			Ok(inside(text, 5))
		} else {
			Err(NoList)
		}
	}

	## The leading run of name bytes of a type's text (`A-Z`, `a-z`, `0-9`,
	## `_`, `.`), `""` when it starts with none.
	type_head : Str -> Str
	type_head = |text| {
		bytes = text.to_utf8()
		length = bytes.find_first_index(|byte| !(is_type_name_byte(byte))) ?? bytes.len()
		Str.from_utf8_lossy(bytes.take_first(length))
	}

	## The last word of a type's text, or `""` for one with no name:
	## `Shapes.RoundShape(a)` gives `shape`.
	element_word : Str -> Str
	element_word = |element| {
		head = TypeText.type_head(element)
		Text.name_words(Tree.last_part(head)).last() ?? ""
	}

	## Words joined as one type name: `rounded`, `rectangle` gives
	## `RoundedRectangle`.
	camel_case : List(Str) -> Str
	camel_case = |words| Str.join_with(words.map(capitalized), "")

	## A tag's first word, lowercased: `Unpinned` gives `unpinned`, `AddedTo`
	## gives `added`.
	tag_first_word : Str -> Str
	tag_first_word = |tag| {
		bytes = tag.to_utf8()
		match bytes {
			[first, .. as rest] => {
				length = rest.find_first_index(Text.is_capital) ?? rest.len()
				Text.lower(Str.from_utf8_lossy([first].concat(rest.take_first(length))))
			}
			[] => ""
		}
	}

	## A type's text as its parts: a function when an arrow stands outside
	## every bracket, once any `where` clause is dropped.
	parts_of : Str -> TypeText.Parts
	parts_of = |written| {
		bytes = written.to_utf8()
		body = bytes.take_first(where_start(bytes))
		$depth : I64
		var $depth = 0
		$at : U64
		var $at = 0
		while $at + 1 < body.len() {
			byte = body.get($at) ?? 0
			$depth = $depth + depth_change(byte)
			is_arrow = (byte == '-' or byte == '=') and (body.get($at + 1) ?? 0) == '>'
			if $depth == 0 and is_arrow {
				return {
					is_function: Bool.True,
					arguments: Text.trim(Str.from_utf8_lossy(body.take_first($at))),
					result: Text.trim(Str.from_utf8_lossy(body.drop_first($at + 2))),
				}
			}
			$at = $at + 1
		}
		{ is_function: Bool.False, arguments: "", result: Text.trim(Str.from_utf8_lossy(body)) }
	}

	## The source text of a node, from the file's lines: its end column is
	## one past its last byte.
	span_text : List(Str), Tree.Node -> Str
	span_text = |source_lines, node|
		if node.line == 0 {
			""
		} else if node.line == node.end_line {
			columns(Text.line_at(source_lines, node.line), node.column, node.end_column)
		} else {
			first = columns(Text.line_at(source_lines, node.line), node.column, Text.line_at(source_lines, node.line).count_utf8_bytes() + 1)
			middle = source_lines.sublist({ start: node.line, len: node.end_line - node.line - 1 })
			last = columns(Text.line_at(source_lines, node.end_line), 1, node.end_column)
			Str.join_with([first].concat(middle).append(last), "\n")
		}

	## The place of the sibling just before the node at `at`, or 0.
	previous_sibling : List(Tree.Node), U64 -> U64
	previous_sibling = |nodes, at| {
		parent = Tree.node_at(nodes, at).parent
		if parent == 0 {
			return 0
		}
		$previous : U64
		var $previous = 0
		for kid in Tree.node_at(nodes, parent).kids {
			if kid == at {
				return $previous
			}
			$previous = kid
		}
		0
	}

	## Whether the definition at `at` names `name` in its body, as an
	## identifier (`name`, `Tree.name`) or a method call. A node's
	## descendants follow it, each deeper than it.
	calls_itself : List(Tree.Node), U64, Str -> Bool
	calls_itself = |nodes, at, name| {
		depth = Tree.node_at(nodes, at).depth
		var $place = at + 1
		while $place <= nodes.len() and Tree.node_at(nodes, $place).depth > depth {
			node = Tree.node_at(nodes, $place)
			if node.kind == "e-ident" {
				raw = Tree.attribute(node, "raw")
				if raw == name or raw.ends_with(".${name}") {
					return Bool.True
				}
			}
			if node.kind == "e-method-call" and Tree.attribute(node, "method") == name {
				return Bool.True
			}
			$place = $place + 1
		}
		Bool.False
	}
}

## How a byte moves the bracket depth.
depth_change : U8 -> I64
depth_change = |byte|
	if byte == '(' or byte == '[' or byte == '{' {
		1
	} else if byte == ')' or byte == ']' or byte == '}' {
		-1
	} else {
		0
	}

## The bytes before ` where ` outside every bracket, or all of them.
where_start : List(U8) -> U64
where_start = |bytes| {
	clause = " where ".to_utf8()
	$depth : I64
	var $depth = 0
	$at : U64
	var $at = 0
	for byte in bytes {
		$depth = $depth + depth_change(byte)
		if $depth == 0 and bytes.sublist({ start: $at, len: clause.len() }) == clause {
			return $at
		}
		$at = $at + 1
	}
	bytes.len()
}

## A text between its first `opening` bytes and its last byte.
inside : Str, U64 -> Str
inside = |text, opening| {
	length = text.count_utf8_bytes()
	Text.bytes_of(text, { start: opening, count: length - opening - 1 })
}

## Shale's `sub(line, from, to - 1)`: the bytes from column `from` to before
## column `to`, both counted from 1, `""` when none lie between.
columns : Str, U64, U64 -> Str
columns = |line, from, to|
	if to <= from or from == 0 {
		""
	} else {
		Text.bytes_of(line, { start: from - 1, count: to - from })
	}

is_type_name_byte : U8 -> Bool
is_type_name_byte = |byte| Text.is_capital(byte) or Text.is_small_letter(byte) or Text.is_digit(byte) or byte == '_' or byte == '.'

## A word with its first byte made a capital, as Shale's `upper` does.
capitalized : Str -> Str
capitalized = |word|
	match word.to_utf8() {
		[first, .. as rest] if Text.is_small_letter(first) => Str.from_utf8_lossy([first - 32].concat(rest))
		_ => word
	}
