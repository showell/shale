## The rule, `bare_float`: an `F64` in a top-level or member annotation or in
## a type declaration, however deep in the type (a field, a list's element, a
## tag's payload), each `F64` a finding, unless the table excuses it or a
## helper only tests use holds it. A local's annotation is the body's
## business and isn't judged.
import Exceptions
import lib.Finding
import TestHelpers
import lib.Tree

Floats := [].{

	## A file Roc's parser read: its path and its tree.
	ParsedFile : { path : Str, syntax_nodes : List(Tree.Node) }

	## What a file's floats give: the findings, the table's names that excused
	## one, and how many sit in helpers only tests use.
	Judged : {
		findings : List(Finding),
		covered : List(Str),
		in_helpers : U64,
	}

	## Each `F64` of the file judged.
	judge_file : Floats.ParsedFile, Exceptions -> Floats.Judged
	judge_file = |file, exceptions| {
		var $float_places = []
		$at : U64
		var $at = 0
		for node in file.syntax_nodes {
			$at = $at + 1
			if node.kind == "ty" and Tree.last_part(Tree.attribute(node, "name")) == "F64" {
				$float_places = $float_places.append($at)
			}
		}
		if $float_places.is_empty() {
			return { findings: [], covered: [], in_helpers: 0 }
		}
		reading = { file, file_module: Tree.module_name(file.path) }
		helpers = TestHelpers.helper_names(file.syntax_nodes)
		var $findings = []
		var $covered = []
		$in_helpers : U64
		var $in_helpers = 0
		for place in $float_places {
			match declaration_of(reading, place) {
				Err(Local) => {}
				Ok(found) =>
					if !(found.in_type) and helpers.contains(found.name) {
						$in_helpers = $in_helpers + 1
					} else {
						match Exceptions.cover(exceptions, found.key) {
							Ok(name) => {
								$covered = $covered.append(name)
							}
							Err(Uncovered) => {
								$findings = $findings.append(bare_float(file, found))
							}
						}
					}
			}
		}
		{
			findings: $findings,
			covered: $covered,
			in_helpers: $in_helpers,
		}
	}
}

## A file being judged, with the module its name says it holds.
Reading : { file : Floats.ParsedFile, file_module : Str }

## Where a float is declared: `key`, its dotted name (`Scene.Spot.lift`);
## `declaration`, the place of the annotation or type declaration holding
## it, and `name`, that declaration's own; whether a type's block holds it;
## and the float's own line.
Declared : {
	key : Str,
	declaration : U64,
	name : Str,
	in_type : Bool,
	line : U64,
}

## Where the `F64` at `place` is declared, or `Local` when it belongs to a
## local's annotation or a body. A type's own module isn't repeated in its
## key, so `Unit :: F64` in Unit.roc is `Unit`.
declaration_of : Reading, U64 -> Try(Declared, [Local])
declaration_of = |reading, place| {
	syntax_nodes = reading.file.syntax_nodes
	float_node = Tree.node_at(syntax_nodes, place)
	var $names = []
	$declaration : U64
	var $declaration = 0
	var $in_type = Bool.False
	var $above = float_node.parent
	while $above != 0 {
		node = Tree.node_at(syntax_nodes, $above)
		kind = node.kind
		if kind.starts_with("e-") or kind.starts_with("p-") or kind == "s-decl" {
			return Err(Local)
		}
		if kind == "anno-record-field" or kind == "s-type-anno" {
			$names = [Tree.attribute(node, "name")].concat($names)
		}
		if kind == "s-type-decl" {
			type_name = Tree.attribute(Tree.node_at(syntax_nodes, Tree.kid_at(node, 1)), "name")
			if type_name != reading.file_module {
				$names = [type_name].concat($names)
			}
			$in_type = Bool.True
		}
		if (kind == "s-type-anno" or kind == "s-type-decl") and $declaration == 0 {
			$declaration = $above
		}
		$above = node.parent
	}
	Ok({
		key: Str.join_with([reading.file_module].concat($names), "."),
		declaration: $declaration,
		name: Tree.attribute(Tree.node_at(syntax_nodes, $declaration), "name"),
		in_type: $in_type,
		line: float_node.line,
	})
}

## A `bare_float` finding: the float's key and the declaration's whole type,
## what a reader will find at the line.
bare_float : Floats.ParsedFile, Declared -> Finding
bare_float = |file, found| {
	declaration = Tree.node_at(file.syntax_nodes, found.declaration)
	type_place = if declaration.kind == "s-type-anno" {
		Tree.kid_at(declaration, 1)
	} else {
		Tree.kid_at(declaration, 2)
	}
	type_text = Tree.written_type(file.syntax_nodes, type_place)
	{
		path: file.path,
		line: found.line,
		check: "bare_float",
		what: "`${found.key}` has a bare `F64`: ${type_text}",
	}
}
