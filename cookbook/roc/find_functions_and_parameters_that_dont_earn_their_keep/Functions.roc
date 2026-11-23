## The first pass of find_functions_and_parameters_that_dont_earn_their_keep: what each node's ancestors say, and
## each function defined at a file's top level or as a module type's member,
## with what the checks read of its body, its `##` doc and its parameters.
import lib.Forest
import lib.Text

Functions := [].{

	## What each node's ancestors say, by place counted from 1: `top`, every
	## ancestor is a file, its statements, a module type or its block of
	## members; `owner`, the name of the nearest module type around it, or
	## `""`; `enclosing`, the name of the outermost declaration around it, or
	## `?`; `in_expect`, it is inside an `expect`.
	Ancestry : {
		top : List(Bool),
		owner : List(Str),
		enclosing : List(Str),
		in_expect : List(Bool),
	}

	## A function: its key is `owner|name`, the owner its module type's name
	## or `@path` for a file's own; `line` and `end_line` its definition's;
	## whether its body is small enough for `single` to call it a wrapper, and
	## its size in words; the `##` lines above it; whether it takes `{}`
	## alone; and its parameter patterns' places.
	Function : {
		key : Str,
		path : Str,
		owner : Str,
		name : Str,
		line : U64,
		end_line : U64,
		is_wrapper : Bool,
		size : Str,
		doc_lines : U64,
		takes_empty_record_alone : Bool,
		params : List(U64),
	}

	## The functions found, each key's place among them, and the keys of the
	## module types' members by name, a key listed again when its name is
	## declared again.
	Defined : {
		functions : List(Functions.Function),
		by_key : Dict(Str, U64),
		member_keys : Dict(Str, List(Str)),
	}

	## The most ways and nodes in a body `single` still calls a wrapper.
	Limits : { ways : I64, nodes : I64 }

	## Each node's ancestry, worked out from its parent's, which comes
	## before it in the forest.
	ancestry : Forest -> Functions.Ancestry
	ancestry = |forest| {
		var $top = []
		var $owner = []
		var $enclosing = []
		var $in_expect = []
		$at : U64
		var $at = 1
		while $at <= Forest.count(forest) {
			above = Forest.parent(forest, $at)
			if above == 0 {
				$top = $top.append(Bool.True)
				$owner = $owner.append("")
				$enclosing = $enclosing.append("?")
				$in_expect = $in_expect.append(Bool.False)
			} else {
				kind = Forest.kind(forest, above)
				is_top = Functions.flag_at($top, above) and top_level_kinds.contains(kind)
				$top = $top.append(is_top)
				owner = if kind == "s-type-decl" {
					type_decl_name(forest, above)
				} else {
					Functions.text_at($owner, above)
				}
				$owner = $owner.append(owner)
				enclosing = enclosing_name(forest, above, Functions.text_at($enclosing, above))
				$enclosing = $enclosing.append(enclosing)
				in_expect = Functions.flag_at($in_expect, above) or kind == "s-expect"
				$in_expect = $in_expect.append(in_expect)
			}
			$at = $at + 1
		}
		{ top: $top, owner: $owner, enclosing: $enclosing, in_expect: $in_expect }
	}

	## The functions declared in the checked files, `lines_of` each file's
	## lines. A key declared twice keeps its last declaration.
	read : Forest, Functions.Ancestry, { checked : Set(Str), lines_of : Dict(Str, List(Str)), limits : Functions.Limits } -> Functions.Defined
	read = |forest, ancestors, given| {
		var $functions = []
		var $by_key = Dict.empty()
		var $member_keys = Dict.empty()
		$at : U64
		var $at = 1
		while $at <= Forest.count(forest) {
			name_at = Forest.kid(forest, $at, 1)
			lambda = Forest.kid(forest, $at, 2)
			path = Forest.path(forest, $at)
			is_declared = Forest.kind(forest, $at) == "s-decl" and lambda != 0
			is_function = is_declared and Forest.kind(forest, name_at) == "p-ident" and Forest.kind(forest, lambda) == "e-lambda"
			if is_function and Functions.flag_at(ancestors.top, $at) and given.checked.contains(path) {
				owned_by = Functions.text_at(ancestors.owner, $at)
				owner = if owned_by != "" {
					owned_by
				} else {
					"@${path}"
				}
				node = Forest.node(forest, $at)
				name = Forest.raw(forest, name_at)
				key = "${owner}|${name}"
				body = body_measure(forest, lambda, given.limits)
				function = {
					key,
					path,
					owner,
					name,
					line: node.line,
					end_line: node.end_line,
					is_wrapper: body.is_wrapper,
					size: body.size,
					doc_lines: doc_comment_lines(given.lines_of.get(path) ?? [], name, node.line),
					takes_empty_record_alone: takes_empty_record_alone(forest, $at, name),
					params: lambda_params(forest, lambda),
				}
				match $by_key.get(key) {
					Ok(index) => {
						$functions = $functions.set(index, function) ?? crash "find_functions_and_parameters_that_dont_earn_their_keep: a key past the functions"
					}
					Err(_) => {
						$by_key = $by_key.insert(key, $functions.len())
						$functions = $functions.append(function)
					}
				}
				if !(owner.starts_with("@")) {
					keys = $member_keys.get(name) ?? []
					$member_keys = $member_keys.insert(name, keys.append(key))
				}
			}
			$at = $at + 1
		}
		{ functions: $functions, by_key: $by_key, member_keys: $member_keys }
	}

	## The function at a place among them.
	at : Functions.Defined, U64 -> Functions.Function
	at = |defined, index| defined.functions.get(index) ?? crash "find_functions_and_parameters_that_dont_earn_their_keep: no function ${index.to_str()}"

	## A node's flag, by its place counted from 1.
	flag_at : List(Bool), U64 -> Bool
	flag_at = |flags, place| flags.get(place - 1) ?? Bool.False

	## A node's text, by its place counted from 1.
	text_at : List(Str), U64 -> Str
	text_at = |texts, place| texts.get(place - 1) ?? ""
}

