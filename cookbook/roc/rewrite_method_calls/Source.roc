## A file as the rewrite reads it: Roc's tree of it, each node's span as byte
## offsets into its text, and where each line starts, so a byte's line is
## found and a span's text cut out.
import lib.Tree

Source := [].{

	## A node of the file's tree: its kind and the attributes the rewrite
	## reads (`""` where the tree gives none), its span as byte offsets
	## (`from` its first byte, counted from 1, `to` one past its last, both 0
	## and `has_span` false for a node with no position), its kids and
	## parent as places counted from 1 (the root's parent 0), and its region
	## as the compiler writes it, `line:col:end_line:end_col`, by which its
	## type is found.
	Node : {
		kind : Str,
		raw : Str,
		name : Str,
		field : Str,
		method : Str,
		from : U64,
		to : U64,
		has_span : Bool,
		kids : List(U64),
		parent : U64,
		region : Str,
	}

	## A file: its path, its text (and its bytes), its nodes, and the byte at
	## which each line starts, counted from 1, with one more past the end.
	File : {
		path : Str,
		text : Str,
		bytes : List(U8),
		nodes : List(Source.Node),
		starts : List(U64),
	}

	## The file at `path`, of text `text`, as Roc's tree of it reads.
	of : Str, Str, List(Tree.Node) -> Source.File
	of = |path, text, tree_nodes| {
		bytes = text.to_utf8()
		starts = line_starts(bytes)
		nodes = tree_nodes.map(|tree_node| node_of(tree_node, starts))
		{ path, text, bytes, nodes, starts }
	}

	## The node at a place counted from 1.
	node_at : Source.File, U64 -> Source.Node
	node_at = |file, place|
		match file.nodes.get(place - 1) {
			Ok(node) => node
			Err(_) => crash "Source.node_at: ${file.path} has no node ${place.to_str()}"
		}

	## The place of a node's kid, the kids counted from 1.
	kid : Source.Node, U64 -> U64
	kid = |node, number|
		match node.kids.get(number - 1) {
			Ok(place) => place
			Err(_) => crash "Source.kid: a ${node.kind} with no kid ${number.to_str()}"
		}

	## The node that is a node's kid, the kids counted from 1.
	kid_node : Source.File, Source.Node, U64 -> Source.Node
	kid_node = |file, node, number| Source.node_at(file, Source.kid(node, number))

	## The line the byte at `offset` is on: the last line starting at or
	## before it, and line 1 for an offset before the first.
	line_of_offset : Source.File, U64 -> U64
	line_of_offset = |file, offset| {
		var $low = 0
		var $high = file.starts.len()
		while $low < $high {
			middle = ($low + $high) // 2
			if (file.starts.get(middle) ?? 0) <= offset {
				$low = middle + 1
			} else {
				$high = middle
			}
		}
		if $low == 0 {
			1
		} else {
			$low
		}
	}

	## The bytes from offset `from` up to, not including, `to`; none when
	## `to` is not past `from`.
	bytes_between : Source.File, U64, U64 -> List(U8)
	bytes_between = |file, from, to|
		if to > from and from > 0 {
			file.bytes.sublist({ start: from - 1, len: to - from })
		} else {
			[]
		}

	## The text from offset `from` up to, not including, `to`.
	text_between : Source.File, U64, U64 -> Str
	text_between = |file, from, to| Str.from_utf8_lossy(Source.bytes_between(file, from, to))

	## A node's text.
	text_of : Source.File, Source.Node -> Str
	text_of = |file, node| Source.text_between(file, node.from, node.to)
}

## The byte at which each line starts, counted from 1, and one more past the
## last line's end, as method_text.shale's `line_starts` counts them.
line_starts : List(U8) -> List(U64)
line_starts = |bytes| {
	var $starts = [1]
	$at : U64
	var $at = 0
	for byte in bytes {
		$at = $at + 1
		if byte == '\n' {
			$starts = $starts.append($at + 1)
		}
	}
	$starts.append(bytes.len() + 2)
}

## A node of Roc's tree with its span made byte offsets.
node_of : Tree.Node, List(U64) -> Source.Node
node_of = |tree_node, starts| {
	start_of = |line|
		match starts.get(line - 1) {
			Ok(start) => start
			Err(_) => crash "Source: a node on line ${line.to_str()}, past the file's end"
		}
	has_span = tree_node.line != 0
	from = if has_span {
		start_of(tree_node.line) + tree_node.column - 1
	} else {
		0
	}
	to = if has_span {
		start_of(tree_node.end_line) + tree_node.end_column - 1
	} else {
		0
	}
	starts_at = "${tree_node.line.to_str()}:${tree_node.column.to_str()}"
	ends_at = "${tree_node.end_line.to_str()}:${tree_node.end_column.to_str()}"
	{
		kind: tree_node.kind,
		raw: Tree.attribute(tree_node, "raw"),
		name: Tree.attribute(tree_node, "name"),
		field: Tree.attribute(tree_node, "field"),
		method: Tree.attribute(tree_node, "method"),
		from,
		to,
		has_span,
		kids: tree_node.kids,
		parent: tree_node.parent,
		region: "${starts_at}:${ends_at}",
	}
}
