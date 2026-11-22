## The stacking rule's one home, over Groups' groups (ported from the Shale
## groups.shale, since retired): a group of three or more items and `tokens`
## or more tokens stacks, one item a line, and any other group sits on one
## line while that line fits `width`.
## A function type's arguments can't take a trailing comma, and a row of
## literals or a line of numeric data stays packed. The rewrite's own pieces
## are here too: the group a line's rewrite gives a trailing comma, the
## stacked groups whose comma it takes away, the `targets:` section no
## rewrite reaches, and the columns a line's strings take up, which no
## trailing comma can shorten.
import Bytes
import Groups
import Packed
import Tree

Stacking := [].{

	## The rule's two numbers: the tokens from which a group of three or more
	## items stacks, and the widest a line may be.
	Rule : { tokens : U64, width : U64 }

	## A group with the line it is on.
	Placed : { line : Str, group : Groups.Group }

	## A stacked group that should sit on one line, with the line and column
	## of its trailing comma.
	Unstacked : {
		group : Groups.Group,
		comma_line : U64,
		comma : U64,
	}

	## The items from which a group stacks.
	stack_items : U64
	stack_items = 3

	## The tokens from which a group stacks, unless a run says otherwise.
	stack_tokens : U64
	stack_tokens = 6

	## Whether a line is a line of data: wider than `width`, with a number
	## list on it. No group on it stacks, since stacking any group around the
	## data, or beside it, makes `roc fmt` open every group around that one.
	is_data_line : Str, U64 -> Bool
	is_data_line = |text, width| {
		bytes = text.to_utf8()
		Bytes.width_of(bytes) > width and Packed.holds_number_list(bytes)
	}

	## Whether a group holds enough to read better stacked.
	is_big : Groups.Group, U64 -> Bool
	is_big = |group, tokens| group.items >= Stacking.stack_items and group.tokens >= tokens

	## Whether a group on its line should stack: big, no trailing comma yet,
	## not a function type's arguments, not a row of literals and not numeric
	## data. The caller asks is_data_line first, once a line.
	should_stack : Stacking.Placed, Stacking.Rule -> Bool
	should_stack = |placed, rule| {
		group = placed.group
		bytes = placed.line.to_utf8()
		if !(Stacking.is_big(group, rule.tokens)) or group.trailing or group.fn_type {
			Bool.False
		} else {
			is_row = Packed.is_literal_row({ bytes, group }, rule.width)
			!(is_row) and !(Packed.is_number_data(bytes, group))
		}
	}

	## The group on a line that the rewrite gives a trailing comma: the
	## outermost, then widest, group that should stack; or else, on a line
	## wider than `width`, the widest group of two or more items whose
	## stacking brings the line within `width`. None on a line of data.
	group_to_stack : Str, Stacking.Rule -> Try(Groups.Group, [NoGroup])
	group_to_stack = |text, rule|
		if Stacking.is_data_line(text, rule.width) {
			Err(NoGroup)
		} else {
			groups = Groups.groups_on_line(text)
			var $best = Err(NoGroup)
			if rule.tokens > 0 {
				for group in groups {
					if Stacking.should_stack({ line: text, group }, rule) {
						$best = match $best {
							Err(_) => Ok(group)
							Ok(best) =>
								if is_outer_or_wider(group, best) {
									Ok(group)
								} else {
									Ok(best)
								}
						}
					}
				}
			}
			if $best != Err(NoGroup) or Bytes.line_width(text) <= rule.width {
				$best
			} else {
				group_to_wrap(text.to_utf8(), { width: rule.width, groups })
			}
		}

	## The line with a comma after the last item of its group, before any
	## spaces that come ahead of the group's closing bracket.
	with_trailing_comma : Stacking.Placed -> Str
	with_trailing_comma = |placed| {
		bytes = placed.line.to_utf8()
		group = placed.group
		var $j = group.close - 1
		while $j > group.open and Bytes.byte_at(bytes, $j) == ' ' {
			$j = $j - 1
		}
		Str.from_utf8_lossy(
			Bytes.span(bytes, 1, $j).append(',').concat(Bytes.rest_from(bytes, $j + 1)),
		)
	}

	## The line a stacked group of the lines would be on one line, spaced as
	## `roc fmt` spaces it, or `Kept` when a line break inside it does more
	## than separate items: a blank line, or a block's statements.
	group_on_one_line : List(Str), Groups.Group -> Try(Str, [Kept])
	group_on_one_line = |source_lines, group| {
		var $joined = text_up_to_group(source_lines, group)
		var $i = group.line + 1
		while $i <= group.close_line {
			piece = Bytes.trimmed(Bytes.line_of(source_lines, $i))
			before = $joined.last() ?? 0
			first = Bytes.byte_at(piece, 1)
			closes = Groups.is_closer(first) or ($i == group.close_line and first == '|')
			if piece.is_empty() {
				return Err(Kept)
			}
			after_bar = before == '|' and $i == group.line + 1
			separates = before == ',' or Groups.closer_of(before) != 0 or after_bar
			if !(separates) {
				return Err(Kept)
			}
			if closes and before == ',' {
				$joined = $joined.drop_last(1)
			}
			if before == '{' or first == '}' or (before == ',' and !(closes)) {
				$joined = $joined.append(' ')
			}
			$joined = $joined.concat(piece)
			$i = $i + 1
		}
		Ok(Str.from_utf8_lossy(with_annotation_rest(source_lines, { group, joined: $joined })))
	}

	## The stacked groups of the lines that should sit on one line, by where
	## they close. A group around one that stays on several lines can't be one
	## line itself: `roc fmt` would give it its comma back. A group whose line
	## doesn't end in its trailing comma is refused, with that line.
	groups_to_unstack : List(Str), Stacking.Rule -> Try(List(Stacking.Unstacked), [NoComma(U64)])
	groups_to_unstack = |source_lines, rule| {
		var $out = []
		var $last_kept = Err(NoGroup)
		for group in Groups.groups_in_lines(Stacking.without_targets_section(source_lines)) {
			if group.line != group.close_line {
				holds_kept = match $last_kept {
					Ok(kept) => is_inside(kept, group)
					Err(_) => Bool.False
				}
				if holds_kept or !(should_unstack(source_lines, { group, rule })) {
					$last_kept = Ok(group)
				} else {
					close_bytes = Bytes.line_of(source_lines, group.close_line)
					comma_line = if Bytes.trimmed(
						Bytes.span(close_bytes, 1, group.close - 1),
					).is_empty() {
						group.close_line - 1
					} else {
						group.close_line
					}
					comma = Bytes.trimmed_end(Bytes.line_of(source_lines, comma_line)).len()
					if Bytes.byte_at(Bytes.line_of(source_lines, comma_line), comma) != ',' {
						return Err(NoComma(comma_line))
					}
					$out = $out.append({ group, comma_line, comma })
				}
			}
		}
		Ok($out)
	}

	## The lines with the trailing comma of each of the groups taken out,
	## which `roc fmt` reads as asking for one line.
	without_trailing_commas : List(Str), List(Stacking.Unstacked) -> List(Str)
	without_trailing_commas = |source_lines, unstacked| {
		var $out = source_lines
		for found in unstacked {
			bytes = Bytes.line_of($out, found.comma_line)
			cut = Str.from_utf8_lossy(Bytes.span(bytes, 1, found.comma - 1))
			$out = $out.set(
				found.comma_line - 1,
				cut,
			) ?? crash "Stacking: a comma past the lines' end"
		}
		$out
	}

	## The lines with a platform header's `targets:` section blanked, for
	## reading groups: `roc fmt` lays that section out by itself, so a
	## rewrite that tried would never end.
	without_targets_section : List(Str) -> List(Str)
	without_targets_section = |source_lines|
		if !(source_lines.any(|text| text.contains("targets:"))) {
			source_lines
		} else {
			found = Groups.groups_in_lines(source_lines).find_first(
				|group| ends_in_targets(
					Bytes.span(
						Bytes.line_of(source_lines, group.line),
						1,
						group.open - 1,
					),
				),
			)
			match found {
				Ok(group) =>
					source_lines.map_with_index(
						|text, index|
							if index + 1 >= group.line and index + 1 <= group.close_line {
								""
							} else {
								text
							},
					)
				Err(_) => source_lines
			}
		}

	## The columns that strings' text takes up on each line of a file, from
	## its tree and its lines, by line number. A run of string text that
	## crosses a line is refused, with its line.
	string_columns_on_lines : List(Tree.Node), List(Str) -> Try(Dict(U64, U64), [CrossesLine(U64)])
	string_columns_on_lines = |nodes, source_lines| {
		var $by_line = Dict.empty()
		for node in nodes {
			if node.kind == "e-string-part" or node.kind == "p-string-text" {
				if node.line != node.end_line {
					return Err(CrossesLine(node.line))
				}
				bytes = Bytes.line_of(source_lines, node.line)
				text = if node.end_column == 0 {
					Bytes.rest_from(bytes, node.column)
				} else {
					Bytes.span(bytes, node.column, node.end_column - 1)
				}
				so_far = $by_line.get(node.line) ?? 0
				$by_line = $by_line.insert(node.line, so_far + Bytes.width_of(text))
			}
		}
		Ok($by_line)
	}
}

