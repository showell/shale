## What a file declares at its top level or as a module type's member, read
## from its tree in file order (cookbook/roc/declared.shale's
## `declarations_of`): its types, its values and the record fields of its
## types and annotations, never a local's. Each carries `place`, its node's
## place in the tree, so the three kinds can be put back in file order.
import Text
import Tree

Declarations := {
	types : List(Declarations.TypeDeclared),
	values : List(Declarations.ValueDeclared),
	fields : List(Declarations.FieldDeclared),
}.{

	## A type declaration. `underlying` is the type it is built on as written
	## and `shape` the same as a shape (Tree.shape), both `""` when it is
	## built on none, as a module's `Cat := [].{ ... }` is not; `is_nominal`
	## says it is declared with `:=` or `::`, not as an alias with `:`;
	## `is_tag_union` and `is_function_type` say what it is built on.
	TypeDeclared : {
		path : Str,
		place : U64,
		line : U64,
		name : Str,
		owner : Str,
		underlying : Str,
		shape : Str,
		is_nominal : Bool,
		is_tag_union : Bool,
		is_function_type : Bool,
	}

	## A value: an annotation, or a definition with no annotation before it
	## (`annotated` false, its types `""`). `written_type` is its whole type,
	## `result_type` what it gives when it is a function, else the same, and
	## `first_type` its first argument's type, `""` when it takes none.
	## `has_text` says its definition is a string with something in it.
	ValueDeclared : {
		path : Str,
		place : U64,
		line : U64,
		name : Str,
		owner : Str,
		annotated : Bool,
		is_function : Bool,
		has_text : Bool,
		written_type : Str,
		result_type : Str,
		first_type : Str,
	}

	## A record field in a type or an annotation, with its type as written.
	FieldDeclared : {
		path : Str,
		place : U64,
		line : U64,
		name : Str,
		owner : Str,
		written_type : Str,
	}

	## A parsed file: its path, the file's nodes (its syntax tree, a node a
	## place) and its text cut at each newline.
	FileRead : {
		path : Str,
		nodes : List(Tree.Node),
		source_lines : List(Str),
	}

	## Nothing declared, to add files to.
	none_yet : Declarations
	none_yet = { types: [], values: [], fields: [] }

	## What `earlier` holds, then what `later` does.
	concat : Declarations, Declarations -> Declarations
	concat = |earlier, later| {
		types: earlier.types.concat(later.types),
		values: earlier.values.concat(later.values),
		fields: earlier.fields.concat(later.fields),
	}

	## What the file declares at its top level or as a module type's member,
	## each kind in file order. A definition of an annotated value sets the
	## annotation's `has_text`, and the last definition says.
	from_file : Declarations.FileRead -> Declarations
	from_file = |file| {
		nodes = file.nodes
		var $outer_answers = []
		var $declaration_lines = []
		var $annotation_places = Dict.empty()
		var $declared = Declarations.none_yet
		var $at = 0
		for node in nodes {
			$at = $at + 1
			at = $at
			place = { outer_answers: $outer_answers, parent: node.parent }
			is_outer = node.parent == 0 or is_outer_parent(nodes, place)
			$outer_answers = $outer_answers.append(is_outer)
			kind = node.kind
			is_declaration = kind == "s-type-anno" or kind == "s-type-decl"
			$declaration_lines = $declaration_lines.append(
				if is_outer and is_declaration {
					node.line
				} else {
					0
				},
			)
			if is_outer and kind == "s-type-decl" {
				match type_declared(file, at) {
					Ok(declared) => {
						$declared = { ..$declared, types: $declared.types.append(declared) }
					}
					Err(Unnamed) => {}
				}
			}
			field_place = { declaration_lines: $declaration_lines, at }
			if kind == "anno-record-field" and field_declaration_line(nodes, field_place) != 0 {
				field = {
					path: file.path,
					place: at,
					line: node.line,
					name: Tree.attribute(node, "name"),
					owner: owner_of(nodes, at),
					written_type: Tree.written_type(nodes, Tree.first_kid(node)),
				}
				$declared = { ..$declared, fields: $declared.fields.append(field) }
			}
			if is_outer and kind == "s-type-anno" {
				match annotated_value(file, at) {
					Ok(value) => {
						value_key = "${value.owner}.${value.name}"
						value_place = $declared.values.len()
						$annotation_places = $annotation_places.insert(value_key, value_place)
						$declared = { ..$declared, values: $declared.values.append(value) }
					}
					Err(OnlyWhere) => {}
				}
			}
			if is_outer and kind == "s-decl" and Tree.kid_kind(nodes, node, 1) == "p-ident" {
				name = Tree.attribute(Tree.node_at(nodes, Tree.first_kid(node)), "raw")
				owner = owner_of(nodes, at)
				values = match $annotation_places.get("${owner}.${name}") {
					Ok(annotation_place) => {
						definition = { place: annotation_place, has_text: is_text(nodes, node) }
						with_text($declared.values, definition)
					}
					Err(_) => {
						value = {
							path: file.path,
							place: at,
							line: node.line,
							name,
							owner,
							annotated: Bool.False,
							is_function: Tree.kid_kind(nodes, node, 2) == "e-lambda",
							has_text: Bool.False,
							written_type: "",
							result_type: "",
							first_type: "",
						}
						$declared.values.append(value)
					}
				}
				$declared = { ..$declared, values }
			}
		}
		$declared
	}
}

