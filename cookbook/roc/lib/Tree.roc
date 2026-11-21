## Roc's tree of a file, as `Shale.roc_tree!` gives it, read: a node by its
## place, its attributes and kids, a type written back as Roc writes it or as
## a shape, the module a file holds, and whether the file is an app.
import Text

Tree := [].{

	## A node of Roc's tree, its fields named by the platform: `parent` and
	## `kids` are places in the tree counted from 1, the root's parent 0;
	## `depth` is how far below the root it is, and `line` is 0 for a node
	## with no span.
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
			crash "Tree.node_at: the tree has no node 0"
		} else {
			match nodes.get(place - 1) {
				Ok(node) => node
				Err(_) => crash "Tree.node_at: the tree has no node ${place.to_str()}"
			}
		}

	## A node's attribute, by its name, when the tree gives it one.
	find_attribute : Tree.Node, Str -> Try(Str, [NoSuchAttribute])
	find_attribute = |node, name|
		match node.attrs.find_first(|attribute_entry| attribute_entry.name == name) {
			Ok(attribute_entry) => Ok(attribute_entry.value)
			Err(_) => Err(NoSuchAttribute)
		}

	## A node's attribute, `""` where the tree gives it none.
	attribute : Tree.Node, Str -> Str
	attribute = |node, name| Tree.find_attribute(node, name) ?? ""

	## The place of a node's kid, the kids counted from 1.
	kid_at : Tree.Node, U64 -> U64
	kid_at = |node, number|
		match node.kids.get(number - 1) {
			Ok(kid) => kid
			Err(_) => crash "Tree.kid_at: a ${node.kind} with no kid ${number.to_str()}"
		}

	## The place of a node's first kid: an apply's function, a receiver's
	## value.
	first_kid : Tree.Node -> U64
	first_kid = |node|
		match node.kids.first() {
			Ok(kid) => kid
			Err(_) => crash "Tree.first_kid: a ${node.kind} with no kids"
		}

	## The place of a node's last kid.
	last_kid : Tree.Node -> U64
	last_kid = |node|
		match node.kids.last() {
			Ok(kid) => kid
			Err(_) => crash "Tree.last_kid: a ${node.kind} with no kids"
		}

	## The kind of a node's kid, the kids counted from 1, `""` when it has no
	## such kid.
	kid_kind : List(Tree.Node), Tree.Node, U64 -> Str
	kid_kind = |nodes, node, number|
		if number == 0 {
			""
		} else {
			match node.kids.get(number - 1) {
				Ok(kid) => Tree.node_at(nodes, kid).kind
				Err(_) => ""
			}
		}

	## A dotted name's last part: `Geom.Pose` gives `Pose`.
	last_part : Str -> Str
	last_part = |name| Text.after_last(name, ".")

	## The type at `at`, as Roc writes it (cookbook/roc/declared.shale's
	## `render_type`): `List(U8)`, `{ id : U8 }`, `[A, B]`, `(U8 -> Bool)`. The
	## tree does not say which arrow a function type has, so both come out as
	## `->`.
	written_type : List(Tree.Node), U64 -> Str
	written_type = |nodes, at| type_text(nodes, at, AsWritten)

	## The type at `at` as a shape: written as Roc writes it, each name cut to
	## its last part and a record's fields and a union's tags sorted, so two
	## copies of one type read alike.
	shape : List(Tree.Node), U64 -> Str
	shape = |nodes, at| type_text(nodes, at, AsShape)

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

	## Whether the file is an app, with an `app` header.
	is_app : List(Tree.Node) -> Bool
	is_app = |nodes| nodes.any(|node| node.kind == "app")
}

## How a type is written: as the code writes it, or as a shape, its names cut
## to their last parts and its fields and tags sorted.
TypeStyle : [AsWritten, AsShape]

## The type at `at`, written in `style`.
type_text : List(Tree.Node), U64, TypeStyle -> Str
type_text = |nodes, at, style| {
	node = Tree.node_at(nodes, at)
	kind = node.kind
	name = Tree.attribute(node, "name")
	texts_of = |kids| kids.map(|kid| type_text(nodes, kid, style))
	joined = |kids| Str.join_with(texts_of(kids), ", ")
	members_joined = |kids|
		match style {
			AsWritten => joined(kids)
			AsShape => Str.join_with(Text.sort_bytewise(texts_of(kids)), ", ")
		}
	first = |{}| type_text(nodes, Tree.first_kid(node), style)
	if kind == "ty-apply" {
		"${first({})}(${joined(node.kids.drop_first(1))})"
	} else if kind == "ty-fn" {
		result = type_text(nodes, Tree.last_kid(node), style)
		"(${joined(node.kids.drop_last(1))} -> ${result})"
	} else if kind == "ty-record" {
		"{ ${members_joined(node.kids)} }"
	} else if kind == "ty-tuple" {
		"(${joined(node.kids)})"
	} else if kind == "tags" {
		members_joined(node.kids)
	} else if kind == "ty-tag-union" {
		"[${first({})}]"
	} else if kind == "anno-record-field" {
		"${name} : ${first({})}"
	} else if name != "" {
		match style {
			AsWritten => name
			AsShape => Tree.last_part(name)
		}
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
