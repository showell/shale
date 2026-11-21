## hunt_names' `short_name` for what a file binds inside its definitions
## (cookbook/roc/declared.shale's `short_binding_findings`): each local,
## parameter, match binding, record destructuring and `var` whose name, past
## a `$` or `_` and before a `!`, is one letter. A loop's variable, a
## coordinate, a colour's channels and a triangle's corners pass, as the
## launcher's header says.
import lib.Finding
import lib.Tree

ShortNames := [].{

	## Whether a name of one letter is a coordinate (`x`, `y`, `z`, and `w`
	## beside `x` and `y`), a colour's channel (`r`, `g`, `b`, and `a` beside
	## them) or a triangle's corner (`a`, `b` and `c`, all three), by
	## `siblings`, the names beside it.
	is_letter_of_set : Str, Set(Str) -> Bool
	is_letter_of_set = |name, siblings| {
		has_all = |names| names.all(|sibling| siblings.contains(sibling))
		if ["x", "y", "z"].contains(name) or (name == "w" and has_all(["x", "y"])) {
			Bool.True
		} else if ["a", "b", "c"].contains(name) and has_all(["a", "b", "c"]) {
			Bool.True
		} else {
			["r", "g", "b", "a"].contains(name) and has_all(["r", "g", "b"])
		}
	}

	## The one-letter bindings of the file at `path`, in the tree's order.
	findings : Str, List(Tree.Node) -> List(Finding)
	findings = |path, nodes| {
		tested = while_tested(nodes)
		vars = Set.from_list(nodes.keep_if(|node| node.kind == "s-var").map(|node| Tree.attribute(node, "name")))
		var $found = []
		$at : U64
		var $at = 0
		for node in nodes {
			$at = $at + 1
			name = name_bound_by(nodes, node)
			letters = if name == "" {
				""
			} else {
				without_marks(name)
			}
			if letters.count_utf8_bytes() == 1 {
				binding = { at: $at, name, letters, tested, vars }
				match judged_binding(nodes, binding) {
					Reported(what) => {
						why = "${what} `${name}` is one letter: name what it holds"
						$found = $found.append({ path, line: node.line, check: "short_name", what: why })
					}
					Passed => {}
				}
			}
		}
		$found
	}
}

## A binding of one letter: its node's place, its name, the name's letters,
## and the names a `while` tests and the `var`s the file declares.
Binding : { at : U64, name : Str, letters : Str, tested : Set(Str), vars : Set(Str) }

## What a one-letter binding is called when it is reported, or that it
## passes.
judged_binding : List(Tree.Node), Binding -> [Reported(Str), Passed]
judged_binding = |nodes, binding| {
	node = Tree.node_at(nodes, binding.at)
	is_var = node.kind == "s-var"
	holder = if node.kind == "p-ident" and Tree.node_at(nodes, node.parent).kind == "field" {
		Tree.node_at(nodes, node.parent).parent
	} else {
		node.parent
	}
	binder = binder_of(nodes, node.parent)
	binder_kind = if is_var {
		"s-var"
	} else {
		Tree.node_at(nodes, binder).kind
	}
	siblings = if is_var {
		Set.empty()
	} else {
		bound_siblings(nodes, holder)
	}
	is_record = Tree.node_at(nodes, holder).kind == "p-record"
	is_colour = ["r", "g", "b"].all(|channel| siblings.contains(channel))
	is_corner_outside_record = ["a", "b", "c"].contains(binding.letters) and !(is_record) and !(is_colour)
	is_reassigned_var = binding.name.starts_with("$") and binding.vars.contains(binding.name)
	skip = (binder_kind == "s-decl" and (is_outer_definition(nodes, binder) or is_reassigned_var))
		or (binder_kind == "s-var" and binding.tested.contains(binding.name))
		or binder_kind == "s-for"
		or (binder_kind == "e-lambda" and is_loop_lambda(nodes, binder))
		or (ShortNames.is_letter_of_set(binding.letters, siblings) and !(is_corner_outside_record))
	if skip {
		Passed
	} else if binder_kind == "s-decl" {
		Reported("the local")
	} else if binder_kind == "e-lambda" {
		Reported("the parameter")
	} else if binder_kind == "branch" {
		Reported("the match binding")
	} else if binder_kind == "s-var" {
		Reported("the var")
	} else {
		Reported("the binding")
	}
}

## The names a `while` tests, in any of the file's loops.
while_tested : List(Tree.Node) -> Set(Str)
while_tested = |nodes| {
	var $tested = Set.empty()
	for node in nodes {
		if node.kind == "s-while" {
			match node.kids.first() {
				Ok(condition) => {
					last = subtree_end(nodes, condition)
					var $place = condition
					while $place <= last {
						inner = Tree.node_at(nodes, $place)
						if inner.kind == "e-ident" {
							$tested = $tested.insert(Tree.attribute(inner, "raw"))
						}
						$place = $place + 1
					}
				}
				Err(_) => {}
			}
		}
	}
	$tested
}

## A name past its leading `$`s and `_`s and before a last `!`.
without_marks : Str -> Str
without_marks = |name| {
	bytes = name.to_utf8()
	start = bytes.find_first_index(|byte| byte != '$' and byte != '_') ?? bytes.len()
	Str.from_utf8_lossy(bytes.drop_first(start)).drop_suffix("!")
}