## The kinds of node that can hold a top-level function: a file, its
## statements, a module type's declaration, and the block of its members.
top_level_kinds : Set(Str)
top_level_kinds = Set.from_list(["file", "statements", "s-type-decl", "associated"])

## The name in the header of the module type declared at `at`.
type_decl_name : Forest, U64 -> Str
type_decl_name = |forest, at| {
	header = Forest.kid(forest, at, 1)
	if header == 0 {
		""
	} else {
		Forest.name(forest, header)
	}
}

## The outermost declaration's name below the node at `above`, whose own is
## `was`: a declaration of a plain name is the first to give one.
enclosing_name : Forest, U64, Str -> Str
enclosing_name = |forest, above, was| {
	pattern = Forest.kid(forest, above, 1)
	is_named = pattern != 0 and Forest.kind(forest, pattern) == "p-ident"
	if was == "?" and Forest.kind(forest, above) == "s-decl" and is_named {
		Forest.raw(forest, pattern)
	} else {
		was
	}
}

## How many `##` lines stand just above the function defined at `line`, or
## above its annotation (`name : ...`, on one line or several) if it has one.
doc_comment_lines : List(Str), Str, U64 -> U64
doc_comment_lines = |source_lines, name, line| {
	trimmed_at = |number| Text.trim(source_lines.get(number - 1) ?? "")
	$top : U64
	var $top = line - 1
	$up : U64
	var $up = $top
	while $up >= 1 and $up + 12 > line and trimmed_at($up) != "" and !(trimmed_at($up).starts_with("#")) {
		if trimmed_at($up).starts_with("${name} :") {
			$top = $up - 1
		}
		$up = $up - 1
	}
	$count : U64
	var $count = 0
	while $top >= 1 and trimmed_at($top).starts_with("##") {
		$count = $count + 1
		$top = $top - 1
	}
	$count
}

