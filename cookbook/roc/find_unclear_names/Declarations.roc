## What a file declares at its top level or as a module type's member, read
## from its tree in file order (cookbook/roc/declared.shale's
## `declarations_of`): what the checks judge.
import Text
import Tree

Declarations := [].{

	## A type, a value (an annotation, or a definition with none above it) or a
	## record field of a type or an annotation.
	Declared : {
		line : U64,
		kind : [Type, Value, Field],
		name : Str,
		owner : Str,
		annotated : Bool,
		fn : Bool,
		result : Str,
		first : Str,
		is_nominal : Bool,
		underlying : Str,
	}

	## Everything the file declares at its top level or as a module type's
	## member, in file order.
	of : List(Tree.Node), List(Str) -> List(Declarations.Declared)
	of = |tree, src| {
		var $outer = []
		var $declaration_lines = []
		var $annotated = Set.empty()
		var $out = []
		var $i = 0
		for n in tree {
			$i = $i + 1
			i = $i
			parent_outer = n.parent != 0 and (List.get($outer, n.parent - 1) ?? Bool.False) and is_outer_kind(Tree.node_at(tree, n.parent).kind)
			is_outer = n.parent == 0 or parent_outer
			$outer = $outer.append(is_outer)
			kind = n.kind
			$declaration_lines = $declaration_lines.append(
				if is_outer and (kind == "s-type-anno" or kind == "s-type-decl") {
					n.line
				} else {
					0
				},
			)
			if is_outer and kind == "s-type-decl" {
				$out = List.concat($out, declared_type(tree, src, i))
			}
			if kind == "anno-record-field" and field_declaration_line(tree, $declaration_lines, i) != 0 {
				$out = $out.append(plain(n.line, Field, Tree.attribute(n, "name"), owner_of(tree, i)))
			}
			if is_outer and kind == "s-type-anno" {
				for d in annotated_value(tree, i) {
					$out = $out.append(d)
					$annotated = Set.insert($annotated, "${d.owner}.${d.name}")
				}
			}
			if is_outer and kind == "s-decl" and Tree.kid_kind(tree, n, 0) == "p-ident" {
				name = Tree.attribute(Tree.node_at(tree, Tree.first_kid(n)), "raw")
				owner = owner_of(tree, i)
				if !(Set.contains($annotated, "${owner}.${name}")) {
					$out = $out.append({ ..plain(n.line, Value, name, owner), fn: Tree.kid_kind(tree, n, 1) == "e-lambda" })
				}
			}
		}
		$out
	}
}

## The kinds a definition may sit in and still be top-level or a module
## type's member.
is_outer_kind : Str -> Bool
is_outer_kind = |kind| kind == "file" or kind == "statements" or kind == "s-type-decl" or kind == "associated"

## The name of the type declaration around node `at`, or "" at the top level.
owner_of : List(Tree.Node), U64 -> Str
owner_of = |tree, at| {
	var $k = Tree.node_at(tree, at).parent
	while $k != 0 {
		n = Tree.node_at(tree, $k)
		if n.kind == "s-type-decl" {
			return Tree.attribute(Tree.node_at(tree, Tree.first_kid(n)), "name")
		}
		$k = n.parent
	}
	""
}

## The line of the declaration a record field belongs to, as
## `declaration_lines` holds it, or 0 when an expression, a pattern or a
## definition lies between them (the field is then a local's).
field_declaration_line : List(Tree.Node), List(U64), U64 -> U64
field_declaration_line = |tree, declaration_lines, at| {
	var $k = Tree.node_at(tree, at).parent
	while $k != 0 {
		n = Tree.node_at(tree, $k)
		if Str.starts_with(n.kind, "e-") or Str.starts_with(n.kind, "p-") or n.kind == "s-decl" {
			return 0
		}
		if n.kind == "s-type-anno" or n.kind == "s-type-decl" {
			return List.get(declaration_lines, $k - 1) ?? 0
		}
		$k = n.parent
	}
	0
}

plain : U64, [Type, Value, Field], Str, Str -> Declarations.Declared
plain = |line, kind, name, owner| { line, kind, name, owner, annotated: Bool.False, fn: Bool.False, result: "", first: "", is_nominal: Bool.False, underlying: "" }

## The annotation at `i`, unless it is only a `where` clause.
annotated_value : List(Tree.Node), U64 -> List(Declarations.Declared)
annotated_value = |tree, i| {
	n = Tree.node_at(tree, i)
	match List.first(n.kids) {
		Err(_) => []
		Ok(type_at) => {
			type_node = Tree.node_at(tree, type_at)
			if type_node.kind == "where" {
				return []
			}
			base = { ..plain(n.line, Value, Tree.attribute(n, "name"), owner_of(tree, i)), annotated: Bool.True, result: Tree.render_type(tree, type_at) }
			if type_node.kind == "ty-fn" {
				first = if List.len(type_node.kids) > 1 {
					Tree.render_type(tree, Tree.first_kid(type_node))
				} else {
					""
				}
				[{ ..base, fn: Bool.True, result: Tree.render_type(tree, Tree.last_kid(type_node)), first }]
			} else {
				[base]
			}
		}
	}
}

## The type declared at `i`: `is_nominal` says it is declared with `:=` or
## `::`, and `underlying` is the type it is built on.
declared_type : List(Tree.Node), List(Str), U64 -> List(Declarations.Declared)
declared_type = |tree, src, i| {
	n = Tree.node_at(tree, i)
	name_node = Tree.node_at(tree, Tree.first_kid(n))
	name = Tree.attribute(name_node, "name")
	if name == "" {
		return []
	}
	base = plain(n.line, Type, name, owner_of(tree, i))
	match List.get(n.kids, 1) {
		Ok(built_at) =>
			if Tree.node_at(tree, built_at).kind == "associated" {
				[base]
			} else {
				between_text = Text.bytes_of(Text.line_at(src, name_node.end_line), name_node.end_col - 1, 4)
				is_nominal = Str.contains(between_text, ":=") or Str.contains(between_text, "::")
				[{ ..base, is_nominal, underlying: Tree.render_type(tree, built_at) }]
			}
		Err(_) => [base]
	}
}
