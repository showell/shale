## Roc's tree of a file, as `Shale.roc_tree!` gives it: a node by its place,
## its attributes, and the line it sits on. This is the part of
## cookbook/roc/find_deep_calls/Tree.roc this recipe reads, copied until the
## recipes share a library; `line_of` is the Shale original's `node_line`.
Tree := [].{

	## A node of Roc's tree, its fields named by the platform: `parent` and
	## `kids` are places in the tree counted from 1, the root's parent 0;
	## `line` is 0 for a node with no span.
	SyntaxNode : {
		kind : Str,
		depth : U64,
		parent : U64,
		kids : List(U64),
		line : U64,
		col : U64,
		end_line : U64,
		end_col : U64,
		attrs : List({ name : Str, value : Str }),
	}

	## The node at a place counted from 1, which the tree's own `parent` and
	## `kids` always name.
	node_at : List(Tree.SyntaxNode), U64 -> Tree.SyntaxNode
	node_at = |syntax_nodes, place|
		match syntax_nodes.get(place - 1) {
			Ok(node) => node
			Err(_) => crash "check_vocabulary: the tree has no node ${place.to_str()}"
		}

	## A node's attribute, `""` where the tree gives it none.
	attribute : Tree.SyntaxNode, Str -> Str
	attribute = |node, name|
		match node.attrs.find_first(|attribute_entry| attribute_entry.name == name) {
			Ok(attribute_entry) => attribute_entry.value
			Err(_) => ""
		}

	## The line of the node at `place`, or of the nearest node around it that
	## has one; 0 when none does.
	line_of : List(Tree.SyntaxNode), U64 -> U64
	line_of = |syntax_nodes, place| {
		var $place = place
		var $line = 0
		while $place != 0 and $line == 0 {
			node = Tree.node_at(syntax_nodes, $place)
			if node.line == 0 {
				$place = node.parent
			} else {
				$line = node.line
			}
		}
		$line
	}
}
