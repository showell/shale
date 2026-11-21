## A nest of calls: the outermost call of calls nested in each other's
## arguments, like `f(g(h(x)))`, with its depth and the names of its
## outermost and deepest calls. `Nest.nests_in` finds a file's nests from
## Roc's tree of it; the launcher's header says the rule.
import lib.Tree

Nest := {
	line : U64,
	column : U64,
	depth : U64,
	outermost : Str,
	deepest : Str,
}.{

	## Whether a record, a list or a tuple argument starts the depth again
	## (`Strict`, the default) or passes its parts' depth through (`Loose`).
	Strictness : [Strict, Loose]

	## What to report: nests at least `min` deep, judged as `strictness` says.
	Rule : { min : I64, strictness : Nest.Strictness }

	## The file's nests at least the rule's `min` deep, in source order, each
	## named by its outermost call.
	nests_in : List(Tree.Node), Nest.Rule -> List(Nest)
	nests_in = |syntax_nodes, rule| {
		reading = {
			syntax_nodes,
			depths: call_depths(syntax_nodes, rule.strictness),
			strictness: rule.strictness,
		}
		var $nests = []
		var $place = 1
		while $place <= syntax_nodes.len() {
			is_deep_enough = depth_at(reading, $place).to_i64_wrap() >= rule.min
			is_nest = is_call(Tree.node_at(syntax_nodes, $place)) and is_outermost(reading, $place)
			if is_deep_enough and is_nest {
				$nests = $nests.append(nest_at(reading, $place))
			}
			$place = $place + 1
		}
		$nests
	}
}

## A tree read for nests: each node's call depth beside it, by place.
Reading : {
	syntax_nodes : List(Tree.Node),
	depths : List(U64),
	strictness : Nest.Strictness,
}

nest_at : Reading, U64 -> Nest
nest_at = |reading, place| {
	node = Tree.node_at(reading.syntax_nodes, place)
	{
		line: node.line,
		column: node.col,
		depth: depth_at(reading, place),
		outermost: call_name(reading.syntax_nodes, place),
		deepest: call_name(reading.syntax_nodes, deepest_call(reading, place)),
	}
}

# ── Depth

## The node kinds that pass their parts' depth through: what reads left to
## right, a wrapper, an operator, an access, a string's interpolation. `unary`
## is `!x` or `-x`, `receiver` holds the value whose method is called, and
## `mapper` is the right side of `x |> f`.
pass_through_kinds : List(Str)
pass_through_kinds = [
	"e-arrow-call",
	"e-question-suffix",
	"e-nominal-apply",
	"mapper",
	"e-binop",
	"unary",
	"e-field-access",
	"e-tuple-access",
	"receiver",
	"e-string",
]

## The kinds that pass their parts' depth through only when the rule is
## `Loose`.
loose_pass_through_kinds : List(Str)
loose_pass_through_kinds = ["e-record", "e-list", "e-tuple", "field"]

passes_depth_through : Str, Nest.Strictness -> Bool
passes_depth_through = |kind, strictness|
	match strictness {
		Strict => pass_through_kinds.contains(kind)
		Loose => pass_through_kinds.contains(kind) or loose_pass_through_kinds.contains(kind)
	}

is_call : Tree.Node -> Bool
is_call = |node| node.kind == "e-apply" or node.kind == "e-method-call"

## Every node's call depth, by place. A node's depth is its kids', so the
## places are taken from the last, whose kids come after them all.
call_depths : List(Tree.Node), Nest.Strictness -> List(U64)
call_depths = |syntax_nodes, strictness| {
	var $depths = List.repeat(0, syntax_nodes.len())
	var $place = syntax_nodes.len()
	while $place > 0 {
		depth = depth_from_kids({ syntax_nodes, depths: $depths, strictness }, $place)
		$depths = $depths.set(
			$place - 1,
			depth,
		) ?? crash "find_deep_calls: a place past the last node"
		$place = $place - 1
	}
	$depths
}

## A node's call depth from its kids' depths, which `reading` already has.
## A call is one more than its deepest argument; a method call is at least
## as deep as its receiver, which reads left to right; a tag's payload, as
## in `Ok(...)`, adds no level; what passes depth through, and a one-item
## tuple, which is a pair of parentheses, is as deep as its deepest part;
## anything else (a lambda, a record) starts again at zero.
depth_from_kids : Reading, U64 -> U64
depth_from_kids = |reading, place| {
	node = Tree.node_at(reading.syntax_nodes, place)
	if node.kind == "e-apply" {
		deepest_argument = deepest_of(reading, node.kids.drop_first(1))
		function = Tree.node_at(reading.syntax_nodes, Tree.first_kid(node))
		if function.kind == "e-tag" {
			deepest_argument
		} else {
			1 + deepest_argument
		}
	} else if node.kind == "e-method-call" {
		method_call_depth(reading, node)
	} else if passes_depth_through(node.kind, reading.strictness) or is_parenthesized(node) {
		deepest_of(reading, node.kids)
	} else {
		0
	}
}

