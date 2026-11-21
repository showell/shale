## Roc's tree of a file, as `Shale.roc_tree!` gives it: a node by its place,
## its kids and its attributes. The nodes come in source order, each before
## its kids, so a node's place is less than every kid's. This is the part of
## cookbook/roc/find_unclear_names/Tree.roc this recipe reads, copied until
## the recipes share a library; the node's type is named as
## cookbook/roc/find_unused_imports/Imports.roc names it.
Tree := [].{

	## A node of Roc's tree, its fields named by the platform: `parent` and
	## `kids` are places in the tree counted from 1, the root's parent 0;
	## `depth` is how far below the root it is.
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
			Err(_) => crash "find_deep_calls: the tree has no node ${place.to_str()}"
		}

	## A node's attribute, by its name, when the tree gives it one.
	attribute : Tree.SyntaxNode, Str -> Try(Str, [NoSuchAttribute])
	attribute = |node, name|
		match node.attrs.find_first(|attribute_entry| attribute_entry.name == name) {
			Ok(attribute_entry) => Ok(attribute_entry.value)
			Err(_) => Err(NoSuchAttribute)
		}

	## The first kid's place: an apply's function, a receiver's value.
	first_kid : Tree.SyntaxNode -> U64
	first_kid = |node|
		match node.kids.first() {
			Ok(kid) => kid
			Err(_) => crash "find_deep_calls: a ${node.kind} with no kids"
		}
}
