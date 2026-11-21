## Roc's bracket groups read from a file's lines, as cookbook/roc/groups.shale
## reads them for check_stacking and wrap_long_lines: each pair of brackets or
## a lambda's `|...|`, where it opens and closes, its items counted by the
## commas at its own depth and its tokens. A token is a name (a dotted path
## like `Angle.Bearing` is one), a number, a string or a character;
## punctuation and operators are none. The rule the groups are judged by is
## Stacking's. A place in a line is counted from 1 and in bytes, as Shale's
## `sub` counts, so a line's width is its bytes with a tab counting four.
import Bytes

Groups := [].{

	## A group of the lines read: the line and column it opens at, the line
	## and column it closes at, its items, its tokens, whether a comment or a
	## `\\` string lies inside it (`fixed`), whether its last item ends in a
	## comma (`trailing`), how many groups are open around it, the group
	## around it and whether it is a function type's arguments, `(A, B -> C)`,
	## which can't take a trailing comma.
	Group : {
		line : U64,
		open : U64,
		close_line : U64,
		close : U64,
		items : U64,
		tokens : U64,
		fixed : Bool,
		trailing : Bool,
		depth : U64,
		parent : Groups.Parent,
		fn_type : Bool,
	}

	## The group around a group: the byte that closes it (0 for none), the
	## line it opens on (0 for none) and whether its last item ends in a
	## comma, which is known once it closes.
	Parent : {
		closer : U8,
		line : U64,
		trailing : Bool,
	}

	## The byte that closes a group `byte` opens, or 0 when it opens none.
	closer_of : U8 -> U8
	closer_of = |byte|
		if byte == '(' {
			')'
		} else if byte == '{' {
			'}'
		} else if byte == '[' {
			']'
		} else {
			0
		}

	## Whether `byte` closes a pair of brackets.
	is_closer : U8 -> Bool
	is_closer = |byte| byte == ')' or byte == '}' or byte == ']'

	## The groups of `source_lines`, by where they close. A group that never
	## closes in them is left out.
	groups_in_lines : List(Str) -> List(Groups.Group)
	groups_in_lines = |source_lines| {
		var $scan = {
			open: [],
			closed: [],
			trailing_by_id: [],
			tokens: 0,
			fixed: 0,
			last_code: 0,
		}
		$number : U64
		var $number = 0
		for text in source_lines {
			$number = $number + 1
			$scan = scanned_line($scan, { bytes: text.to_utf8(), number: $number })
		}
		trailing_by_id = $scan.trailing_by_id
		$scan.closed.map(
			|(group, parent_id)|
				if parent_id == 0 {
					group
				} else {
					parent = group.parent
					{
						..group,
						parent: {
							..parent,
							trailing: trailing_by_id.get(parent_id - 1) ?? Bool.False,
						},
					}
				},
		)
	}

	## The groups that open and close on one line.
	groups_on_line : Str -> List(Groups.Group)
	groups_on_line = |text| Groups.groups_in_lines([text])

}

## A group still open: where it opened, the byte that closes it, its commas
## so far and the place of the last one, whether an arrow stands at its own
## depth, whether it is a `where` clause's brackets, and the running counts
## when it opened.
Opened : {
	id : U64,
	line : U64,
	at : U64,
	closer : U8,
	commas : U64,
	last : U64,
	has_arrow : Bool,
	is_where : Bool,
	tokens_before : U64,
	fixed_before : U64,
}

## The scan so far: the groups open (innermost last), those closed with the
## id of the group around each (0 for none), each group's `trailing` by its
## id once it closes, the running counts of tokens and of comments and `\\`
## strings, and the last byte of code seen (0 for none).
Scan : {
	open : List(Opened),
	closed : List((Groups.Group, U64)),
	trailing_by_id : List(Bool),
	tokens : U64,
	fixed : U64,
	last_code : U8,
}

