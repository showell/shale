## What a file declares at its top level or as a module type's member, read
## from its tree in file order: the types, each with its shape, and the
## values' names. It keeps what check_families reads of
## cookbook/roc/declared.shale's `declarations_of`;
## cookbook/roc/find_unclear_names/Declarations.roc reads more of it for that
## recipe.
import Tree

Declarations := [].{

	## A type declaration: its name, the type it is a member of (`""` at the
	## top level), its line, and the shape of the type it is built on (`""`
	## when it is built on none, as a module's `Cat := [].{ ... }` is not).
	TypeDeclaration : {
		name : Str,
		owner : Str,
		line : U64,
		shape : Str,
	}

	## A value declared by an annotation or a definition, and the type it is a
	## member of.
	ValueName : { name : Str, owner : Str }

	## What one file declares, in file order. A value annotated and defined is
	## there twice.
	FileDeclarations : {
		types : List(Declarations.TypeDeclaration),
		values : List(Declarations.ValueName),
	}

	## Every type and value the file declares at its top level or as a module
	## type's member.
	from_tree : List(Tree.Node) -> Declarations.FileDeclarations
	from_tree = |nodes| {
		var $outer_answers = []
		var $types = []
		var $values = []
		var $at = 0
		for node in nodes {
			$at = $at + 1
			place = { outer_answers: $outer_answers, parent: node.parent }
			is_outer = node.parent == 0 or is_outer_parent(nodes, place)
			$outer_answers = $outer_answers.append(is_outer)
			if is_outer {
				$types = $types.concat(type_declared(nodes, $at))
				$values = $values.concat(value_declared(nodes, $at))
			}
		}
		{ types: $types, values: $values }
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
		Err(_) => crash "check_families: node ${place.parent.to_str()} comes after its kid"
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

## The type declared at `at`, when it is a type declaration with a name.
type_declared : List(Tree.Node), U64 -> List(Declarations.TypeDeclaration)
type_declared = |nodes, at| {
	node = Tree.node_at(nodes, at)
	if node.kind != "s-type-decl" {
		return []
	}
	name = Tree.attribute(Tree.node_at(nodes, Tree.first_kid(node)), "name")
	if name == "" {
		return []
	}
	shape = match node.kids.get(1) {
		Ok(built_on) =>
			if Tree.node_at(nodes, built_on).kind == "associated" {
				""
			} else {
				Tree.shape(nodes, built_on)
			}
		Err(_) => ""
	}
	[
		{
			name,
			owner: owner_of(nodes, at),
			line: node.line,
			shape,
		},
	]
}

## The value named at `at`: an annotation that is more than a `where`
## clause, or a definition of a plain name.
value_declared : List(Tree.Node), U64 -> List(Declarations.ValueName)
value_declared = |nodes, at| {
	node = Tree.node_at(nodes, at)
	first_kind = Tree.first_kid_kind(nodes, node)
	if node.kind == "s-type-anno" and first_kind != "" and first_kind != "where" {
		[{ name: Tree.attribute(node, "name"), owner: owner_of(nodes, at) }]
	} else if node.kind == "s-decl" and first_kind == "p-ident" {
		name = Tree.attribute(Tree.node_at(nodes, Tree.first_kid(node)), "raw")
		[{ name, owner: owner_of(nodes, at) }]
	} else {
		[]
	}
}
