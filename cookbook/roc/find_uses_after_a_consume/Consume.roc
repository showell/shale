## The rule of find_uses_after_a_consume: within one function, a use of a
## value after a write has consumed it. Roc writes into a list, a string or
## a dict in place only when nothing else refers to it, and every copy
## roc-apps' copy shapes could make (tools/copy_shapes/CopyShapes.roc) is
## the same mistake: something still reads the old value after a consuming
## builtin has taken it. Each function, and each lambda, is walked in
## evaluation order, its values tracked by path (`xs`, `st.items`), with
## five checks:
##
## - `after_consume`: a path read after a consuming call took it, or taken
##   twice.
## - `whole_record`: a record read whole after a consuming call took one of
##   its fields, or inside that call's own arguments.
## - `closure`: a closure called after a write took a value it holds, or a
##   closure that consumes a value its maker holds.
## - `held`: a value got from a container (`get`, `first`, `last`) written
##   while the container is still used after.
## - `restored_refcounted`: with the types, `?? xs` handing back a list whose
##   entries hold a reference count, which copies in the dev and boxy
##   builds.
##
## The `??` restitution is not a use: `xs.set(i, v) ?? xs` hands the list
## back on the error path, and RC1 sees through it.
import lib.Finding
import lib.Forest
import lib.RocTypes

Consume := [].{

	## The findings over every function and lambda of the files, each once,
	## in Finding's order.
	judge : Forest, RocTypes.Typing -> List(Finding)
	judge = |forest, typing| {
		var $findings = []
		$at : U64
		var $at = 1
		while $at <= Forest.count(forest) {
			if Forest.kind(forest, $at) == "e-lambda" {
				$findings = $findings.concat(judge_function(forest, typing, $at))
			}
			$at = $at + 1
		}
		once_each($findings).sort_with(Finding.order)
	}

	## The checks, in the order the summary counts them.
	checks : List(Str)
	checks = ["after_consume", "whole_record", "closure", "held", "restored_refcounted"]

	## Whether a builtin consumes its first argument (or its receiver):
	## `owner` is `List`, `Str`, `Dict` or `Set` for a module call, `""` for
	## a method, which may be any of them.
	consumes : Str, Str -> Bool
	consumes = |owner, name|
		match owner {
			"List" => list_consumers.contains(name)
			"Str" => str_consumers.contains(name)
			"Dict" => dict_consumers.contains(name)
			"Set" => dict_consumers.contains(name)
			"" => list_consumers.contains(name) or str_consumers.contains(name) or dict_consumers.contains(name) or unique_consumers.contains(name)
			_ => Bool.False
		}
}

# ── The table of consuming builtins

## The List builtins that write in place when the list is unique and copy
## otherwise: RC1's uniqueness-checked ops (`list_set`, `list_replace`,
## `list_swap`, `list_concat`, `list_prepend`, `list_reserve`,
## `list_drop_at`, `list_reverse`, the appends), and `update`, `map` and
## the sorts, which ask the same through `list_map_prepare_reuse` and
## `list_owned_unique`. `list_sublist`, behind `sublist` and the drops and
## takes, is left out: on a shared list it makes a slice of the same
## memory, no copy (copy-shapes' `append-length-after`).
list_consumers : List(Str)
list_consumers = [
	"set",
	"replace",
	"swap",
	"concat",
	"prepend",
	"reserve",
	"drop_at",
	"reverse",
	"append",
	"update",
	"map",
	"sort_with",
	"sort_asc",
	"sort_desc",
]

## The Str builtins RC1 checks the same way: `str_concat`, the trims, the
## case maps and `str_reserve`.
str_consumers : List(Str)
str_consumers = ["concat", "trim", "trim_start", "trim_end", "with_ascii_lowercased", "with_ascii_uppercased", "reserve"]

## Dict's and Set's writes, Roc code over lists that reach `list_set` and
## its kin.
dict_consumers : List(Str)
dict_consumers = ["insert", "remove", "update"]

## roc-apps' opt-in marker's writes (`Unique.roc`): each takes its value.
unique_consumers : List(Str)
unique_consumers = ["unwrap", "take_from"]