## Whether the text before a group ends in `targets:` and spaces, the word
## standing alone (`\btargets: *$`).
ends_in_targets : List(U8) -> Bool
ends_in_targets = |bytes| {
	var $end = bytes.len()
	while $end > 0 and Bytes.byte_at(bytes, $end) == ' ' {
		$end = $end - 1
	}
	word = "targets:".to_utf8()
	if $end < word.len() {
		Bool.False
	} else {
		start = $end - word.len() + 1
		Bytes.span(
			bytes,
			start,
			$end,
		) == word and !(Bytes.is_word_byte(Bytes.byte_at(bytes, start - 1)))
	}
}

## Whether stacked group `group` should sit on one line: not big, nothing a
## rewrite can't join, and a joined line within the width.
should_unstack : List(Str), { group : Groups.Group, rule : Stacking.Rule } -> Bool
should_unstack = |source_lines, judged| {
	group = judged.group
	rule = judged.rule
	kept = group.fixed or holds_parentheses_open(group) or Stacking.is_big(group, rule.tokens)
	if !(group.trailing) or kept {
		Bool.False
	} else {
		match Stacking.group_on_one_line(source_lines, group) {
			Ok(joined) => Bytes.line_width(joined) <= rule.width
			Err(_) => Bool.False
		}
	}
}