## Whether a node's `parent` lets it be outer: the parent is outer itself, as
## `outer_answers` holds it so far (a parent comes before its kids), and of a
## kind a definition may sit in and still be top-level or a module type's
## member, the type's `associated` block among them. Inside any other kind,
## such as a lambda's block, a definition is local.
is_outer_parent : List(Tree.Node), { outer_answers : List(Bool), parent : U64 } -> Bool
is_outer_parent = |nodes, place| {
	parent_is_outer = match place.outer_answers.get(place.parent - 1) {
		Ok(answer) => answer
		Err(_) => {
			parent = place.parent.to_str()
			crash "Declarations: node ${parent} comes after its kid"
		}
	}
	kind = Tree.node_at(nodes, place.parent).kind
	parent_is_outer and ["file", "statements", "s-type-decl", "associated"].contains(kind)
}

## The name of the type declaration around node `at`, or "" at the top level.
owner_of : List(Tree.Node), U64 -> Str
owner_of = |nodes, at| {
	var $up = Tree.node_at(nodes, at).parent
	while $up != 0 {
		node = Tree.node_at(nodes, $up)
		if node.kind == "s-type-decl" {
			return Tree.attribute(Tree.node_at(nodes, Tree.first_kid(node)), "name")
		}
		$up = node.parent
	}
	""
}

## The line of the declaration the record field at `at` belongs to, as
## `declaration_lines` holds it so far, or 0 when an expression, a pattern or
## a definition lies between them (the field is then a local's), or the
## declaration is not outer.
field_declaration_line : List(Tree.Node), { declaration_lines : List(U64), at : U64 } -> U64
field_declaration_line = |nodes, field_place| {
	var $up = Tree.node_at(nodes, field_place.at).parent
	while $up != 0 {
		node = Tree.node_at(nodes, $up)
		if node.kind.starts_with("e-") or node.kind.starts_with("p-") or node.kind == "s-decl" {
			return 0
		}
		if node.kind == "s-type-anno" or node.kind == "s-type-decl" {
			return match field_place.declaration_lines.get($up - 1) {
				Ok(line) => line
				Err(_) => crash "Declarations: a field's declaration comes after it"
			}
		}
		$up = node.parent
	}
	0
}

## Whether the definition `node` gives a string with something in it: `""`
## spans two columns.
is_text : List(Tree.Node), Tree.Node -> Bool
is_text = |nodes, node|
	match node.kids.get(1) {
		Ok(at) => {
			given = Tree.node_at(nodes, at)
			is_wide = given.end_line > given.line or given.end_column > given.column + 2
			given.kind == "e-string" and is_wide
		}
		Err(_) => Bool.False
	}

## A definition of an annotated value: the annotation's place among the
## values, and whether the definition is a string with something in it.
Definition : { place : U64, has_text : Bool }

## The values with the annotation at the definition's place given its
## `has_text`.
with_text : List(Declarations.ValueDeclared), Definition -> List(Declarations.ValueDeclared)
with_text = |values, definition| {
	past_end = "Declarations: an annotation's place is past the values"
	value = match values.get(definition.place) {
		Ok(found) => found
		Err(OutOfBounds) => crash past_end
	}
	match values.set(definition.place, { ..value, has_text: definition.has_text }) {
		Ok(changed) => changed
		Err(OutOfBounds) => crash past_end
	}
}

## The annotation at `at`, unless it is only a `where` clause.
annotated_value : Declarations.FileRead, U64 -> Try(Declarations.ValueDeclared, [OnlyWhere])
annotated_value = |file, at| {
	nodes = file.nodes
	node = Tree.node_at(nodes, at)
	match node.kids.first() {
		Err(_) => Err(OnlyWhere)
		Ok(type_at) => {
			type_node = Tree.node_at(nodes, type_at)
			if type_node.kind == "where" {
				return Err(OnlyWhere)
			}
			written_type = Tree.written_type(nodes, type_at)
			is_function = type_node.kind == "ty-fn"
			result_type = if is_function {
				Tree.written_type(nodes, Tree.last_kid(type_node))
			} else {
				written_type
			}
			first_type = if is_function and type_node.kids.len() > 1 {
				Tree.written_type(nodes, Tree.first_kid(type_node))
			} else {
				""
			}
			Ok({
				path: file.path,
				place: at,
				line: node.line,
				name: Tree.attribute(node, "name"),
				owner: owner_of(nodes, at),
				annotated: Bool.True,
				is_function,
				has_text: Bool.False,
				written_type,
				result_type,
				first_type,
			})
		}
	}
}

## The type declared at `at`, when it has a name. Whether it is nominal is
## read from the text just after its name, where `:=` or `::` stands.
type_declared : Declarations.FileRead, U64 -> Try(Declarations.TypeDeclared, [Unnamed])
type_declared = |file, at| {
	nodes = file.nodes
	node = Tree.node_at(nodes, at)
	name_node = Tree.node_at(nodes, Tree.first_kid(node))
	name = Tree.attribute(name_node, "name")
	if name == "" {
		return Err(Unnamed)
	}
	declared = {
		path: file.path,
		place: at,
		line: node.line,
		name,
		owner: owner_of(nodes, at),
		underlying: "",
		shape: "",
		is_nominal: Bool.False,
		is_tag_union: Bool.False,
		is_function_type: Bool.False,
	}
	match node.kids.get(1) {
		Ok(built_at) => {
			built_on = Tree.node_at(nodes, built_at)
			if built_on.kind == "associated" {
				Ok(declared)
			} else {
				name_line = Text.line_at(file.source_lines, name_node.end_line)
				after_name = Text.bytes_of(name_line, { start: name_node.end_column - 1, count: 4 })
				Ok({
					..declared,
					underlying: Tree.written_type(nodes, built_at),
					shape: Tree.shape(nodes, built_at),
					is_nominal: after_name.contains(":=") or after_name.contains("::"),
					is_tag_union: built_on.kind == "ty-tag-union",
					is_function_type: built_on.kind == "ty-fn",
				})
			}
		}
		Err(_) => Ok(declared)
	}
}
