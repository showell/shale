## Roc's tree of a file, as `Shale.roc_tree!` gives it, read: a node by its
## place, its attributes and kids, a type written back as Roc writes it, and
## the module a file holds. It copies part of
## cookbook/roc/find_unclear_names/Tree.roc, on purpose, until the Roc
## recipes share a library; `render_type` is cookbook/roc/declared.shale's.
import Text

Tree := [].{

	## A parse tree's node, its fields named by the platform: `parent` and
	## `kids` are places in the tree counted from 1, the root's parent 0.
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

	## The node at a place counted from 1, which the tree's own `parent` and
	## `kids` always name.
	node_at : List(Tree.Node), U64 -> Tree.Node
	node_at = |nodes, place|
		if place == 0 {
			crash "find_bare_floats: the tree has no node 0"
		} else {
			match nodes.get(place - 1) {
				Ok(node) => node
				Err(_) => crash "find_bare_floats: the tree has no node ${place.to_str()}"
			}
		}

	## A node's attribute, `""` where the tree gives it none.
	attribute : Tree.Node, Str -> Str
	attribute = |node, name|
		match node.attrs.find_first(|attribute_entry| attribute_entry.name == name) {
			Ok(attribute_entry) => attribute_entry.value
			Err(_) => ""
		}

	## The place of a node's kid, the kids counted from 1.
	kid_at : Tree.Node, U64 -> U64
	kid_at = |node, number|
		match node.kids.get(number - 1) {
			Ok(kid) => kid
			Err(_) => crash "find_bare_floats: a ${node.kind} with no kid ${number.to_str()}"
		}

	## The place of a node's last kid.
	last_kid : Tree.Node -> U64
	last_kid = |node|
		match node.kids.last() {
			Ok(kid) => kid
			Err(_) => crash "find_bare_floats: a ${node.kind} with no kids"
		}

	## A dotted name's last part: `Num.F64` gives `F64`.
	last_part : Str -> Str
	last_part = |name|
		match name.split_last(".") {
			Ok(parts) => parts.after
			Err(_) => name
		}

	## The type at `at`, as Roc writes it: `List(U8)`, `{ id : U8 }`, `[A, B]`,
	## `(U8 -> Bool)`. The tree does not say which arrow a function type has,
	## so both come out as `->`.
	render_type : List(Tree.Node), U64 -> Str
	render_type = |nodes, at| {
		node = Tree.node_at(nodes, at)
		kind = node.kind
		name = Tree.attribute(node, "name")
		joined = |kids| Str.join_with(kids.map(|kid| Tree.render_type(nodes, kid)), ", ")
		if kind == "ty-apply" {
			"${Tree.render_type(nodes, Tree.kid_at(node, 1))}(${joined(node.kids.drop_first(1))})"
		} else if kind == "ty-fn" {
			"(${joined(node.kids.drop_last(1))} -> ${Tree.render_type(nodes, Tree.last_kid(node))})"
		} else if kind == "ty-record" {
			"{ ${joined(node.kids)} }"
		} else if kind == "ty-tuple" {
			"(${joined(node.kids)})"
		} else if kind == "tags" {
			joined(node.kids)
		} else if kind == "ty-tag-union" {
			"[${Tree.render_type(nodes, Tree.kid_at(node, 1))}]"
		} else if kind == "anno-record-field" {
			"${name} : ${Tree.render_type(nodes, Tree.kid_at(node, 1))}"
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

	## The module a .roc file holds, by its file name: the name without its
	## last four bytes, as Shale's `module_name` cuts `.roc`.
	module_name : Str -> Str
	module_name = |path| {
		bytes = Text.basename(path).to_utf8()
		if bytes.len() > 4 {
			Str.from_utf8_lossy(bytes.take_first(bytes.len() - 4))
		} else {
			""
		}
	}
}
