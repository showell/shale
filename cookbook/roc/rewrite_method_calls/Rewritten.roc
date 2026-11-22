## A file's text written back with chosen calls turned: `Module.fn(first,
## rest...)` into `first.fn(rest...)`, or a peer method call back into a
## module call, `a.max(b)` into `U64.max(a, b)` (method_text.shale's
## `rewritten_text`). Every other byte is the file's own.
import Source

Rewritten := [].{

	## A call to turn, by its node: the module and function it calls, and
	## whether it is a peer method call turned back (`undo`).
	Turn : { node : U64, module_name : Str, fn : Str, undo : Bool }

	## The text written, the lines each turned call took in it, by the
	## call's node, and the line each node written starts on.
	Written : {
		text : Str,
		turned_lines : Dict(U64, { from_line : U64, to_line : U64 }),
		line_of_node : Dict(U64, U64),
	}

	## The file's text with every call in `turning` (by its node) turned. The
	## file node spans what roc-ast parsed; the bytes outside it, such as a
	## last newline, are kept.
	written : Source.File, Dict(U64, Rewritten.Turn) -> Rewritten.Written
	written = |file, turning| {
		root = Source.node_at(file, 1)
		start = {
			out: [],
			line: Source.line_of_offset(file, root.from),
			turned_lines: Dict.empty(),
			line_of_node: Dict.empty(),
		}
		done = emit_node(file, turning, start, 1, 0)
		before = Source.bytes_between(file, 1, root.from)
		after = file.bytes.drop_first(root.to - 1)
		{
			text: Str.from_utf8_lossy(before.concat(done.out).concat(after)),
			turned_lines: done.turned_lines,
			line_of_node: done.line_of_node,
		}
	}
}

## The text written so far, the line it ends on, and what is known of the
## turned calls and the nodes written.
Writer : {
	out : List(U8),
	line : U64,
	turned_lines : Dict(U64, { from_line : U64, to_line : U64 }),
	line_of_node : Dict(U64, U64),
}

## The node kinds a receiver can be without parentheses, as in `f(x).len()`
## or `"a".to_utf8()`. Any other (a number, a sum, a lambda, an `if`) is
## wrapped in them: `(a + b).to_str()`.
bare_receiver_kinds : Set(Str)
bare_receiver_kinds = Set.from_list(["e-ident", "e-apply", "e-method-call", "e-field-access", "e-string", "e-list", "e-record", "e-tuple", "e-tag"])

## `bytes` written, the line counted on past each newline.
emit_bytes : Writer, List(U8) -> Writer
emit_bytes = |writer, bytes| {
	newlines = bytes.count_if(|byte| byte == '\n')
	{ ..writer, out: writer.out.concat(bytes), line: writer.line + newlines }
}

emit : Writer, Str -> Writer
emit = |writer, text| emit_bytes(writer, text.to_utf8())

## The nodes that carry node `place`'s text: itself when it has a span, else
## its nearest descendants that do (`args` and `receiver` have none).
spanned_nodes : Source.File, U64 -> List(U64)
spanned_nodes = |file, place| {
	node = Source.node_at(file, place)
	if node.has_span {
		[place]
	} else {
		node.kids.join_map(|kid| spanned_nodes(file, kid))
	}
}

## Writes node `place`'s text, from byte `start` on. A call being turned is
## written as a method call (or a module call); any other node as it is,
## each kid that has a span written in its place by the same rule, so nested
## calls turn too. Spans may overlap (a type module's `type-mod` and its
## declaration both start at the type's name), so a kid is written from
## where the text so far ends, and one wholly before that is skipped.
emit_node : Source.File, Dict(U64, Rewritten.Turn), Writer, U64, U64 -> Writer
emit_node = |file, turning, writer, place, start| {
	node = Source.node_at(file, place)
	noted = { ..writer, line_of_node: writer.line_of_node.insert(place, writer.line) }
	match turning.get(place) {
		Ok(turn) => {
			from_line = noted.line
			done = if turn.undo {
				emit_undone(file, turning, noted, node, turn)
			} else {
				emit_turned(file, turning, noted, node, turn)
			}
			lines = { from_line, to_line: done.line }
			{ ..done, turned_lines: done.turned_lines.insert(place, lines) }
		}
		Err(_) => emit_as_written(file, turning, noted, node, start)
	}
}

## A module call written as a method call on its first argument.
emit_turned : Source.File, Dict(U64, Rewritten.Turn), Writer, Source.Node, Rewritten.Turn -> Writer
emit_turned = |file, turning, writer, node, turn| {
	args = node.kids.drop_first(1)
	first = args.first() ?? 0
	needs_parens = !(bare_receiver_kinds.contains(Source.node_at(file, first).kind))
	var $writer = writer
	if needs_parens {
		$writer = emit($writer, "(")
	}
	$writer = emit_node(file, turning, $writer, first, 0)
	if needs_parens {
		$writer = emit($writer, ")")
	}
	$writer = emit($writer, ".${turn.fn}(")
	var $comma = Bool.False
	for arg in args.drop_first(1) {
		if $comma {
			$writer = emit($writer, ", ")
		}
		$writer = emit_node(file, turning, $writer, arg, 0)
		$comma = Bool.True
	}
	emit($writer, ")")
}

## A peer method call turned back, `a.max(b)` written `U64.max(a, b)`.
emit_undone : Source.File, Dict(U64, Rewritten.Turn), Writer, Source.Node, Rewritten.Turn -> Writer
emit_undone = |file, turning, writer, node, turn| {
	receiver = Source.kid_node(file, node, 1)
	var $writer = emit(writer, "${turn.module_name}.${turn.fn}(")
	$writer = emit_node(file, turning, $writer, Source.kid(receiver, 1), 0)
	for arg in Source.kid_node(file, node, 2).kids {
		$writer = emit($writer, ", ")
		$writer = emit_node(file, turning, $writer, arg, 0)
	}
	emit($writer, ")")
}

## A node written as it is, from `start` or its own first byte, whichever is
## later, with its kids written in their places.
emit_as_written : Source.File, Dict(U64, Rewritten.Turn), Writer, Source.Node, U64 -> Writer
emit_as_written = |file, turning, writer, node, start| {
	var $at = if start > node.from {
		start
	} else {
		node.from
	}
	var $writer = writer
	for kid in node.kids {
		for spanned in spanned_nodes(file, kid) {
			span = Source.node_at(file, spanned)
			if span.to > $at {
				if span.from > $at {
					$writer = emit_bytes($writer, Source.bytes_between(file, $at, span.from))
				}
				$writer = emit_node(file, turning, $writer, spanned, $at)
				$at = span.to
			}
		}
	}
	emit_bytes($writer, Source.bytes_between(file, $at, node.to))
}