## The builtins whose answer the container still holds.
getters : List(Str)
getters = ["get", "first", "last"]

# ── One function

## What a function is walked with: the files, the types, the file it is
## in, the names it tracks (its own and those it captures), the captured
## ones apart, and its last node.
Ctx : {
	forest : Forest,
	typing : RocTypes.Typing,
	file : Str,
	tracked : Set(Str),
	captured : Set(Str),
	last : U64,
}

## A path a consuming call took, the call's name and its line.
Taken : { path : Str, op : Str, line : U64 }

## What the walk knows at a point: what has been taken, the closures and
## what each holds, each value's container, the field paths whose write is
## under way, the paths whose consuming call's arguments are being walked,
## and the findings so far.
State : {
	taken : List(Taken),
	closures : Dict(Str, List(Str)),
	held : Dict(Str, Str),
	writing : List(Str),
	pending : List(Str),
	findings : List(Finding),
}

## How a use reaches a path: directly, or through a closure's call.
Via : [Direct, Closure(Str)]

judge_function : Forest, RocTypes.Typing, U64 -> List(Finding)
judge_function = |forest, typing, lambda| {
	last = Forest.last_descendant(forest, lambda)
	bound = bound_in(forest, lambda, last)
	outer = outer_names(forest, lambda)
	captured = Set.from_list(used_in(forest, lambda, last).keep_if(|name| outer.contains(name) and !(bound.contains(name))))
	ctx = {
		forest,
		typing,
		file: Forest.path(forest, lambda),
		tracked: bound.union(captured),
		captured,
		last,
	}
	empty = {
		taken: [],
		closures: Dict.empty(),
		held: Dict.empty(),
		writing: [],
		pending: [],
		findings: [],
	}
	match Forest.kids(forest, lambda).last() {
		Ok(body) => walk(ctx, empty, body).findings
		Err(_) => []
	}
}

## The names bound in the nodes `from` to `to`: each pattern's names and
## each `var`'s.
bound_in : Forest, U64, U64 -> Set(Str)
bound_in = |forest, from, to| {
	var $names = Set.empty()
	var $at = from
	while $at <= to {
		kind = Forest.kind(forest, $at)
		if kind == "p-ident" {
			$names = $names.insert(Forest.raw(forest, $at))
		} else if kind == "s-var" {
			$names = $names.insert(Forest.name(forest, $at))
		}
		$at = $at + 1
	}
	$names
}

## The names the identifiers in the nodes `from` to `to` use, each once.
used_in : Forest, U64, U64 -> List(Str)
used_in = |forest, from, to| {
	var $names = Set.empty()
	var $at = from
	while $at <= to {
		if Forest.kind(forest, $at) == "e-ident" {
			$names = $names.insert(Forest.raw(forest, $at))
		}
		$at = $at + 1
	}
	$names.to_list()
}

## The names the lambdas around a lambda bind, which it may capture.
outer_names : Forest, U64 -> Set(Str)
outer_names = |forest, lambda| {
	var $names = Set.empty()
	var $at = Forest.parent(forest, lambda)
	while $at > 0 {
		if Forest.kind(forest, $at) == "e-lambda" {
			$names = $names.union(bound_in(forest, $at, Forest.last_descendant(forest, $at)))
		}
		$at = Forest.parent(forest, $at)
	}
	$names
}

# ── The walk, in evaluation order

walk : Ctx, State, U64 -> State
walk = |ctx, state, at| {
	forest = ctx.forest
	match Forest.kind(forest, at) {
		"e-ident" => use_ident(ctx, state, at)
		"e-field-access" =>
			match path_of(ctx, at) {
				Ok(path) => use_path(ctx, state, { path, at, via: Direct })
				Err(_) => walk_kids(ctx, state, Forest.kids(forest, at))
			}
		"e-method-call" => method_call(ctx, state, at)
		"e-apply" => apply(ctx, state, at)
		"e-binop" =>
			if Forest.attribute(forest, at, "op") == "??" {
				fallback(ctx, state, at)
			} else {
				walk_kids(ctx, state, Forest.kids(forest, at))
			}
		"e-lambda" => lambda_made(ctx, state, at).state
		"s-decl" => declared(ctx, state, at)
		"s-var" => declared(ctx, state, at)
		"s-for" => for_loop(ctx, state, at)
		"s-while" => while_loop(ctx, state, at)
		"e-if-then-else" => branched(ctx, state, at)
		"e-if-without-else" => branched(ctx, state, at)
		"e-match" => matched(ctx, state, at)
		"s-type-anno" => state
		"s-type-decl" => state
		"s-import" => state
		_ => walk_kids(ctx, state, Forest.kids(forest, at))
	}
}