## Whether the body of the lambda at `lambda` is small enough for `single` to
## call it a wrapper, and its size in words for the finding. A wrapper's body
## is one statement: a block of one, or an expression. Its size counts
## syntax-tree nodes, so the layout of its lines changes nothing. It has one
## way through, and one more for each `if` and each `match` arm past the
## first, so only a two-way choice reads as a wrapper.
body_measure : Forest, U64, Functions.Limits -> { is_wrapper : Bool, size : Str }
body_measure = |forest, lambda, limits| {
	outer = Forest.kids(forest, lambda).last() ?? crash "find_functions_and_parameters_that_dont_earn_their_keep: a lambda with no body"
	var $body = outer
	if Forest.kind(forest, outer) == "e-block" {
		var $statements = []
		for kid in Forest.kids(forest, outer) {
			if Forest.kind(forest, kid) == "statements" {
				$statements = Forest.kids(forest, kid)
			}
		}
		if $statements.len() != 1 {
			return { is_wrapper: Bool.False, size: "${$statements.len().to_str()} statements" }
		}
		$body = $statements.first() ?? outer
	}
	last = Forest.last_descendant(forest, $body)
	$ways : I64
	var $ways = 1
	$place : U64
	var $place = $body
	while $place <= last {
		kind = Forest.kind(forest, $place)
		if kind == "e-if-then-else" {
			$ways = $ways + 1
		}
		if kind == "branches" {
			$ways = $ways + Forest.kids(forest, $place).len().to_i64_wrap() - 1
		}
		$place = $place + 1
	}
	count = (last - $body + 1).to_i64_wrap()
	is_small = count <= limits.nodes
	if $ways > 1 {
		{ is_wrapper: is_small and $ways <= limits.ways, size: "a decision of ${$ways.to_str()} ways, ${count.to_str()} nodes" }
	} else {
		{ is_wrapper: is_small, size: "one expression of ${count.to_str()} nodes" }
	}
}

## Whether the subtree at `at` holds a node of `kind`.
holds_kind : Forest, U64, Str -> Bool
holds_kind = |forest, at, kind| {
	last = Forest.last_descendant(forest, at)
	var $place = at
	while $place <= last {
		if Forest.kind(forest, $place) == kind {
			return Bool.True
		}
		$place = $place + 1
	}
	Bool.False
}

## The function type of the annotation just before the declaration at
## `decl`, when that annotation is for `name`, or 0.
annotation_function_type : Forest, U64, Str -> U64
annotation_function_type = |forest, decl, name| {
	above = Forest.parent(forest, decl)
	if above == 0 {
		return 0
	}
	siblings = Forest.kids(forest, above)
	before = match siblings.find_first_index(|kid| kid == decl) {
		Ok(index) if index > 0 => siblings.get(index - 1) ?? 0
		_ => 0
	}
	if before == 0 or Forest.kind(forest, before) != "s-type-anno" or Forest.name(forest, before) != name {
		0
	} else {
		signature = Forest.kid(forest, before, 1)
		if signature != 0 and Forest.kind(forest, signature) == "ty-fn" {
			signature
		} else {
			0
		}
	}
}

## Whether the function declared at `decl` takes `{}` alone, by its lambda's
## one pattern or its annotation's one argument, and could be a constant: an
## annotated return type holding a type variable rules that out.
takes_empty_record_alone : Forest, U64, Str -> Bool
takes_empty_record_alone = |forest, decl, name| {
	lambda = Forest.kid(forest, decl, 2)
	is_empty_record = |at, kind| at != 0 and Forest.kind(forest, at) == kind and Forest.kids(forest, at).is_empty()
	by_pattern = Forest.kids(forest, lambda).any(
		|kid| Forest.kind(forest, kid) == "args" and Forest.kids(forest, kid).len() == 1 and is_empty_record(Forest.kid(forest, kid, 1), "p-record"),
	)
	fn_type = annotation_function_type(forest, decl, name)
	if fn_type == 0 {
		by_pattern
	} else {
		parts = Forest.kids(forest, fn_type)
		by_type = parts.len() == 2 and is_empty_record(parts.first() ?? 0, "ty-record")
		result = parts.last() ?? 0
		(by_pattern or by_type) and (result == 0 or !(holds_kind(forest, result, "ty-var")))
	}
}

## The parameter patterns of the lambda at `lambda`, by position.
lambda_params : Forest, U64 -> List(U64)
lambda_params = |forest, lambda|
	match Forest.kids(forest, lambda).find_first(|kid| Forest.kind(forest, kid) == "args") {
		Ok(args) => Forest.kids(forest, args)
		Err(_) => []
	}