## Whether a group to stack is a better one than `best`: outer to it, or as
## deep and wider.
is_outer_or_wider : Groups.Group, Groups.Group -> Bool
is_outer_or_wider = |group, best| {
	wider = group.close - group.open > best.close - best.open
	group.depth < best.depth or (group.depth == best.depth and wider)
}

## Whether a group is what keeps the parentheses around it on several lines.
## They would join its line, past what group_on_one_line measures.
holds_parentheses_open : Groups.Group -> Bool
holds_parentheses_open = |group|
	group.parent.closer == ')' and group.parent.line < group.line and !(group.parent.trailing)

## Whether group `inner`, which closes before `outer`, lies inside it.
is_inside : Groups.Group, Groups.Group -> Bool
is_inside = |inner, outer|
	inner.line > outer.line or (inner.line == outer.line and inner.open > outer.open)

## The text before a group once it sits on one line. A stacked group among an
## annotation's arguments moves its line down from the annotation's, and `roc
## fmt` brings it back up with the group on one line.
text_up_to_group : List(Str), Groups.Group -> List(U8)
text_up_to_group = |source_lines, group| {
	up_to = Bytes.span(Bytes.line_of(source_lines, group.line), 1, group.open)
	above = Bytes.line_of(source_lines, group.line - 1)
	if group.line > 1 and is_annotation_line(Bytes.trimmed(above)) {
		above.append(' ').concat(Bytes.trimmed(up_to))
	} else {
		up_to
	}
}