method_call_depth : Reading, Tree.Node -> U64
method_call_depth = |reading, node| {
	var $receiver_depth = 0
	var $deepest_argument = 0
	for kid in node.kids {
		part = Tree.node_at(reading.syntax_nodes, kid)
		if part.kind == "receiver" {
			$receiver_depth = depth_at(reading, Tree.first_kid(part))
		}
		if part.kind == "args" {
			$deepest_argument = U64.max($deepest_argument, deepest_of(reading, part.kids))
		}
	}
	U64.max(1 + $deepest_argument, $receiver_depth)
}

## A tuple of one item, which is a pair of parentheses.
is_parenthesized : Tree.Node -> Bool
is_parenthesized = |node| node.kind == "e-tuple" and node.kids.len() == 1

deepest_of : Reading, List(U64) -> U64
deepest_of = |reading, places|
	places.fold(0, |deepest, place| U64.max(deepest, depth_at(reading, place)))

depth_at : Reading, U64 -> U64
depth_at = |reading, place|
	match reading.depths.get(place - 1) {
		Ok(depth) => depth
		Err(_) => crash "find_deep_calls: no depth for node ${place.to_str()}"
	}

# ── The nest around a call

## Whether a call nested under this node still belongs to the nest of the
## call above it.
continues_nest : Tree.Node, Nest.Strictness -> Bool
continues_nest = |node, strictness| {
	is_part = node.kind == "args" or node.kind == "e-tuple"
	is_call(node) or is_part or passes_depth_through(node.kind, strictness)
}

## Whether the call at `place` is the outermost of its nest: the first call
## above it, through nodes that continue the nest, is not as deep.
is_outermost : Reading, U64 -> Bool
is_outermost = |reading, place| {
	depth = depth_at(reading, place)
	var $above = Tree.node_at(reading.syntax_nodes, place).parent
	while $above > 0 {
		node = Tree.node_at(reading.syntax_nodes, $above)
		if is_call(node) {
			return depth_at(reading, $above) < depth
		}
		if !(continues_nest(node, reading.strictness)) {
			return Bool.True
		}
		$above = node.parent
	}
	Bool.True
}

## Whether the call at `place` is inside the nest of the call at `top`:
## every node between them continues the nest.
is_in_nest : Reading, { top : U64, place : U64 } -> Bool
is_in_nest = |reading, { top, place }| {
	var $above = Tree.node_at(reading.syntax_nodes, place).parent
	while $above > 0 and $above != top {
		node = Tree.node_at(reading.syntax_nodes, $above)
		if !(continues_nest(node, reading.strictness)) {
			return Bool.False
		}
		$above = node.parent
	}
	$above == top
}

## The deepest call of the nest under the call at `top`: the first call of
## depth 1 inside it, in source order, or `top` itself when it has none.
## The nodes inside it are those after it that are deeper in the tree.
deepest_call : Reading, U64 -> U64
deepest_call = |reading, top| {
	top_depth = Tree.node_at(reading.syntax_nodes, top).depth
	is_inside = |place| Tree.node_at(reading.syntax_nodes, place).depth > top_depth
	var $place = top + 1
	while $place <= reading.syntax_nodes.len() and is_inside($place) {
		node = Tree.node_at(reading.syntax_nodes, $place)
		is_deepest = is_call(node) and depth_at(reading, $place) == 1
		if is_deepest and is_in_nest(reading, { top, place: $place }) {
			return $place
		}
		$place = $place + 1
	}
	top
}

## A call's name: an apply's function as written (`List.concat`, else its
## name, else its kind), or a method call's method (`.concat`).
call_name : List(Tree.Node), U64 -> Str
call_name = |syntax_nodes, place| {
	node = Tree.node_at(syntax_nodes, place)
	if node.kind == "e-method-call" {
		match Tree.find_attribute(node, "method") {
			Ok(method) => method
			Err(_) => crash "find_deep_calls: a method call with no method"
		}
	} else {
		function = Tree.node_at(syntax_nodes, Tree.first_kid(node))
		match Tree.find_attribute(function, "raw") {
			Ok(raw) => raw
			Err(_) => Tree.find_attribute(function, "name") ?? function.kind
		}
	}
}