walk_kids : Ctx, State, List(U64) -> State
walk_kids = |ctx, state, kids| kids.fold(state, |so_far, kid| walk(ctx, so_far, kid))

## A name read: the value itself when the function tracks it, and every
## value a closure of that name holds.
use_ident : Ctx, State, U64 -> State
use_ident = |ctx, state, at| {
	name = Forest.raw(ctx.forest, at)
	direct = if ctx.tracked.contains(name) {
		use_path(ctx, state, { path: name, at, via: Direct })
	} else {
		state
	}
	match direct.closures.get(name) {
		Ok(holds) => holds.fold(direct, |so_far, path| use_path(ctx, so_far, { path, at, via: Closure(name) }))
		Err(_) => direct
	}
}

## A path read at a node: a finding for each taken path it reaches, and for
## each field whose write is under way when it is that field's record.
use_path : Ctx, State, { path : Str, at : U64, via : Via } -> State
use_path = |ctx, state, { path, at, via }| {
	line = Forest.line_number(ctx.forest, at)
	finding = |check, what| { path: ctx.file, line, check, what }
	var $found = []
	for taken in state.taken {
		if path == taken.path or path.starts_with("${taken.path}.") {
			$found = match via {
				Direct => {
					what_took = if (path == taken.path) "it" else "`${taken.path}`"
					$found.append(finding("after_consume", "`${path}` is read after `${taken.op}` took ${what_took} at line ${taken.line.to_str()}, so the write may copy"))
				}
				Closure(name) => $found.append(finding("closure", "`${name}` holds `${path}`, which `${taken.op}` took at line ${taken.line.to_str()}: calling it after the write may copy"))
			}
		} else if taken.path.starts_with("${path}.") {
			$found = $found.append(finding("whole_record", "`${path}` is read whole after `${taken.op}` took its field `${taken.path}` at line ${taken.line.to_str()}, so the write may copy (in the dev and boxy builds)"))
		}
	}
	for field in state.writing {
		if field.starts_with("${path}.") {
			$found = $found.append(finding("whole_record", "`${path}` is read whole inside the write of its field `${field}`: bind what it gives on the line before, or the write may copy (in the dev and boxy builds)"))
		}
	}
	{ ..state, findings: state.findings.concat($found) }
}

## The path an expression names, `xs` or `st.items`, when it is one of the
## function's values.
path_of : Ctx, U64 -> Try(Str, [NotAPath])
path_of = |ctx, at| {
	forest = ctx.forest
	match Forest.kind(forest, at) {
		"e-ident" =>
			if ctx.tracked.contains(Forest.raw(forest, at)) {
				Ok(Forest.raw(forest, at))
			} else {
				Err(NotAPath)
			}
		"e-field-access" => {
			receiver = Forest.kid(forest, at, 1)
			segment = Forest.kid(forest, at, 2)
			field = Forest.attribute(forest, segment, "field")
			if receiver == 0 or field == "" {
				Err(NotAPath)
			} else {
				path_of(ctx, Forest.kid(forest, receiver, 1)).map_ok(|path| "${path}.${field}")
			}
		}
		_ => Err(NotAPath)
	}
}

## A consuming call's place, the path it takes, its name and its other
## arguments.
Call : { at : U64, target : Str, op : Str, args : List(U64) }

