## Roc's tree of a file, as `Shale.roc_tree!` gives it, read: a node by its
## place, its attributes and kids, a type's shape, and whether the file is an
## app. It copies part of cookbook/roc/find_unclear_names/Tree.roc, on
## purpose, until the Roc recipes share a library; `shape` is
## cookbook/roc/declared.shale's `render_type` asked for a shape.
import Text

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
			Err(_) => crash "check_families: the tree has no node ${at.to_str()}"
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
			Err(_) => crash "check_families: a ${node.kind} with no kids"
		}

	## The kind of a node's first kid, `""` when it has none.
	first_kid_kind : List(Tree.Node), Tree.Node -> Str
	first_kid_kind = |nodes, node|
		match node.kids.first() {
			Ok(at) => Tree.node_at(nodes, at).kind
			Err(_) => ""
		}

	## The type at `at` as a shape: written as Roc writes it (`List(U8)`,
	## `{ id : U8 }`, `[A, B]`, `(U8 -> Bool)`, both arrows as `->`), each
	## name cut to its last part and a record's fields and a union's tags
	## sorted, so two copies of one type read alike.
	shape : List(Tree.Node), U64 -> Str
	shape = |nodes, at| {
		node = Tree.node_at(nodes, at)
		kind = node.kind
		name = Tree.attribute(node, "name")
		shapes_of = |kids| kids.map(|kid| Tree.shape(nodes, kid))
		joined = |kids| Str.join_with(shapes_of(kids), ", ")
		sorted_and_joined = |kids| Str.join_with(Text.sort_bytewise(shapes_of(kids)), ", ")
		if kind == "ty-apply" {
			"${Tree.shape(nodes, Tree.first_kid(node))}(${joined(node.kids.drop_first(1))})"
		} else if kind == "ty-fn" {
			"(${joined(node.kids.drop_last(1))} -> ${result_shape(nodes, node)})"
		} else if kind == "ty-record" {
			"{ ${sorted_and_joined(node.kids)} }"
		} else if kind == "ty-tuple" {
			"(${joined(node.kids)})"
		} else if kind == "tags" {
			sorted_and_joined(node.kids)
		} else if kind == "ty-tag-union" {
			"[${Tree.shape(nodes, Tree.first_kid(node))}]"
		} else if kind == "anno-record-field" {
			"${name} : ${Tree.shape(nodes, Tree.first_kid(node))}"
		} else if name != "" {
			last_part(name)
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

	## Whether the file is an app, with an `app` header.
	is_app : List(Tree.Node) -> Bool
	is_app = |nodes| nodes.any(|node| node.kind == "app")
}

## The shape of what the function type `node` gives, its last kid.
result_shape : List(Tree.Node), Tree.Node -> Str
result_shape = |nodes, node|
	match node.kids.last() {
		Ok(result) => Tree.shape(nodes, result)
		Err(_) => crash "check_families: a function type with no kids"
	}

## A dotted name's last part: `Geom.Pose` gives `Pose`.
last_part : Str -> Str
last_part = |name|
	match name.split_last(".") {
		Ok(parts) => parts.after
		Err(_) => name
	}
