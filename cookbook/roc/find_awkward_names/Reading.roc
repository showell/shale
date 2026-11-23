## What find_awkward_names reads of each file: a record for every top-level and module-member
## annotation and definition, leaving out the names the file's header
## dictates, and along the way the facts (Facts) the checks judged over the
## whole tree need.
import lib.Headers
import lib.Text
import lib.Tree
import Facts
import TypeText

Reading := [].{

	## A lambda's argument: its name (`""` for a pattern that is no plain
	## name), its line, and the checker's type of it (`""` for none).
	Parameter : { name : Str, line : U64, typed : Str }

	## A definition, from its annotation or its `name = ...`. An annotation
	## has `whole`, its type, and `result`, what it gives; a function's also
	## `first`, `arity` and `argument_types`, and an effect's the type names it
	## mentions and each argument's shape. A definition whose annotation is
	## just above it is paired, and the annotation then has its definition's
	## checker's type, callback and parameters.
	Definition : {
		path : Str,
		line : U64,
		name : Str,
		owner : Str,
		is_annotation : Bool,
		is_function_type : Bool,
		has_callback : Bool,
		is_paired : Bool,
		has_definition : Bool,
		has_doc : Bool,
		whole : Str,
		result : Str,
		first : Str,
		arity : I64,
		argument_types : List(Str),
		mentioned_types : Set(Str),
		arguments : List(TypeText.ArgumentShape),
		is_lambda : Bool,
		parameters : List(Reading.Parameter),
		recurses : Bool,
		typed : [Typed(Str), Untyped],
	}

	## A parsed file: its path, its tree and its text cut at each newline.
	FileRead : Facts.FileRead

	## The suffixes `worker_name` reports only on a definition that calls
	## itself, as they are ordinary words too.
	recursion_suffixes : List(Str)
	recursion_suffixes = ["step", "walk", "go"]

	## The file's definitions in file order, and the facts it adds.
	## `pattern_types` is the checker's type of each plain-name pattern, by
	## its node's place.
	file_definitions : Reading.FileRead, Dict(U64, Str) -> { definitions : List(Reading.Definition), facts : Facts.Gathered }
	file_definitions = |file, pattern_types| {
		nodes = file.nodes
		dictated = Set.from_list(Headers.header_names(nodes).dictated)
		is_kept = |definition| !(dictated.contains(definition.name) and definition.owner == "")
		var $facts = Facts.keywords_of(file)
		var $outer = []
		var $declaration_lines = []
		var $annotation_places = Dict.empty()
		var $definitions = []
		$at : U64
		var $at = 0
		for node in nodes {
			$at = $at + 1
			at = $at
			kind = node.kind
			is_outer = node.parent == 0 or (($outer.get(node.parent - 1) ?? Bool.False) and outer_kinds.contains(Tree.node_at(nodes, node.parent).kind))
			$outer = $outer.append(is_outer)
			is_declaration = is_outer and (kind == "s-type-anno" or kind == "s-type-decl")
			$declaration_lines = $declaration_lines.append(
				if is_declaration {
					node.line
				} else {
					0
				},
			)
			if is_outer and kind == "s-type-decl" {
				$facts = Facts.with_type_declaration(file, at, $facts)
			}
			if kind == "anno-record-field" {
				declaration_line = field_declaration_line(nodes, $declaration_lines, at)
				if declaration_line != 0 and Tree.attribute(node, "name") != "" {
					$facts = Facts.with_field(file, at, declaration_line, $facts)
				}
			}
			if is_outer and kind == "s-type-anno" {
				match annotation_definition(file, at) {
					Ok(definition) if is_kept(definition) => {
						$annotation_places = $annotation_places.insert(at, $definitions.len())
						$definitions = $definitions.append(definition)
					}
					_ => {}
				}
			}
			if is_outer and kind == "s-decl" {
				match value_definition(file, at, pattern_types) {
					Ok(definition) if is_kept(definition) => {
						above_place = $annotation_places.get(TypeText.previous_sibling(nodes, at))
						match above_place {
							Ok(place) => {
								above = definition_at($definitions, place)
								if above.name == definition.name {
									paired = with_definition(above, definition)
									$definitions = replaced($definitions, place, paired).append({ ..definition, is_paired: Bool.True })
								} else {
									$definitions = $definitions.append(definition)
								}
							}
							Err(_) => {
								$definitions = $definitions.append(definition)
							}
						}
					}
					_ => {}
				}
			}
		}
		{ definitions: $definitions, facts: $facts }
	}
}

## The kinds of node a definition may sit in and still be top-level or a
## module type's member, which `associated` holds.
outer_kinds : List(Str)
outer_kinds = ["file", "statements", "s-type-decl", "associated"]

definition_at : List(Reading.Definition), U64 -> Reading.Definition
definition_at = |definitions, place|
	match definitions.get(place) {
		Ok(found) => found
		Err(_) => crash "Reading: an annotation's place is past the definitions"
	}

## The definitions with the one at `place` replaced.
replaced : List(Reading.Definition), U64, Reading.Definition -> List(Reading.Definition)
replaced = |definitions, place, definition|
	match definitions.set(place, definition) {
		Ok(changed) => changed
		Err(_) => crash "Reading: an annotation's place is past the definitions"
	}

## The annotation `above` given what its definition says.
with_definition : Reading.Definition, Reading.Definition -> Reading.Definition
with_definition = |above, definition| {
	typed = match definition.typed {
		Typed(_) => definition.typed
		Untyped => above.typed
	}
	{
		..above,
		has_definition: Bool.True,
		typed,
		has_callback: above.has_callback or definition.has_callback,
		parameters: definition.parameters,
	}
}