## The consuming call a node is, when it takes one of the function's paths.
consuming_call : Ctx, U64 -> Try(Call, [NotConsuming])
consuming_call = |ctx, at| {
	forest = ctx.forest
	kind = Forest.kind(forest, at)
	if kind == "e-method-call" {
		op = Forest.attribute(forest, at, "method").drop_prefix(".")
		receiver = Forest.kid(forest, at, 1)
		args = Forest.kid(forest, at, 2)
		if Consume.consumes("", op) and receiver > 0 {
			match path_of(ctx, Forest.kid(forest, receiver, 1)) {
				Ok(target) => Ok({ at, target, op, args: if (args > 0) Forest.kids(forest, args) else [] })
				Err(_) => Err(NotConsuming)
			}
		} else {
			Err(NotConsuming)
		}
	} else if kind == "e-apply" {
		callee = Forest.kid(forest, at, 1)
		parts = Forest.raw(forest, callee).split_on(".")
		match parts {
			[owner, op] if Consume.consumes(owner, op) =>
				match path_of(ctx, Forest.kid(forest, at, 2)) {
					Ok(target) => Ok({ at, target, op, args: Forest.kids(forest, at).drop_first(2) })
					Err(_) => Err(NotConsuming)
				}
			_ => Err(NotConsuming)
		}
	} else {
		Err(NotConsuming)
	}
}

method_call : Ctx, State, U64 -> State
method_call = |ctx, state, at|
	match consuming_call(ctx, at) {
		Ok(call) => consumed(ctx, state, call)
		Err(_) => walk_kids(ctx, state, Forest.kids(ctx.forest, at))
	}

apply : Ctx, State, U64 -> State
apply = |ctx, state, at|
	match consuming_call(ctx, at) {
		Ok(call) => consumed(ctx, state, call)
		Err(_) => walk_kids(ctx, state, Forest.kids(ctx.forest, at))
	}

## A consuming call: its target read (a second take is a finding), its
## container judged, its arguments walked while the write is under way, and
## then the target taken.
consumed : Ctx, State, Call -> State
consumed = |ctx, state, call| {
	forest = ctx.forest
	line = Forest.line_number(forest, call.at)
	finding = |check, what| { path: ctx.file, line, check, what }
	read = use_path(ctx, state, { path: call.target, at: call.at, via: Direct })
	held_found = match read.held.get(call.target) {
		Ok(container) if !(is_taken(read, container)) and (read.pending.contains(container) or is_used_after(ctx, container, call.at)) =>
			[finding("held", "`${call.target}` came from `${container}`, which still holds it, so `${call.op}` may copy it: take it out first (`remove`, `replace`)")]
		_ => []
	}
	captured_found =
		if ctx.captured.contains(root_of(call.target)) {
			[finding("closure", "the closure's `${call.op}` takes `${call.target}`, which its maker still holds, so the write may copy")]
		} else {
			[]
		}
	writing =
		if call.target.contains(".") {
			read.writing.append(call.target)
		} else {
			read.writing
		}
	during = {
		..read,
		findings: read.findings.concat(held_found).concat(captured_found),
		writing,
		pending: read.pending.append(call.target),
	}
	after = walk_kids(ctx, during, call.args)
	{
		..after,
		writing: read.writing,
		pending: read.pending,
		taken: after.taken.append({ path: call.target, op: call.op, line }),
	}
}

is_taken : State, Str -> Bool
is_taken = |state, path| state.taken.any(|taken| path == taken.path or path.starts_with("${taken.path}."))

## Whether the function reads a path's root anywhere after a node.
is_used_after : Ctx, Str, U64 -> Bool
is_used_after = |ctx, path, at| used_in(ctx.forest, at + 1, ctx.last).contains(root_of(path))

## A path's first name: `st` for `st.items`.
root_of : Str -> Str
root_of = |path| path.split_on(".").first() ?? path

## A `??`: the lookup walked, then the fallback, unless it hands the
## consuming call's own target back (the restitution, no use); with the
## types, a list of counted entries handed back is a finding.
fallback : Ctx, State, U64 -> State
fallback = |ctx, state, at| {
	forest = ctx.forest
	lookup = Forest.kid(forest, at, 1)
	back = Forest.kid(forest, at, 2)
	after = walk(ctx, state, lookup)
	match (consuming_call(ctx, lookup), path_of(ctx, back)) {
		(Ok(call), Ok(path)) if call.target == path => restored(ctx, after, { path, back })
		_ => walk(ctx, after, back)
	}
}

