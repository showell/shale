## A Roc file's imports, read from Roc's own tree of it, and the ones the file
## never uses. The rules are the recipe's `unused_import`
## (cookbook/roc/find_unused_imports.shale's header says what counts as a
## use); cookbook/roc/imports.shale holds the same rules for the Shale
## recipes that ask them of a file, rewrite_method_calls.shale among them.
Imports := [].{

	## A parsed file's node, as `Shale.roc_tree!` gives it (the platform names
	## its fields): `parent` and `kids` are places in the file's nodes
	## counted from 1.
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

	## An import line no name of the file uses: its line and how a finding
	## names it.
	UnusedImport : { line : U64, label : Str }

	## Each import of the file whose names it never uses, from its tree and
	## its source lines, which hold the qualifier of a pattern's tag that the
	## tree drops.
	unused_imports : List(Imports.SyntaxNode), List(Str) -> List(Imports.UnusedImport)
	unused_imports = |nodes, source_lines| {
		used = heads_used(nodes, source_lines)
		imports_of(nodes)
			.drop_if(|line_import| line_import.names.any(|name| used.contains(name)))
			.map(|line_import| { line: line_import.line, label: line_import.label })
	}
}

## An import line: its line, how a finding names it, and the names a use of
## it may start with.
Import : {
	line : U64,
	label : Str,
	names : List(Str),
}

## The node at a place of the file's nodes; a place the tree gives always
## holds one.
node_at : List(Imports.SyntaxNode), U64 -> Imports.SyntaxNode
node_at = |nodes, at|
	match nodes.get(at - 1) {
		Ok(node) => node
		Err(_) => crash "find_unused_imports: the nodes has no node ${at.to_str()}"
	}

## A node's attribute, `""` where the tree gives it none.
attribute : Imports.SyntaxNode, Str -> Str
attribute = |node, name|
	match node.attrs.find_first(|attribute_entry| attribute_entry.name == name) {
		Ok(attribute_entry) => attribute_entry.value
		Err(_) => ""
	}

## The kids of a node, as nodes.
kid_nodes : List(Imports.SyntaxNode), Imports.SyntaxNode -> List(Imports.SyntaxNode)
kid_nodes = |nodes, node|
	node.kids.map(|at| node_at(nodes, at))

# ── The imports

## Each import of the file, in order: `import pkg.Mod`, its alias and what it
## exposes, and each file import, `import "page.html" as page : Str`.
imports_of : List(Imports.SyntaxNode) -> List(Import)
imports_of = |nodes|
	nodes.join_map(
		|node|
			if node.kind == "s-import" {
				[module_import(nodes, node)]
			} else if node.kind == "s-file-import" {
				file_imports(nodes, node)
			} else {
				[]
			},
	)

## A module's import. Its name is the alias of `import pkg.Mod as M`, else
## the last part of the module's path; a name it exposes is one too.
module_import : List(Imports.SyntaxNode), Imports.SyntaxNode -> Import
module_import = |nodes, node| {
	path = attribute(node, "raw")
	alias = attribute(node, "alias")
	name = if alias == "" {
		path.split_on(".").last() ?? path
	} else {
		alias
	}
	label = if alias == "" {
		"`${path}`"
	} else {
		"`${path}` as `${alias}`"
	}
	exposing_lists = kid_nodes(nodes, node).keep_if(|kid| kid.kind == "exposing")
	exposed = exposing_lists.join_map(|list| exposed_names(nodes, list))
	{
		line: node.line,
		label,
		names: exposed.prepend(name),
	}
}

## The names an `exposing [...]` list names: a type's `text`, and each
## value's name, held by the value's own kids.
exposed_names : List(Imports.SyntaxNode), Imports.SyntaxNode -> List(Str)
exposed_names = |nodes, list|
	kid_nodes(nodes, list).join_map(
		|entry|
			if entry.kind == "exposed-upper-ident" {
				[attribute(entry, "text")]
			} else if entry.kind == "exposed-lower-ident" {
				kid_nodes(nodes, entry).map(|name| attribute(name, "value"))
			} else {
				[]
			},
	)

