## Roc's tree of a file, as `Shale.roc_tree!` gives it, read: a node by its
## place, its attributes and kids, a type written back as Roc writes it, and
## whether the file is an app.
import Text

Tree := [].{

	## A node of Roc's tree: `parent` and `kids` are places in the tree
	## counted from 1, the root's parent 0.
	Node : { kind : Str, depth : U64, parent : U64, kids : List(U64), line : U64, col : U64, end_line : U64, end_col : U64, attrs : List({ name : Str, value : Str }) }

	node_at : List(Tree.Node), U64 -> Tree.Node
	node_at = |tree, at|
		match List.get(tree, at - 1) {
			Ok(n) => n
			Err(_) => crash "find_unclear_names: the tree has no node ${U64.to_str(at)}"
		}

	## A node's attribute, `""` where the tree gives it none.
	attribute : Tree.Node, Str -> Str
	attribute = |n, name|
		match List.find_first(n.attrs, |a| a.name == name) {
			Ok(a) => a.value
			Err(_) => ""
		}

	first_kid : Tree.Node -> U64
	first_kid = |n|
		match List.first(n.kids) {
			Ok(k) => k
			Err(_) => crash "find_unclear_names: a ${n.kind} with no kids"
		}

	last_kid : Tree.Node -> U64
	last_kid = |n|
		match List.last(n.kids) {
			Ok(k) => k
			Err(_) => crash "find_unclear_names: a ${n.kind} with no kids"
		}

	kid_kind : List(Tree.Node), Tree.Node, U64 -> Str
	kid_kind = |tree, n, k|
		match List.get(n.kids, k) {
			Ok(at) => Tree.node_at(tree, at).kind
			Err(_) => ""
		}

	## A dotted type's last part.
	last_part : Str -> Str
	last_part = |type_name| Text.after_last(type_name, ".")

	## The type at `at`, as Roc writes it: `List(U8)`, `{ id : U8 }`, `[A, B]`,
	## `(U8 -> Bool)`; both arrows come out as `->`.
	render_type : List(Tree.Node), U64 -> Str
	render_type = |tree, at| {
		n = Tree.node_at(tree, at)
		kind = n.kind
		name = Tree.attribute(n, "name")
		parts = |kids| Str.join_with(kids.map(|c| Tree.render_type(tree, c)), ", ")
		if kind == "ty-apply" {
			"${Tree.render_type(tree, Tree.first_kid(n))}(${parts(List.drop_first(n.kids, 1))})"
		} else if kind == "ty-fn" {
			"(${parts(List.drop_last(n.kids, 1))} -> ${Tree.render_type(tree, Tree.last_kid(n))})"
		} else if kind == "ty-record" {
			"{ ${parts(n.kids)} }"
		} else if kind == "ty-tuple" {
			"(${parts(n.kids)})"
		} else if kind == "tags" {
			parts(n.kids)
		} else if kind == "ty-tag-union" {
			"[${Tree.render_type(tree, Tree.first_kid(n))}]"
		} else if kind == "anno-record-field" {
			"${name} : ${Tree.render_type(tree, Tree.first_kid(n))}"
		} else if name != "" {
			name
		} else if kind == "_" {
			"_"
		} else {
			raw = Tree.attribute(n, "raw")
			if raw != "" {
				raw
			} else {
				"(${kind})"
			}
		}
	}

	module_name : Str -> Str
	module_name = |path| Str.drop_suffix(Text.basename(path), ".roc")

	is_app : List(Tree.Node) -> Bool
	is_app = |tree| List.any(tree, |n| n.kind == "app")
}