## `joined`, the line a group would be on, with the rest of its annotation
## when the group is among an annotation's arguments: stacking such a group
## puts the arguments after it on lines of their own, and `roc fmt` brings
## them back up with it. The rest runs to the line with the annotation's
## arrow; without one, `joined` is the whole line.
with_annotation_rest : List(Str), { group : Groups.Group, joined : List(U8) } -> List(U8)
with_annotation_rest = |source_lines, at| {
	group = at.group
	opens = annotation_opens(Bytes.trimmed(Bytes.line_of(source_lines, group.line)))
	follows = group.line > 1 and is_annotation_line(
		Bytes.trimmed(Bytes.line_of(source_lines, group.line - 1)),
	)
	if !(opens) and !(follows) {
		return at.joined
	}
	var $rest = at.joined
	var $i = group.close_line + 1
	while $rest.last() == Ok(',') and $i <= source_lines.len() {
		piece = Bytes.trimmed(Bytes.line_of(source_lines, $i))
		if piece.is_empty() {
			return at.joined
		}
		$rest = $rest.append(' ').concat(piece)
		if holds_arrow(piece) {
			return $rest
		}
		$i = $i + 1
	}
	at.joined
}

## Whether the bytes hold `->` or `=>`.
holds_arrow : List(U8) -> Bool
holds_arrow = |bytes| {
	var $at = 1
	while $at < bytes.len() {
		byte = Bytes.byte_at(bytes, $at)
		if (byte == '-' or byte == '=') and Bytes.byte_at(bytes, $at + 1) == '>' {
			return Bool.True
		}
		$at = $at + 1
	}
	Bool.False
}

## Where an annotation's name and its `!` end on a trimmed line, one past
## them: `^[a-z_][A-Za-z0-9_]*!?`.
annotated_name_end : List(U8) -> Try(U64, [NoName])
annotated_name_end = |bytes| {
	first = Bytes.byte_at(bytes, 1)
	if (first >= 'a' and first <= 'z') or first == '_' {
		var $end = 2
		while Bytes.is_word_byte(Bytes.byte_at(bytes, $end)) {
			$end = $end + 1
		}
		if Bytes.byte_at(bytes, $end) == '!' {
			$end = $end + 1
		}
		Ok($end)
	} else {
		Err(NoName)
	}
}

## Whether a trimmed line starts a function's annotation whose bare
## arguments go on past it: `^[a-z_][A-Za-z0-9_]*!? : .*,$`.
is_annotation_line : List(U8) -> Bool
is_annotation_line = |bytes|
	match annotated_name_end(bytes) {
		Ok(end) => Bytes.span(
			bytes,
			end,
			end + 2,
		) == [' ', ':', ' '] and bytes.len() >= end + 3 and bytes.last() == Ok(',')
		Err(_) => Bool.False
	}

## Whether a trimmed line opens with a function's annotation:
## `^[a-z_][A-Za-z0-9_]*!? : `.
annotation_opens : List(U8) -> Bool
annotation_opens = |bytes|
	match annotated_name_end(bytes) {
		Ok(end) => Bytes.span(bytes, end, end + 2) == [' ', ':', ' ']
		Err(_) => Bool.False
	}

## The group on a line wider than the width that the rewrite stacks for its
## width alone: the widest group of two or more items, not a function type's
## arguments, whose stacking brings the line within the width.
group_to_wrap : List(U8),
{
	width : U64,
	groups : List(Groups.Group),
} -> Try(Groups.Group, [NoGroup])
group_to_wrap = |bytes, given| {
	var $best = Err(NoGroup)
	for group in given.groups {
		if group.items >= 2 and !(group.trailing) and !(group.fn_type) and stacking_fits(
			{ bytes, group },
			given.width,
		) {
			$best = match $best {
				Ok(best) if group.close - group.open <= best.close - best.open => Ok(best)
				_ => Ok(group)
			}
		}
	}
	$best
}

## Whether stacking a group brings its line within `width`, as far as the
## line itself goes: the text up to its opening bracket is one line, and its
## closing bracket and what follows it, at the line's indent, another.
stacking_fits : { bytes : List(U8), group : Groups.Group }, U64 -> Bool
stacking_fits = |at, width|
	if Bytes.width_of(Bytes.span(at.bytes, 1, at.group.open)) > width {
		Bool.False
	} else {
		is_indent = |byte| byte == ' ' or byte == '\t'
		indent_end = at.bytes.find_first_index(|byte| !(is_indent(byte))) ?? at.bytes.len()
		indent = at.bytes.take_first(indent_end)
		Bytes.width_of(indent.concat(Bytes.rest_from(at.bytes, at.group.close))) <= width
	}