## The last node of the subtree at `at`: the tree is in order, each subtree
## running on while the depth is deeper than its root's.
subtree_end : List(Tree.Node), U64 -> U64
subtree_end = |nodes, at| {
	depth = Tree.node_at(nodes, at).depth
	var $stop = at
	while $stop < nodes.len() and Tree.node_at(nodes, $stop + 1).depth > depth {
		$stop = $stop + 1
	}
	$stop
}

## The kinds of node a pattern sits in on its way up to what binds it.
pattern_holders : List(Str)
pattern_holders = ["args", "field", "p-record", "p-tuple", "p-list", "p-tag", "p-as"]

## Up through the patterns from `start` to what binds them.
binder_of : List(Tree.Node), U64 -> U64
binder_of = |nodes, start| {
	var $binder = start
	while $binder > 0 and pattern_holders.contains(Tree.node_at(nodes, $binder).kind) {
		$binder = Tree.node_at(nodes, $binder).parent
	}
	$binder
}

## The name a node binds, `""` for none: a pattern's name, a record's pun
## (`{ x }`), a list's rest (`.. as rest`) and a `var`.
bound_name : List(Tree.Node), U64 -> Str
bound_name = |nodes, at| name_bound_by(nodes, Tree.node_at(nodes, at))

## The name the node `node` binds, as `bound_name` gives it.
name_bound_by : List(Tree.Node), Tree.Node -> Str
name_bound_by = |nodes, node| {
	if node.kind == "p-ident" {
		Tree.attribute(node, "raw")
	} else if node.kind == "field" and node.kids.is_empty() and node.parent > 0 and Tree.node_at(nodes, node.parent).kind == "p-record" {
		Tree.attribute(node, "name")
	} else if node.kind == "p-list-rest" or node.kind == "s-var" {
		Tree.attribute(node, "name")
	} else {
		""
	}
}

## The names bound beside a binding in one pattern, `holder`'s: a lambda's
## parameters, a record's fields, a tuple's or a tag's parts. A record's
## field renamed (`{ y: q }`) binds its pattern, beside the record's others.
bound_siblings : List(Tree.Node), U64 -> Set(Str)
bound_siblings = |nodes, holder| {
	var $names = Set.empty()
	for kid in Tree.node_at(nodes, holder).kids {
		name = bound_name(nodes, kid)
		if name != "" {
			$names = $names.insert(name)
		}
		kid_node = Tree.node_at(nodes, kid)
		if kid_node.kind == "field" {
			for inner in kid_node.kids {
				inner_name = bound_name(nodes, inner)
				if inner_name != "" {
					$names = $names.insert(inner_name)
				}
			}
		}
	}
	$names
}

## The kinds of node a definition may sit in and still be top-level or a
## module type's member.
outer_kinds : List(Str)
outer_kinds = ["file", "statements", "s-type-decl", "associated"]

## Whether a definition at `at` is top-level or a module type's member, its
## ancestors all of the outer kinds: its name is judged as a definition's.
is_outer_definition : List(Tree.Node), U64 -> Bool
is_outer_definition = |nodes, at| {
	var $up = Tree.node_at(nodes, at).parent
	while $up > 0 {
		node = Tree.node_at(nodes, $up)
		if !(outer_kinds.contains(node.kind)) {
			return Bool.False
		}
		$up = node.parent
	}
	Bool.True
}

## The list methods whose one-line lambda is a loop's body, so its
## parameters are loop variables (cookbook/roc/declared.shale's
## LOOP_METHODS).
loop_methods : List(Str)
loop_methods = [
	"map",
	"map2",
	"map3",
	"map_with_index",
	"map_try",
	"keep_if",
	"drop_if",
	"keep_oks",
	"keep_errs",
	"walk",
	"walk_backwards",
	"walk_until",
	"walk_with_index",
	"walk_try",
	"any",
	"all",
	"count_if",
	"find_first",
	"find_last",
	"find_first_index",
	"find_last_index",
	"sort_with",
	"join_map",
	"for_each!",
	"for_each_try!",
	"walk!",
	"walk_try!",
]

## Whether the lambda at `at` is a loop's body: on one line, and the direct
## argument of a list method, as a method call or a module's
## (`List.map(xs, |x| ...)`).
is_loop_lambda : List(Tree.Node), U64 -> Bool
is_loop_lambda = |nodes, at| {
	lambda = Tree.node_at(nodes, at)
	if lambda.line != lambda.end_line {
		return Bool.False
	}
	holder = Tree.node_at(nodes, lambda.parent)
	if holder.kind == "args" and Tree.node_at(nodes, holder.parent).kind == "e-method-call" {
		method = Tree.attribute(Tree.node_at(nodes, holder.parent), "method")
		return loop_methods.contains(method.drop_prefix("."))
	}
	if holder.kind != "e-apply" or holder.kids.first() == Ok(at) {
		return Bool.False
	}
	callee = Tree.node_at(nodes, Tree.first_kid(holder))
	if callee.kind != "e-ident" {
		return Bool.False
	}
	match Tree.attribute(callee, "raw").split_on(".") {
		["List", method] => loop_methods.contains(method)
		_ => Bool.False
	}
}