## The line of the declaration that the record field at `at` belongs to, as
## `declaration_lines` holds it so far, or 0 when an expression, a pattern
## or a definition lies between them: the field is then a local's.
field_declaration_line : List(Tree.Node), List(U64), U64 -> U64
field_declaration_line = |nodes, declaration_lines, at| {
	var $up = Tree.node_at(nodes, at).parent
	while $up != 0 {
		node = Tree.node_at(nodes, $up)
		if node.kind.starts_with("e-") or node.kind.starts_with("p-") or node.kind == "s-decl" {
			return 0
		}
		if node.kind == "s-type-anno" or node.kind == "s-type-decl" {
			return declaration_lines.get($up - 1) ?? 0
		}
		$up = node.parent
	}
	0
}

## The name of the type declaration around node `at`, or `""` at the top
## level.
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

## Whether a `##` doc stands directly above line `line`.
has_doc_above : List(Str), U64 -> Bool
has_doc_above = |source_lines, line| line > 1 and Text.trim(Text.line_at(source_lines, line - 1)).starts_with("##")

## A record with nothing read yet, for the file's node at `at`.
blank_definition : Reading.FileRead, U64, Str -> Reading.Definition
blank_definition = |file, at, name| {
	node = Tree.node_at(file.nodes, at)
	{
		path: file.path,
		line: node.line,
		name,
		owner: owner_of(file.nodes, at),
		is_annotation: Bool.False,
		is_function_type: Bool.False,
		has_callback: Bool.False,
		is_paired: Bool.False,
		has_definition: Bool.False,
		has_doc: has_doc_above(file.source_lines, node.line),
		whole: "",
		result: "",
		first: "",
		arity: -1,
		argument_types: [],
		mentioned_types: Set.empty(),
		arguments: [],
		is_lambda: Bool.False,
		parameters: [],
		recurses: Bool.False,
		typed: Untyped,
	}
}

## The annotation at `at`, unless it is only a `where` clause.
annotation_definition : Reading.FileRead, U64 -> Try(Reading.Definition, [OnlyWhere])
annotation_definition = |file, at| {
	nodes = file.nodes
	node = Tree.node_at(nodes, at)
	type_at = match node.kids.first() {
		Ok(found) => found
		Err(_) => return Err(OnlyWhere)
	}
	type_node = Tree.node_at(nodes, type_at)
	if type_node.kind == "where" {
		return Err(OnlyWhere)
	}
	name = Tree.attribute(node, "name")
	whole = Tree.written_type(nodes, type_at)
	blank = { ..blank_definition(file, at, name), is_annotation: Bool.True, whole, result: whole }
	if type_node.kind != "ty-fn" {
		return Ok(blank)
	}
	is_effect = name.ends_with("!")
	arguments = type_node.kids.drop_last(1)
	Ok({
		..blank,
		is_function_type: Bool.True,
		arity: arguments.len().to_i64_wrap(),
		result: Tree.written_type(nodes, Tree.last_kid(type_node)),
		first: if type_node.kids.len() > 1 {
			Tree.written_type(nodes, Tree.first_kid(type_node))
		} else {
			""
		},
		argument_types: arguments.map(|argument| Tree.written_type(nodes, argument)),
		has_callback: arguments.any(|argument| TypeText.span_text(file.source_lines, Tree.node_at(nodes, argument)).contains("=>")),
		mentioned_types: if is_effect {
			TypeText.mentioned_type_names(nodes, type_at, Set.empty())
		} else {
			Set.empty()
		},
		arguments: if is_effect {
			arguments.map(|argument| TypeText.argument_shape(nodes, argument))
		} else {
			[]
		},
	})
}

## The definition `name = ...` at `at`, unless its pattern is no plain name.
value_definition : Reading.FileRead, U64, Dict(U64, Str) -> Try(Reading.Definition, [NoPlainName])
value_definition = |file, at, pattern_types| {
	nodes = file.nodes
	node = Tree.node_at(nodes, at)
	pattern_at = match node.kids.first() {
		Ok(found) => found
		Err(_) => return Err(NoPlainName)
	}
	pattern = Tree.node_at(nodes, pattern_at)
	if pattern.kind != "p-ident" {
		return Err(NoPlainName)
	}
	name = Tree.attribute(pattern, "raw")
	lambda = match node.kids.get(1) {
		Ok(value_at) if Tree.node_at(nodes, value_at).kind == "e-lambda" => Ok(Tree.node_at(nodes, value_at))
		_ => Err(NoLambda)
	}
	arguments = match lambda {
		Ok(lambda_node) =>
			lambda_node.kids
				.keep_if(|part| Tree.node_at(nodes, part).kind == "args")
				.map(|part| Tree.node_at(nodes, part).kids)
				.join()
		Err(_) => []
	}
	parameter_of = |argument| {
		argument_node = Tree.node_at(nodes, argument)
		parameter_name = if argument_node.kind == "p-ident" {
			Tree.attribute(argument_node, "raw")
		} else {
			""
		}
		typed = if parameter_name != "" {
			pattern_types.get(argument) ?? ""
		} else {
			""
		}
		{ name: parameter_name, line: argument_node.line, typed }
	}
	words = name.drop_suffix("!").split_on("_")
	recurses = words.len() > 1 and Reading.recursion_suffixes.contains(words.last() ?? "") and TypeText.calls_itself(nodes, at, name)
	Ok({
		..blank_definition(file, at, name),
		is_lambda: match lambda {
			Ok(_) => Bool.True
			Err(_) => Bool.False
		},
		parameters: arguments.map(parameter_of),
		has_callback: arguments.any(|argument| Tree.attribute(Tree.node_at(nodes, argument), "raw").ends_with("!")),
		recurses,
		typed: match pattern_types.get(pattern_at) {
			Ok(found) => Typed(found)
			Err(_) => Untyped
		},
	})
}
