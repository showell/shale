## The edits that fix one check's findings in a file, made by the parser's
## spans, and the file's text with them made. Places in the text are counted
## from 1, as Shale's `sub` counts: an edit at `at` cuts `cut` bytes there and
## puts `put` in their place. A span the tree lacks, or two edits that
## overlap, is an `Err` saying so, which the caller stops the run with: a
## guess here would write braces into the wrong place.
import Checks
import lib.Tree

Edits := [].{

	## A file being rewritten: its path, its text's bytes and its tree.
	Source : { path : Str, text : List(U8), nodes : List(Tree.Node) }

	## One edit: `cut` bytes at `at` taken out, `put` put there.
	Edit : { at : U64, cut : U64, put : Str }

	## The edits that fix the findings of `check` among `findings`.
	for_check : Edits.Source, List(Checks.Found), Str -> Try(List(Edits.Edit), Str)
	for_check = |source, findings, check| {
		src = { path: source.path, text: source.text, nodes: source.nodes, starts: line_starts(source.text) }
		var $edits = []
		for finding in findings {
			if finding.check == check {
				$edits = $edits.concat(edits_of(src, finding)?)
			}
		}
		Ok($edits)
	}

	## The text with the edits made. At one place, an edit that only inserts
	## goes first, so that a closing brace nested in a branch's end stands
	## before the one that cuts the space after it; edits equal in both keep
	## their order.
	applied : List(U8), List(Edits.Edit) -> Try(List(U8), Str)
	applied = |text, edits| {
		var $numbered = []
		for edit in edits {
			$numbered = $numbered.append({ edit, number: $numbered.len() })
		}
		ordered = $numbered.sort_with(edit_order).map(|numbered| numbered.edit)
		var $pieces = []
		$from : U64
		var $from = 1
		for edit in ordered {
			if edit.at < $from {
				return Err("find_extra_parens: two edits overlap at index ${edit.at.to_str()}")
			}
			$pieces = $pieces.concat(span(text, $from, edit.at - 1)).concat(edit.put.to_utf8())
			$from = edit.at + edit.cut
		}
		Ok($pieces.concat(span(text, $from, text.len())))
	}
}

## A file being rewritten, with where each of its lines starts.
Src : { path : Str, text : List(U8), nodes : List(Tree.Node), starts : List(U64) }

## By place, then by what is cut, then by the order they were made in.
edit_order : { edit : Edits.Edit, number : U64 }, { edit : Edits.Edit, number : U64 } -> [Before, Same, After]
edit_order = |first, second|
	match compare(first.edit.at, second.edit.at) {
		Same =>
			match compare(first.edit.cut, second.edit.cut) {
				Same => compare(first.number, second.number)
				other => other
			}
		other => other
	}

compare : U64, U64 -> [Before, Same, After]
compare = |first, second|
	if first < second {
		Before
	} else if first > second {
		After
	} else {
		Same
	}

## The edits that fix one finding.
edits_of : Src, Checks.Found -> Try(List(Edits.Edit), Str)
edits_of = |src, finding| {
	nodes = src.nodes
	node = Tree.node_at(nodes, finding.at)
	if finding.check == "wrapped_body" {
		inner = Tree.node_at(nodes, Tree.first_kid(node))
		Ok(bracket_removal(src, node, keeps_break(nodes, node, inner)))
	} else if finding.check == "else_block" {
		Ok(bracket_removal(src, Tree.node_at(nodes, Tree.kid_at(node, 3)), Bool.False))
	} else if finding.check == "lambda_if" {
		from = start_of(src, finding.at)?
		after = before_space(src.text, from)
		Ok([{ at: after, cut: from - after, put: "\n" }])
	} else {
		paren_if_edits(src, node)
	}
}