## The scan after one more line.
scanned_line : Scan, { bytes : List(U8), number : U64 } -> Scan
scanned_line = |before, source| {
	l = source.bytes
	line = source.number
	var $scan = before
	var $count = l.len()
	if Bytes.trimmed(l).starts_with(['\\', '\\']) {
		$scan = {
			..$scan,
			fixed: $scan.fixed + 1,
			tokens: $scan.tokens + 1,
			last_code: '\\',
		}
		$count = 0
	}
	$i : U64
	var $i = 1
	while $i <= $count {
		c = Bytes.byte_at(l, $i)
		var $code = c
		if c == ' ' {
			# A space only separates, and spaces are the most common byte.
			$code = ' '
		} else if Bytes.is_word_byte(c) {
			# A name right after a dot continues a path, as in `f(x).name`.
			continues = Bytes.byte_at(
				l,
				$i - 1,
			) == '.' and !($i >= 2 and Bytes.byte_at(l, $i - 2) == '.')
			if !(continues) {
				$scan = { ..$scan, tokens: $scan.tokens + 1 }
			}
			$i = path_end(l, $i) - 1
		} else if c == '"' or c == '\'' {
			$scan = { ..$scan, tokens: $scan.tokens + 1 }
			$i = $i + 1
			while $i <= $count and Bytes.byte_at(l, $i) != c {
				if Bytes.byte_at(l, $i) == '\\' {
					$i = $i + 1
				}
				$i = $i + 1
			}
		} else if c == '#' {
			$scan = { ..$scan, fixed: $scan.fixed + 1 }
			$code = ' '
			$count = 0
		} else if Groups.closer_of(c) != 0 {
			is_where = c == '[' and follows_where(l, $i)
			$scan = opened(
				$scan,
				{
					line,
					at: $i,
					closer: Groups.closer_of(c),
					is_where,
				},
			)
		} else if (c == '-' or c == '=') and Bytes.byte_at(
			l,
			$i + 1,
		) == '>' and Bytes.byte_at(l, $i - 1) == ' ' and !($scan.open.is_empty()) {
			$scan = with_top($scan, |group| { ..group, has_arrow: Bool.True })
			$i = $i + 1
		} else if c == '|' and Bytes.byte_at(
			l,
			$i + 1,
		) != '>' and Bytes.byte_at(l, $i + 1) != '|' and Bytes.byte_at(l, $i - 1) != '|' {
			if top_closer($scan.open) == '|' {
				$scan = closed($scan, { bytes: l, line, close: $i })
			} else {
				# A `|` opens a lambda only where an expression can start.
				b = Bytes.byte_before(l, $i)
				if b == 0 or b == '=' or b == '(' or b == ',' or b == '[' or b == '{' or b == ':' {
					$scan = opened(
						$scan,
						{
							line,
							at: $i,
							closer: '|',
							is_where: Bool.False,
						},
					)
				}
			}
		} else if Groups.is_closer(c) {
			# Pop down to the group this byte closes. A `|` that turned out to
			# be no lambda is dropped on the way.
			var $open = $scan.open
			while !($open.is_empty()) and top_closer($open) != c {
				$open = $open.drop_last(1)
			}
			$scan = { ..$scan, open: $open }
			if !($open.is_empty()) {
				$scan = closed($scan, { bytes: l, line, close: $i })
			}
		} else if c == ',' and !($scan.open.is_empty()) {
			rest = Bytes.rest_from(l, $i + 1)
			in_where = match $scan.open.last() {
				Ok(group) => group.is_where
				Err(_) => Bool.False
			}
			# In a `where` clause's brackets, only a comma before a method
			# constraint, or at its line's end, separates items.
			if !(in_where) or Bytes.trimmed(rest).is_empty() or starts_constraint(rest) {
				comma_at = $i
				$scan = with_top($scan, |group| with_comma(group, comma_at))
			}
		}
		if $code != ' ' and $code != '\t' {
			$scan = { ..$scan, last_code: $code }
		}
		$i = $i + 1
	}
	$scan
}

## The byte that closes the innermost of the open groups, or 0 for none.
top_closer : List(Opened) -> U8
top_closer = |open|
	match open.last() {
		Ok(group) => group.closer
		Err(_) => 0
	}

## An open group with one more comma, the last at `place`.
with_comma : Opened, U64 -> Opened
with_comma = |group, place| {
	..group,
	commas: group.commas + 1,
	last: place,
}

## The scan with a group opened.
opened : Scan,
{
	line : U64,
	at : U64,
	closer : U8,
	is_where : Bool,
} -> Scan
opened = |scan, place| {
	id = scan.trailing_by_id.len() + 1
	group = {
		id,
		line: place.line,
		at: place.at,
		closer: place.closer,
		commas: 0,
		last: place.at,
		has_arrow: Bool.False,
		is_where: place.is_where,
		tokens_before: scan.tokens,
		fixed_before: scan.fixed,
	}
	{
		..scan,
		open: scan.open.append(group),
		trailing_by_id: scan.trailing_by_id.append(Bool.False),
	}
}

## The scan with its innermost open group changed.
with_top : Scan, (Opened -> Opened) -> Scan
with_top = |scan, change|
	match scan.open.last() {
		Ok(group) => {
			..scan,
			open: scan.open.set(
				scan.open.len() - 1,
				change(group),
			) ?? crash "Groups: no open group to change",
		}
		Err(_) => scan
	}