## `?? xs` handing back the list a write took: no use, but with the types,
## a finding when the list's entries hold a reference count.
restored : Ctx, State, { path : Str, back : U64 } -> State
restored = |ctx, state, { path, back }|
	match RocTypes.type_at(ctx.typing, ctx.file, Forest.node(ctx.forest, back)) {
		Ok(written) if has_counted_entries(written) => {
			line = Forest.line_number(ctx.forest, back)
			what = "`?? ${path}` hands back a `${written}`, whose entries hold a reference count: it copies on every write in the dev and boxy builds; write `?? crash \"...\"`"
			{ ..state, findings: state.findings.append({ path: ctx.file, line, check: "restored_refcounted", what }) }
		}
		_ => state
	}

## Whether a type is a list whose entries hold a reference count: anything
## but a number or a Bool.
has_counted_entries : Str -> Bool
has_counted_entries = |written|
	if written.starts_with("List(") and written.ends_with(")") {
		entry = written.drop_prefix("List(").drop_suffix(")")
		!(plain_types.contains(entry))
	} else {
		Bool.False
	}

plain_types : List(Str)
plain_types = ["U8", "U16", "U32", "U64", "U128", "I8", "I16", "I32", "I64", "I128", "F32", "F64", "Dec", "Bool"]

## A lambda made here: each value it captures is read now, and the lambda's
## holdings are given back for a binding to keep.
lambda_made : Ctx, State, U64 -> { state : State, holds : List(Str) }
lambda_made = |ctx, state, at| {
	forest = ctx.forest
	last = Forest.last_descendant(forest, at)
	bound = bound_in(forest, at, last)
	holds = used_in(forest, at, last).keep_if(|name| ctx.tracked.contains(name) and !(bound.contains(name)))
	read = holds.fold(state, |so_far, path| use_path(ctx, so_far, { path, at, via: Direct }))
	{ state: read, holds }
}

## A `name = expr` or a `var $name = expr`: the expression walked, then the
## names bound afresh, a closure's holdings kept, and a got value's
## container.
declared : Ctx, State, U64 -> State
declared = |ctx, state, at| {
	forest = ctx.forest
	kind = Forest.kind(forest, at)
	(names, expr) =
		if kind == "s-var" {
			([Forest.name(forest, at)], Forest.kid(forest, at, 1))
		} else {
			pattern = Forest.kid(forest, at, 1)
			(pattern_names(forest, pattern), Forest.kid(forest, at, 2))
		}
	is_lambda = Forest.kind_or_none(forest, expr) == "e-lambda"
	made = if is_lambda {
		lambda_made(ctx, state, expr)
	} else {
		{ state: walk(ctx, state, expr), holds: [] }
	}
	fresh = unbound(made.state, names)
	match names {
		[name] =>
			if is_lambda {
				{ ..fresh, closures: fresh.closures.insert(name, made.holds) }
			} else {
				match container_of(ctx, expr) {
					Ok(container) => { ..fresh, held: fresh.held.insert(name, container) }
					Err(_) => fresh
				}
			}
		_ => fresh
	}
}

## The names a pattern binds.
pattern_names : Forest, U64 -> List(Str)
pattern_names = |forest, pattern| bound_in(forest, pattern, Forest.last_descendant(forest, pattern)).to_list()

## The state with names bound afresh: nothing they held is taken.
unbound : State, List(Str) -> State
unbound = |state, names| {
	is_theirs = |path| names.any(|name| path == name or path.starts_with("${name}."))
	{
		..state,
		taken: state.taken.drop_if(|taken| is_theirs(taken.path)),
		held: names.fold(state.held, |held, name| held.remove(name)),
		closures: names.fold(state.closures, |closures, name| closures.remove(name)),
	}
}