## A file import's name, used when the file refers to it.
file_imports : List(Imports.SyntaxNode), Imports.SyntaxNode -> List(Import)
file_imports = |nodes, node|
	kid_nodes(nodes, node)
		.keep_if(|kid| kid.kind == "name")
		.map(
			|kid| {
				name = attribute(kid, "value")
				{
					line: node.line,
					label: "the file import `${name}`",
					names: [name],
				}
			},
		)

# ── The uses

## The first part of every value, tag and type name the file uses. A bare
## tag (`Other`, in an expression, a pattern or a tag union type) names no
## module, so only a qualified one counts, or one that is a nominal type's
## name (`Point.{ x: 0 }`). A platform header's `hosted { "sym": Host.f! }`
## entry uses its module.
heads_used : List(Imports.SyntaxNode), List(Str) -> Set(Str)
heads_used = |nodes, source_lines| {
	union_tags = kids_of_kind(nodes, "tags")
	nominal_names = kids_of_kind(nodes, "mapper")
	names = nodes.map_with_index(
		|node, index| {
			at = index + 1
			if node.kind == "e-ident" {
				attribute(node, "raw")
			} else if node.kind == "e-tag" {
				tag_use(node, nominal_names.contains(at))
			} else if node.kind == "ty" and !(union_tags.contains(at)) {
				attribute(node, "name")
			} else if node.kind == "symbol-map-entry" {
				attribute(node, "module")
			} else if node.kind == "p-tag" {
				pattern_tag_use(node, source_lines)
			} else {
				""
			}
		},
	)
	Set.from_list(names.drop_if(Str.is_empty).map(head_of))
}

## An expression's tag, when it names a module: qualified, or a nominal
## type's name.
tag_use : Imports.SyntaxNode, Bool -> Str
tag_use = |node, names_nominal| {
	raw = attribute(node, "raw")
	if raw.contains(".") or names_nominal {
		raw
	} else {
		""
	}
}

## A pattern's tag, when it is qualified. roc-ast drops a pattern tag's
## qualifier (`Color.Red =>` comes as `.Red`), so the tag is read from the
## source at the pattern's column.
pattern_tag_use : Imports.SyntaxNode, List(Str) -> Str
pattern_tag_use = |node, source_lines| {
	raw = attribute(node, "raw")
	written = if raw.starts_with(".") {
		source_line = match source_lines.get(node.line - 1) {
			Ok(text) => text
			Err(_) => crash "find_unused_imports: a pattern's tag past the file's end"
		}
		written_name(source_line.to_utf8().drop_first(node.col - 1))
	} else {
		raw
	}
	if written.contains(".") {
		written
	} else {
		""
	}
}

## The dotted name the bytes start with: a letter or `_`, then letters,
## digits, `_` and `.`; `""` when they start with none.
written_name : List(U8) -> Str
written_name = |bytes|
	match bytes.first() {
		Ok(first) if is_letter(first) or first == '_' => {
			length = bytes.find_first_index(|byte| !(is_name_byte(byte))) ?? bytes.len()
			match Str.from_utf8(bytes.take_first(length)) {
				Ok(name) => name
				Err(_) => crash "find_unused_imports: a name of ASCII bytes is UTF-8"
			}
		}
		_ => ""
	}

is_letter : U8 -> Bool
is_letter = |byte|
	(byte >= 'a' and byte <= 'z') or (byte >= 'A' and byte <= 'Z')

is_name_byte : U8 -> Bool
is_name_byte = |byte|
	is_letter(byte) or (byte >= '0' and byte <= '9') or byte == '_' or byte == '.'

## The first part of a dotted name: `Mod` of `Mod.Type.name`.
head_of : Str -> Str
head_of = |name|
	name.split_on(".").first() ?? name

## The places of the nodes that are kids of a node of the kind.
kids_of_kind : List(Imports.SyntaxNode), Str -> Set(U64)
kids_of_kind = |nodes, kind|
	Set.from_list(nodes.keep_if(|node| node.kind == kind).join_map(|node| node.kids))