## The edits that take an `if`'s condition out of its parentheses and put
## braces around each branch that has none.
paren_if_edits : Src, Tree.Node -> Try(List(Edits.Edit), Str)
paren_if_edits = |src, node| {
	nodes = src.nodes
	condition_at = Tree.kid_at(node, 1)
	then_at = Tree.kid_at(node, 2)
	otherwise_at = Tree.kid_at(node, 3)
	var $edits = []
	if Checks.is_parens(Tree.node_at(nodes, condition_at)) {
		$edits = $edits.concat(bracket_removal(src, Tree.node_at(nodes, condition_at), Bool.False))
	}
	if !(Checks.has_braces(Tree.node_at(nodes, then_at))) {
		$edits = $edits.append(opening_brace(src, then_at, end_of(src, condition_at)?)?)
		$edits = $edits.append(brace_before_else(src, then_at)?)
	}
	if !(Checks.else_is_braced(Tree.node_at(nodes, otherwise_at))) {
		after_else = past_else(src, end_of(src, then_at)?)?
		$edits = $edits.append(opening_brace(src, otherwise_at, after_else)?)
		$edits = $edits.append({ at: end_of(src, otherwise_at)?, cut: 0, put: " }" })
	}
	Ok($edits)
}

## Where each line starts in `text`, the first line at 1.
line_starts : List(U8) -> List(U64)
line_starts = |text| {
	var $starts = [1]
	$place : U64
	var $place = 1
	for byte in text {
		if byte == '\n' {
			$starts = $starts.append($place + 1)
		}
		$place = $place + 1
	}
	$starts
}

## The place of a line's column, both counted from 1.
offset_of : Src, U64, U64 -> U64
offset_of = |src, line, col| {
	start = match src.starts.get(line - 1) {
		Ok(found) => found
		Err(_) => crash "find_extra_parens: ${src.path} has no line ${line.to_str()}"
	}
	start + col - 1
}

## The byte at a place counted from 1, 0 past either end.
byte_at : List(U8), U64 -> U8
byte_at = |text, at|
	if at == 0 {
		0
	} else {
		text.get(at - 1) ?? 0
	}

## The bytes from place `from` to place `to`, both counted from 1 and kept
## inside the text, as Shale's `sub` takes them.
span : List(U8), U64, U64 -> List(U8)
span = |text, from, to| {
	start = if from < 1 {
		1
	} else {
		from
	}
	if to < start {
		[]
	} else {
		text.sublist({ start: start - 1, len: to + 1 - start })
	}
}

## Whether the bytes from `from` to `to` hold a `#`.
has_comment : List(U8), U64, U64 -> Bool
has_comment = |text, from, to| span(text, from, to).contains('#')

## Whether the bytes from `from` on spell `word`.
spells : List(U8), U64, Str -> Bool
spells = |text, from, word| span(text, from, from + word.count_utf8_bytes() - 1) == word.to_utf8()

is_space : List(U8), U64 -> Bool
is_space = |text, at| {
	byte = byte_at(text, at)
	byte == ' ' or byte == '\t' or byte == '\n'
}

## The first place at or past `at` that isn't white space.
past_space : List(U8), U64 -> U64
past_space = |text, at| {
	var $place = at
	while is_space(text, $place) {
		$place = $place + 1
	}
	$place
}

## The last place before `at` that isn't white space, plus one.
before_space : List(U8), U64 -> U64
before_space = |text, at| {
	var $place = at
	while $place > 1 and is_space(text, $place - 1) {
		$place = $place - 1
	}
	$place
}

## Where the line holding the place just before `at` starts.
last_line_start : List(U8), U64 -> U64
last_line_start = |text, at| {
	var $place = at - 1
	while $place > 1 and byte_at(text, $place - 1) != '\n' {
		$place = $place - 1
	}
	$place
}

## The first place at or past `at` outside white space and comments.
past_comments : List(U8), U64 -> U64
past_comments = |text, at| {
	var $place = past_space(text, at)
	while byte_at(text, $place) == '#' {
		while $place <= text.len() and byte_at(text, $place) != '\n' {
			$place = $place + 1
		}
		$place = past_space(text, $place)
	}
	$place
}