## The container a value is got from: `c.get(k)`, `List.get(c, i)`,
## `c.first()`, a `??` around one.
container_of : Ctx, U64 -> Try(Str, [NoContainer])
container_of = |ctx, at| {
	forest = ctx.forest
	kind = Forest.kind(forest, at)
	if kind == "e-binop" and Forest.attribute(forest, at, "op") == "??" {
		container_of(ctx, Forest.kid(forest, at, 1))
	} else if kind == "e-method-call" and getters.contains(Forest.attribute(forest, at, "method").drop_prefix(".")) {
		receiver = Forest.kid(forest, at, 1)
		path_of(ctx, Forest.kid(forest, receiver, 1)).map_err(|_| NoContainer)
	} else if kind == "e-apply" {
		match Forest.raw(forest, Forest.kid(forest, at, 1)).split_on(".") {
			[owner, name] if (owner == "List" or owner == "Dict") and getters.contains(name) =>
				path_of(ctx, Forest.kid(forest, at, 2)).map_err(|_| NoContainer)
			_ => Err(NoContainer)
		}
	} else {
		Err(NoContainer)
	}
}

# ── Branches and loops

## An `if`: its condition, then each branch from there, merged.
branched : Ctx, State, U64 -> State
branched = |ctx, state, at|
	match Forest.kids(ctx.forest, at) {
		[condition, then_branch, else_branch] => {
			after = walk(ctx, state, condition)
			merged(after, [walk(ctx, after, then_branch), walk(ctx, after, else_branch)])
		}
		[condition, then_branch] => {
			after = walk(ctx, state, condition)
			merged(after, [after, walk(ctx, after, then_branch)])
		}
		kids => walk_kids(ctx, state, kids)
	}

## A `match`: its subject, then each branch from there, its pattern's names
## bound afresh (and held by the subject's container when it is a lookup),
## merged.
matched : Ctx, State, U64 -> State
matched = |ctx, state, at| {
	forest = ctx.forest
	subject = Forest.kid(forest, at, 1)
	after = walk(ctx, state, subject)
	container = container_of(ctx, subject)
	branches = Forest.kid(forest, at, 2)
	arms = if (branches > 0) Forest.kids(forest, branches) else []
	ends = arms.map(
		|arm| {
			parts = Forest.kids(forest, arm)
			names = match parts.first() {
				Ok(pattern) => pattern_names(forest, pattern)
				Err(_) => []
			}
			fresh = unbound(after, names)
			bound = match container {
				Ok(holder) => { ..fresh, held: names.fold(fresh.held, |held, name| held.insert(name, holder)) }
				Err(_) => fresh
			}
			walk_kids(ctx, bound, parts.drop_first(1))
		},
	)
	merged(after, ends)
}

## A `for`: the list walked, then the body twice, so a value taken in one
## pass and read in the next is found.
for_loop : Ctx, State, U64 -> State
for_loop = |ctx, state, at|
	match Forest.kids(ctx.forest, at) {
		[pattern, items, body] => {
			names = pattern_names(ctx.forest, pattern)
			before = walk(ctx, state, items)
			first = walk(ctx, unbound(before, names), body)
			second = walk(ctx, unbound(first, names), body)
			merged(before, [second])
		}
		kids => walk_kids(ctx, state, kids)
	}

## A `while`: the condition and the body, twice.
while_loop : Ctx, State, U64 -> State
while_loop = |ctx, state, at| {
	kids = Forest.kids(ctx.forest, at)
	once = walk_kids(ctx, state, kids)
	merged(state, [walk_kids(ctx, once, kids)])
}

## The states at the ends of several branches from one start, as one: what
## any branch took is taken, and each branch's own findings are kept (a
## branch only adds to the start's).
merged : State, List(State) -> State
merged = |start, ends|
	ends.fold(
		start,
		|so_far, end| {
			..so_far,
			taken: end.taken.fold(so_far.taken, |taken, one| if (taken.any(|t| t.path == one.path)) taken else taken.append(one)),
			closures: end.closures.fold(so_far.closures, |closures, name, holds| closures.insert(name, holds)),
			held: end.held.fold(so_far.held, |held, name, holder| held.insert(name, holder)),
			findings: so_far.findings.concat(end.findings.drop_first(start.findings.len())),
		},
	)

## Each finding once, the first of each line.
once_each : List(Finding) -> List(Finding)
once_each = |findings|
	findings.fold(
		[],
		|kept, finding|
			if kept.any(|other| Finding.line_of(other) == Finding.line_of(finding)) {
				kept
			} else {
				kept.append(finding)
			},
	)
