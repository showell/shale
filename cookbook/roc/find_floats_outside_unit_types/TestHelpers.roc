## The helpers only tests use, whose floats aren't judged. A helper is a
## definition at the top level of a type module, outside the type's block,
## which the compiler lets no other module reach, so the file's own tree
## shows everything that uses it. It is one when every use is in an `expect`
## or in another such helper, and there is one. An app's or a `module [...]`
## file's top-level names can be reached from outside, so they have none. A
## local that shares a helper's name counts as a use, which can only report
## more.
import lib.Tree

TestHelpers := [].{

	## The names of the file's helpers only tests use.
	helper_names : List(Tree.Node) -> Set(Str)
	helper_names = |nodes| {
		if !(is_type_module(nodes)) {
			return Set.empty()
		}
		definitions = top_level_definitions(nodes)
		uses = places_of_uses(nodes, definitions)
		var $helpers = Set.from_list(
			uses
				.to_list()
				.keep_if(|(_, places)| !(places.is_empty()))
				.map(|(name, _)| name),
		)
		# A helper falls when anything outside tests uses it, which can bring
		# down the helpers only it used.
		var $changed = Bool.True
		while $changed {
			$changed = Bool.False
			for (name, places) in uses.to_list() {
				if $helpers.contains(name) {
					var $falls = Bool.False
					for place in places.to_list() {
						if place != in_a_test and !($helpers.contains(place)) {
							$falls = Bool.True
						}
					}
					if $falls {
						$helpers = $helpers.remove(name)
						$changed = Bool.True
					}
				}
			}
		}
		$helpers
	}
}

## The place of a node inside a test.
in_a_test : Str
in_a_test = "expect"

## The place of a node in neither a definition nor a test.
in_no_definition : Str
in_no_definition = ""

## The place of a node's parent, which comes before it.
place_of_parent : List(Str), Tree.Node -> Str
place_of_parent = |places, node|
	match places.get(node.parent - 1) {
		Ok(place) => place
		Err(_) => crash "find_floats_outside_unit_types: a node before its parent"
	}

## Whether the file is a type module, with a `type-mod` header.
is_type_module : List(Tree.Node) -> Bool
is_type_module = |nodes|
	Tree.node_at(nodes, 1).kids.any(|kid| Tree.node_at(nodes, kid).kind == "type-mod")

## The definitions at the top level of the file, outside any type's block:
## each name with its `s-decl`'s place, the last one where a name is defined
## twice.
top_level_definitions : List(Tree.Node) -> Dict(Str, U64)
top_level_definitions = |nodes| {
	var $found = Dict.empty()
	for top in Tree.node_at(nodes, 1).kids {
		statements = Tree.node_at(nodes, top)
		if statements.kind == "statements" {
			for place in statements.kids {
				statement = Tree.node_at(nodes, place)
				if statement.kind == "s-decl" {
					match statement.kids.first() {
						Ok(pattern_place) => {
							pattern = Tree.node_at(nodes, pattern_place)
							if pattern.kind == "p-ident" {
								$found = $found.insert(Tree.attribute(pattern, "raw"), place)
							}
						}
						Err(_) => {}
					}
				}
			}
		}
	}
	$found
}

## Where each definition is used: its name with the places of its uses, a
## place being the definition the use sits in, `in_a_test` in a test, and
## `""` anywhere else. A definition's use of itself is no place. A parent
## comes before its kids, so one pass gives each node its place.
places_of_uses : List(Tree.Node), Dict(Str, U64) -> Dict(Str, Set(Str))
places_of_uses = |nodes, definitions| {
	var $defined_at = Dict.empty()
	for (name, place) in definitions.to_list() {
		$defined_at = $defined_at.insert(place, name)
	}
	var $uses = Dict.empty()
	for (name, _) in definitions.to_list() {
		$uses = $uses.insert(name, Set.empty())
	}
	var $places = []
	$at : U64
	var $at = 0
	for node in nodes {
		$at = $at + 1
		above = if node.parent == 0 {
			""
		} else {
			place_of_parent($places, node)
		}
		in_test = if node.kind == "s-expect" {
			in_a_test
		} else {
			above
		}
		here = if in_test == "" {
			match $defined_at.get($at) {
				Ok(name) => name
				Err(_) => in_no_definition
			}
		} else {
			in_test
		}
		$places = $places.append(here)
		used = name_used(node)
		if used != "" and used != here {
			match $uses.get(used) {
				Ok(places) => {
					$uses = $uses.insert(used, places.insert(here))
				}
				Err(_) => {}
			}
		}
	}
	$uses
}

## The name a node uses, or `""`: an identifier, or a field in a record's
## short form, `{ a, b }`, which uses `a` and `b`.
name_used : Tree.Node -> Str
name_used = |node|
	if node.kind == "e-ident" {
		Tree.attribute(node, "raw")
	} else if node.kind == "field" and node.kids.is_empty() {
		Tree.attribute(node, "field")
	} else {
		""
	}