## The edits that take off the brackets at the ends of `node`: the space
## inside them goes too, so nothing is left of a line that held a bracket
## alone. With `keep_break`, the line break after the opening bracket stays,
## as a body on its own line wants. A comment beside a bracket keeps its line.
bracket_removal : Src, Tree.Node, Bool -> List(Edits.Edit)
bracket_removal = |src, node, keep_break| {
	text = src.text
	opening = offset_of(src, node.line, node.col)
	closing = offset_of(src, node.end_line, node.end_col) - 1
	past_opening = past_space(text, opening + 1)
	cut_to = if keep_break or byte_at(text, past_opening) == '#' {
		opening + 1
	} else {
		past_opening
	}
	before_closing = before_space(text, closing)
	cut_from = if has_comment(text, last_line_start(text, before_closing), before_closing - 1) {
		closing
	} else {
		before_closing
	}
	[
		{ at: opening, cut: cut_to - opening, put: "" },
		{ at: cut_from, cut: closing + 1 - cut_from, put: "" },
	]
}

## A body keeps its own line after `|x|`, `=>` or `=`, unless it is a block,
## whose brace sits there. A statement has nothing before it to keep.
keeps_break : List(Tree.Node), Tree.Node, Tree.Node -> Bool
keeps_break = |nodes, node, inner|
	Tree.node_at(nodes, node.parent).kind != "statements" and inner.kind != "e-block"

no_span : Src, Tree.Node -> Try(U64, Str)
no_span = |src, node| Err("find_extra_parens: ${src.path}: can't find the ends of a ${node.kind}, which the tree gives no span")

## Where the node at `at` starts. Roc's tree gives a `match` and a unary
## operator no span of their own, so their ends are found from the nodes they
## hold; any other kind with no span is an `Err`.
start_of : Src, U64 -> Try(U64, Str)
start_of = |src, at| {
	node = Tree.node_at(src.nodes, at)
	if node.line != 0 {
		Ok(offset_of(src, node.line, node.col))
	} else if node.kind != "unary" and node.kind != "e-match" {
		no_span(src, node)
	} else {
		before = before_space(src.text, start_of(src, Tree.first_kid(node))?)
		if node.kind == "unary" {
			Ok(before - 1)
		} else if before > 5 and spells(src.text, before - 5, "match") {
			Ok(before - 5)
		} else {
			no_span(src, node)
		}
	}
}

## The place just past the node at `at`.
end_of : Src, U64 -> Try(U64, Str)
end_of = |src, at| {
	node = Tree.node_at(src.nodes, at)
	if node.line != 0 {
		Ok(offset_of(src, node.end_line, node.end_col))
	} else if node.kind == "unary" {
		end_of(src, Tree.first_kid(node))
	} else if node.kind != "e-match" {
		no_span(src, node)
	} else {
		branches = Tree.node_at(src.nodes, Tree.kid_at(node, 2))
		closing = past_comments(src.text, end_of(src, Tree.last_kid(branches))?)
		if byte_at(src.text, closing) == '}' {
			Ok(closing + 1)
		} else {
			no_span(src, node)
		}
	}
}

## The opening brace of the branch at `at`, on the line of what stands before
## it, which ends at `after`, since `roc fmt` keeps a line break before a
## brace. A comment there keeps its place after the brace.
opening_brace : Src, U64, U64 -> Try(Edits.Edit, Str)
opening_brace = |src, at, after| {
	from = start_of(src, at)?
	if has_comment(src.text, after, from - 1) {
		Ok({ at: after, cut: 0, put: " {" })
	} else {
		Ok({ at: after, cut: from - after, put: " { " })
	}
}

## The place just past the `else` that follows `then_end`.
past_else : Src, U64 -> Try(U64, Str)
past_else = |src, then_end| {
	at = past_comments(src.text, then_end)
	if spells(src.text, at, "else") {
		Ok(at + 4)
	} else {
		Err("find_extra_parens: ${src.path}: no `else` where a branch ended")
	}
}

## The closing brace of a `then` branch, with `else` brought up to it.
brace_before_else : Src, U64 -> Try(Edits.Edit, Str)
brace_before_else = |src, at| {
	to = end_of(src, at)?
	next_word = past_space(src.text, to)
	if spells(src.text, next_word, "else") {
		Ok({ at: to, cut: next_word - to, put: " } " })
	} else {
		Ok({ at: to, cut: 0, put: " }" })
	}
}
