## Roc's tree of a file, as `Shale.roc_tree!` gives it, read: a node by its
## place, its attributes and kids, and a type written back as Roc writes it
## (cookbook/roc/declared.shale's `render_type`). It copies part of
## cookbook/roc/find_unclear_names/Tree.roc, on purpose, until the Roc
## recipes share a library.
Tree := [].{

	## A parse tree's node, as the platform's `roc_tree!` gives it: `parent`
	## and `kids` are places in the tree counted from 1, the root's parent 0.
	Node : {
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

	node_at : List(Tree.Node), U64 -> Tree.Node
	node_at = |nodes, at|
		match nodes.get(at - 1) {
			Ok(node) => node
			Err(_) => crash "check_name_types: the tree has no node ${at.to_str()}"
		}

	## A node's attribute, `""` where the tree gives it none.
	attribute : Tree.Node, Str -> Str
	attribute = |node, name|
		match node.attrs.find_first(|a| a.name == name) {
			Ok(found) => found.value
			Err(_) => ""
		}

	first_kid : Tree.Node -> U64
	first_kid = |node|
		match node.kids.first() {
			Ok(kid) => kid
			Err(_) => crash "check_name_types: a ${node.kind} with no kids"
		}

	last_kid : Tree.Node -> U64
	last_kid = |node|
		match node.kids.last() {
			Ok(kid) => kid
			Err(_) => crash "check_name_types: a ${node.kind} with no kids"
		}

	## The kind of a node's first kid, `""` when it has none.
	first_kid_kind : List(Tree.Node), Tree.Node -> Str
	first_kid_kind = |nodes, node| kind_at(nodes, node.kids.get(0))

	## The kind of a node's second kid, `""` when it has none.
	second_kid_kind : List(Tree.Node), Tree.Node -> Str
	second_kid_kind = |nodes, node| kind_at(nodes, node.kids.get(1))

	## A dotted type's last part: `Geom.Distance` gives `Distance`.
	last_part : Str -> Str
	last_part = |type_name|
		match type_name.split_last(".") {
			Ok(parts) => parts.after
			Err(_) => type_name
		}

	## The type at `at`, as Roc writes it: `List(U8)`, `{ id : U8 }`, `[A, B]`,
	## `(U8 -> Bool)`; both arrows come out as `->`.
	written_type : List(Tree.Node), U64 -> Str
	written_type = |nodes, at| {
		node = Tree.node_at(nodes, at)
		kind = node.kind
		name = Tree.attribute(node, "name")
		joined = |kids| Str.join_with(kids.map(|kid| Tree.written_type(nodes, kid)), ", ")
		if kind == "ty-apply" {
			"${Tree.written_type(nodes, Tree.first_kid(node))}(${joined(node.kids.drop_first(1))})"
		} else if kind == "ty-fn" {
			result = Tree.written_type(nodes, Tree.last_kid(node))
			"(${joined(node.kids.drop_last(1))} -> ${result})"
		} else if kind == "ty-record" {
			"{ ${joined(node.kids)} }"
		} else if kind == "ty-tuple" {
			"(${joined(node.kids)})"
		} else if kind == "tags" {
			joined(node.kids)
		} else if kind == "ty-tag-union" {
			"[${Tree.written_type(nodes, Tree.first_kid(node))}]"
		} else if kind == "anno-record-field" {
			"${name} : ${Tree.written_type(nodes, Tree.first_kid(node))}"
		} else if name != "" {
			name
		} else if kind == "_" {
			"_"
		} else {
			raw = Tree.attribute(node, "raw")
			if raw != "" {
				raw
			} else {
				"(${kind})"
			}
		}
	}
}

## The kind of the node at a kid's place, `""` when there is no such kid.
kind_at : List(Tree.Node), Try(U64, [OutOfBounds]) -> Str
kind_at = |nodes, found|
	match found {
		Ok(at) => Tree.node_at(nodes, at).kind
		Err(_) => ""
	}
