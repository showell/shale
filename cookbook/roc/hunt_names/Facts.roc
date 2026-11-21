## The facts the reading of the files gathers for the checks judged over the
## whole tree (cookbook/roc/declared.shale's `facts`): each file's declared
## keywords, the tag unions types name, the aliases of `Bool`, the kinds of
## types a file declares, each type name a file declares, and the type and
## field names `container` reads.
import lib.Text
import lib.Tree
import ShortNames
import TypeText

Facts := [].{

	## A type's or a record field's name, for `container`: its words, the
	## doc above its declaration, lowercased; a field's also whether it is a
	## letter of a set beside its siblings, what it gives and its first
	## argument's type when it is a function.
	Named : {
		path : Str,
		line : U64,
		name : Str,
		words : List(Str),
		is_field : Bool,
		doc : Str,
		is_letter_of_set : Bool,
		gives : Str,
		is_function : Bool,
		first : Str,
	}

	## What the reading gathers: each file's `# keywords:` words, by path;
	## each type's tag union, by the type's name; the names of aliases of
	## `Bool`, bare and as `Owner.Name`; each type a file declares, by path,
	## as whether it is `[].{ ... }`; each type name as `path|Name`; and the
	## type and field names.
	Gathered : {
		keywords : Dict(Str, List(Str)),
		unions : Dict(Str, TypeText.Union),
		bool_aliases : Set(Str),
		type_kinds : Dict(Str, List(Bool)),
		type_names : Set(Str),
		names : List(Facts.Named),
	}

	## A parsed file: its path, its tree and its text cut at each newline.
	FileRead : { path : Str, nodes : List(Tree.Node), source_lines : List(Str) }

	## No facts yet, to add files to.
	none : Facts.Gathered
	none = {
		keywords: Dict.empty(),
		unions: Dict.empty(),
		bool_aliases: Set.empty(),
		type_kinds: Dict.empty(),
		type_names: Set.empty(),
		names: [],
	}

	## The facts of `earlier` files, then those of a `later` one: where both
	## name a type's union, the later's stands.
	joined : Facts.Gathered, Facts.Gathered -> Facts.Gathered
	joined = |earlier, later| {
		keywords: later.keywords.to_list().fold(earlier.keywords, |into, (path, words)| into.insert(path, (into.get(path) ?? []).concat(words))),
		unions: later.unions.to_list().fold(earlier.unions, |into, (name, union)| into.insert(name, union)),
		bool_aliases: earlier.bool_aliases.union(later.bool_aliases),
		type_kinds: later.type_kinds.to_list().fold(earlier.type_kinds, |into, (path, kinds)| into.insert(path, (into.get(path) ?? []).concat(kinds))),
		type_names: earlier.type_names.union(later.type_names),
		names: earlier.names.concat(later.names),
	}

	## A file's first facts: the keywords it declares in a comment line
	## `# keywords: for, on`, lowercased.
	keywords_of : Facts.FileRead -> Facts.Gathered
	keywords_of = |file| {
		var $words = []
		for source_line in file.source_lines {
			trimmed = Text.trim(source_line)
			if trimmed.starts_with("# keywords: ") {
				for word in trimmed.drop_prefix("# keywords: ").split_on(",") {
					if Text.trim(word) != "" {
						$words = $words.append(Text.lower(Text.trim(word)))
					}
				}
			}
		}
		keywords = if $words.is_empty() {
			Dict.empty()
		} else {
			Dict.empty().insert(file.path, $words)
		}
		{ ..Facts.none, keywords }
	}

	## The facts with what the type declaration at `at` declares: its tag
	## union, its kind, its name, and its name as an alias of `Bool` when it
	## is one.
	with_type_declaration : Facts.FileRead, U64, Facts.Gathered -> Facts.Gathered
	with_type_declaration = |file, at, facts| {
		nodes = file.nodes
		node = Tree.node_at(nodes, at)
		declared = match node.kids.first() {
			Ok(name_at) => Tree.attribute(Tree.node_at(nodes, name_at), "name")
			Err(_) => ""
		}
		if declared == "" {
			return facts
		}
		built_on = match node.kids.get(1) {
			Ok(built_at) => Ok(Tree.node_at(nodes, built_at))
			Err(_) => Err(NotBuilt)
		}
		is_union = match built_on {
			Ok(built) => built.kind == "ty-tag-union"
			Err(_) => Bool.False
		}
		unions = if is_union {
			facts.unions.insert(declared, TypeText.union_shape(nodes, Tree.kid_at(node, 2)))
		} else {
			facts.unions
		}
		has_no_tags = (unions.get(declared) ?? { names: [], has_payload: Bool.False }).names.is_empty()
		is_holder = is_union and has_no_tags and Tree.node_at(nodes, Tree.last_kid(node)).kind == "associated"
		named = {
			path: file.path,
			line: node.line,
			name: declared,
			words: Text.name_words(declared),
			is_field: Bool.False,
			doc: doc_above(file.source_lines, node.line),
			is_letter_of_set: Bool.False,
			gives: "",
			is_function: Bool.False,
			first: "",
		}
		is_bool_alias = match built_on {
			Ok(built) => built.kind == "ty" and Tree.attribute(built, "name") == "Bool"
			Err(_) => Bool.False
		}
		{
			..facts,
			unions,
			type_kinds: facts.type_kinds.insert(file.path, (facts.type_kinds.get(file.path) ?? []).append(is_holder)),
			type_names: facts.type_names.insert("${file.path}|${declared}"),
			names: facts.names.append(named),
			bool_aliases: if is_bool_alias {
				facts.bool_aliases.union(Set.from_list(bool_alias_names(nodes, node, declared)))
			} else {
				facts.bool_aliases
			},
		}
	}

	## The facts with the record field at `at`, declared at line
	## `declaration_line`, as a name `container` and `singular_list` read.
	with_field : Facts.FileRead, U64, U64, Facts.Gathered -> Facts.Gathered
	with_field = |file, at, declaration_line, facts| {
		nodes = file.nodes
		node = Tree.node_at(nodes, at)
		field = Tree.attribute(node, "name")
		siblings = Set.from_list(Tree.node_at(nodes, node.parent).kids.map(|kid| Tree.attribute(Tree.node_at(nodes, kid), "name")))
		field_type_at = Tree.first_kid(node)
		field_type = Tree.node_at(nodes, field_type_at)
		is_function = field_type.kind == "ty-fn"
		gives = if is_function {
			Tree.written_type(nodes, Tree.last_kid(field_type))
		} else {
			Tree.written_type(nodes, field_type_at)
		}
		first = if is_function and field_type.kids.len() > 1 {
			Tree.written_type(nodes, Tree.first_kid(field_type))
		} else {
			""
		}
		named = {
			path: file.path,
			line: node.line,
			name: field,
			words: Text.name_words(field),
			is_field: Bool.True,
			doc: doc_above(file.source_lines, declaration_line),
			is_letter_of_set: ShortNames.is_letter_of_set(field, siblings),
			gives,
			is_function,
			first,
		}
		{ ..facts, names: facts.names.append(named) }
	}
}

## An alias of `Bool`'s names: bare, and for a module type's member
## `Owner.Name`, as the checker writes it.
bool_alias_names : List(Tree.Node), Tree.Node, Str -> List(Str)
bool_alias_names = |nodes, node, declared| {
	if node.parent == 0 {
		return [declared]
	}
	parent = Tree.node_at(nodes, node.parent)
	if parent.kind != "associated" or parent.parent == 0 {
		return [declared]
	}
	owner = match Tree.node_at(nodes, parent.parent).kids.first() {
		Ok(owner_at) => Tree.attribute(Tree.node_at(nodes, owner_at), "name")
		Err(_) => ""
	}
	if owner == "" {
		[declared]
	} else {
		[declared, "${owner}.${declared}"]
	}
}

## The comment lines directly above line `line`, nearest first, trimmed,
## lowercased and joined: a declaration's doc.
doc_above : List(Str), U64 -> Str
doc_above = |source_lines, line| {
	var $parts = []
	var $above = line
	while $above > 1 and Text.trim(Text.line_at(source_lines, $above - 1)).starts_with("#") {
		$parts = $parts.append(Text.lower(Text.trim(Text.line_at(source_lines, $above - 1))))
		$above = $above - 1
	}
	Str.join_with($parts, " ")
}