## The scan with its innermost open group closed at column `close` of line
## `line`: its items counted from the commas at its own depth, a last item
## that is a rest pattern (`{ a, b, .. }`, `[x, .. as more]`) not counted.
closed : Scan,
{
	bytes : List(U8),
	line : U64,
	close : U64,
} -> Scan
closed = |scan, at|
	match scan.open.last() {
		Err(_) => scan
		Ok(g) => {
			l = at.bytes
			trailing = scan.last_code == ','
			var $items = if trailing {
				g.commas
			} else {
				g.commas + 1
			}
			if g.line == at.line {
				if Bytes.trimmed(Bytes.span(l, g.at + 1, at.close - 1)).is_empty() {
					$items = 0
				}
				last = Bytes.trimmed(Bytes.span(l, g.last + 1, at.close - 1))
				if !(trailing) and g.commas > 0 and (
					last == [
						'.',
						'.',
					] or last.starts_with(".. as ".to_utf8())
				) {
					$items = $items - 1
				}
			}
			depth = scan.open.len() - 1
			around = if depth > 0 {
				scan.open.get(depth - 1)
			} else {
				Err(OutOfBounds)
			}
			(parent, parent_id) = match around {
				Ok(outer) => (
					{
						closer: outer.closer,
						line: outer.line,
						trailing: Bool.False,
					},
					outer.id,
				)
				Err(_) => (
					{
						closer: 0,
						line: 0,
						trailing: Bool.False,
					},
					0,
				)
			}
			group = {
				line: g.line,
				open: g.at,
				close_line: at.line,
				close: at.close,
				items: $items,
				tokens: scan.tokens - g.tokens_before,
				fixed: scan.fixed > g.fixed_before,
				trailing,
				depth,
				parent,
				fn_type: g.has_arrow and g.closer == ')',
			}
			{
				..scan,
				open: scan.open.drop_last(1),
				closed: scan.closed.append((group, parent_id)),
				trailing_by_id: scan.trailing_by_id.set(
					g.id - 1,
					trailing,
				) ?? crash "Groups: a group with no id",
			}
		}
	}

## Where a name or a dotted path that starts at `place` ends, one past its last byte:
## `[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)*`.
path_end : List(U8), U64 -> U64
path_end = |bytes, place| {
	var $end = place
	while Bytes.is_word_byte(Bytes.byte_at(bytes, $end)) {
		$end = $end + 1
	}
	while Bytes.byte_at(bytes, $end) == '.' and Bytes.is_word_byte(Bytes.byte_at(bytes, $end + 1)) {
		$end = $end + 1
		while Bytes.is_word_byte(Bytes.byte_at(bytes, $end)) {
			$end = $end + 1
		}
	}
	$end
}

## Whether the code before column `place` ends in the keyword `where`, so
## that a `[` there opens a `where` clause.
follows_where : List(U8), U64 -> Bool
follows_where = |bytes, place| {
	var $j = place - 1
	while $j > 0 and (Bytes.byte_at(bytes, $j) == ' ' or Bytes.byte_at(bytes, $j) == '\t') {
		$j = $j - 1
	}
	if $j < 5 or Bytes.span(bytes, $j - 4, $j) != "where".to_utf8() {
		Bool.False
	} else if $j == 5 {
		Bool.True
	} else {
		b = Bytes.byte_at(bytes, $j - 5)
		b == ' ' or (b == '\t' and Bytes.trimmed(Bytes.span(bytes, 1, $j - 5)).is_empty())
	}
}

## Whether the bytes start a method constraint, `k.is_eq :`:
## `^ *[a-z_][A-Za-z0-9_]*\.[a-z_][A-Za-z0-9_]*!? *:`. In a `where` clause's
## brackets, only a comma followed by one, or ending its line, separates
## items.
starts_constraint : List(U8) -> Bool
starts_constraint = |bytes| {
	var $at = 1
	while Bytes.byte_at(bytes, $at) == ' ' {
		$at = $at + 1
	}
	match name_end(bytes, $at) {
		Err(_) => Bool.False
		Ok(after_type) =>
			if Bytes.byte_at(bytes, after_type) != '.' {
				Bool.False
			} else {
				match name_end(bytes, after_type + 1) {
					Err(_) => Bool.False
					Ok(after_method) => {
						var $end = after_method
						if Bytes.byte_at(bytes, $end) == '!' {
							$end = $end + 1
						}
						while Bytes.byte_at(bytes, $end) == ' ' {
							$end = $end + 1
						}
						Bytes.byte_at(bytes, $end) == ':'
					}
				}
			}
	}
}

## Where a lower name `[a-z_][A-Za-z0-9_]*` that starts at `place` ends, one
## past its last byte.
name_end : List(U8), U64 -> Try(U64, [NoName])
name_end = |bytes, place| {
	first = Bytes.byte_at(bytes, place)
	if (first >= 'a' and first <= 'z') or first == '_' {
		var $end = place + 1
		while Bytes.is_word_byte(Bytes.byte_at(bytes, $end)) {
			$end = $end + 1
		}
		Ok($end)
	} else {
		Err(NoName)
	}
}
