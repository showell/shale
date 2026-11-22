## Every parsed file's tree joined into one list of nodes, as the Shale
## originals of check_errors and check_boundaries join them: each node's
## `parent` and `kids` are places in the joined list, counted from 1, a
## file's root's parent 0, and each node keeps its file's path. Nodes are in source order, each before its kids, so a
## subtree runs from its root to its last descendant, and a node with no line
## of its own takes the next line in the list, across a file's end.
import Tree

Forest := {
	nodes : List(Tree.Node),
	paths : List(Str),
}.{

	## The files' trees joined, in the files' order.
	join : List({ path : Str, nodes : List(Tree.Node) }) -> Forest
	join = |files| {
		var $nodes = []
		var $paths = []
		for file in files {
			offset = $nodes.len()
			for file_node in file.nodes {
				moved_parent = if file_node.parent > 0 {
					file_node.parent + offset
				} else {
					0
				}
				moved = { ..file_node, kids: file_node.kids.map(|kid_at| kid_at + offset), parent: moved_parent }
				$nodes = $nodes.append(moved)
				$paths = $paths.append(file.path)
			}
		}
		{ nodes: $nodes, paths: $paths }
	}

	## How many nodes there are.
	count : Forest -> U64
	count = |forest| forest.nodes.len()

	## The node at a place.
	node : Forest, U64 -> Tree.Node
	node = |forest, at| Tree.node_at(forest.nodes, at)

	## The path of the file the node at a place is in.
	path : Forest, U64 -> Str
	path = |forest, at|
		match forest.paths.get(at - 1) {
			Ok(found) => found
			Err(_) => crash "Forest.path: no node ${at.to_str()}"
		}

	kind : Forest, U64 -> Str
	kind = |forest, at| Forest.node(forest, at).kind

	kids : Forest, U64 -> List(U64)
	kids = |forest, at| Forest.node(forest, at).kids

	parent : Forest, U64 -> U64
	parent = |forest, at| Forest.node(forest, at).parent

	## A node's attribute, `""` where it has none.
	attribute : Forest, U64, Str -> Str
	attribute = |forest, at, attribute_name| Tree.attribute(Forest.node(forest, at), attribute_name)

	name : Forest, U64 -> Str
	name = |forest, at| Forest.attribute(forest, at, "name")

	raw : Forest, U64 -> Str
	raw = |forest, at| Forest.attribute(forest, at, "raw")

	## The place of a node's kid, the kids counted from 1, 0 when it has no
	## such kid.
	kid : Forest, U64, U64 -> U64
	kid = |forest, at, number| Forest.kids(forest, at).get(number - 1) ?? 0

	## The kind of the node at a place, `""` for place 0, no node.
	kind_or_none : Forest, U64 -> Str
	kind_or_none = |forest, at|
		if at == 0 {
			""
		} else {
			Forest.kind(forest, at)
		}

	## The last node of the subtree at a place.
	last_descendant : Forest, U64 -> U64
	last_descendant = |forest, at| {
		var $last = at
		while !(Forest.kids(forest, $last).is_empty()) {
			$last = Forest.kids(forest, $last).last() ?? $last
		}
		$last
	}

	## The line a node is reported at: its own, else the next node's that has
	## one, 0 when no node after it has one (a finding prints that as `?`).
	line_number : Forest, U64 -> U64
	line_number = |forest, at| {
		var $place = at
		while $place <= Forest.count(forest) and Forest.node(forest, $place).line == 0 {
			$place = $place + 1
		}
		if $place <= Forest.count(forest) {
			Forest.node(forest, $place).line
		} else {
			0
		}
	}

	## Each tag of the tag union at a place, as a whole node (`Some(U64)`).
	union_tags : Forest, U64 -> List(U64)
	union_tags = |forest, at|
		Forest.kids(forest, at)
			.keep_if(|tags| Forest.kind(forest, tags) == "tags")
			.join_map(|tags| Forest.kids(forest, tags))

	## The name of a tag node: `Some` for `Some(U64)`.
	tag_name : Forest, U64 -> Str
	tag_name = |forest, at|
		if Forest.kind(forest, at) == "ty-apply" {
			Forest.name(forest, Forest.kid(forest, at, 1))
		} else {
			Forest.name(forest, at)
		}
}
