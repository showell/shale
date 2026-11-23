## The calls a file offers to turn, `Module.fn(first, ...)`, each with why it
## must stay when it must, and what a trial's `roc check` errors say of them.
import Literals
import Rewritten
import Source

Candidates := [].{

	## A call to turn, by its node: the module and function it calls, its
	## line, why it stays (`""` while it is open to rewriting), whether the
	## types prove it, the kind of type they gave when they settled nothing,
	## and its lines in the last text that turned it (0 when it was not
	## turned). A peer method call turned back is `undo`, its receiver's
	## `type` kept.
	Candidate : {
		node : U64,
		module_name : Str,
		fn : Str,
		line : U64,
		why_left : Str,
		proven : Bool,
		kind : Str,
		from_line : U64,
		to_line : U64,
		undo : Bool,
		type : Str,
	}

	## An error `roc check` reports: its title, the file it names (its base
	## name) and the line.
	CheckError : { title : Str, file : Str, line : U64 }

	## The calls `Module.fn(first, ...)` in the file, except calls of the
	## file's own module (`own_module`), in the tree's order.
	of_file : Source.File, Str -> List(Candidates.Candidate)
	of_file = |file, own_module| {
		var $out = []
		for (node, place) in file.nodes.map_with_index(|node, index| (node, index + 1)) {
			if node.kind == "e-apply" and node.kids.len() >= 2 {
				callee = Source.kid_node(file, node, 1)
				match module_call(callee.raw) {
					Ok(called) if callee.kind == "e-ident" and called.module_name != own_module => {
						why = reason_call_stays(file, place, called.module_name, called.fn)
						line = Source.line_of_offset(file, node.from)
						$out = $out.append(candidate(place, called.module_name, called.fn, line, why))
					}
					_ => {}
				}
			}
		}
		$out
	}

	## A peer method call to turn back into a module call, proven by the
	## receiver's type, which `type` keeps.
	undoing : { place : U64, module_name : Str, fn : Str, line : U64, why_left : Str, type : Str } -> Candidates.Candidate
	undoing = |call| {
		..candidate(call.place, call.module_name, call.fn, call.line, call.why_left),
		proven: Bool.True,
		undo: Bool.True,
		type: call.type,
	}

	## The open candidates as the rewrite turns them, by node.
	turning : List(Candidates.Candidate) -> Dict(U64, Rewritten.Turn)
	turning = |cands| {
		var $turning = Dict.empty()
		for cand in cands {
			if cand.why_left == "" {
				$turning = $turning.insert(cand.node, { node: cand.node, module_name: cand.module_name, fn: cand.fn, undo: cand.undo })
			}
		}
		$turning
	}

	## How many candidates are open.
	open_count : List(Candidates.Candidate) -> U64
	open_count = |cands| cands.count_if(|cand| cand.why_left == "")

	## Every open candidate left, for `why`.
	leave_open : List(Candidates.Candidate), Str -> List(Candidates.Candidate)
	leave_open = |cands, why| cands.map(
		|cand| if cand.why_left == "" {
			{ ..cand, why_left: why }
		} else {
			cand
		},
	)

	## The candidates with the lines a rewrite gave the calls it turned; any
	## other keeps the lines it had.
	with_lines : List(Candidates.Candidate), Rewritten.Written -> List(Candidates.Candidate)
	with_lines = |cands, written|
		cands.map(
			|cand|
				match written.turned_lines.get(cand.node) {
					Ok(lines) => { ..cand, from_line: lines.from_line, to_line: lines.to_line }
					Err(_) => cand
				},
		)

	## The candidates with no lines: none turned.
	without_lines : List(Candidates.Candidate) -> List(Candidates.Candidate)
	without_lines = |cands| cands.map(|cand| { ..cand, from_line: 0 })

	## One error of a trial on the file, laid on each turned call whose lines
	## hold it (one error may fall on two calls), or else on a definition's
	## value: the candidates, each hit one left with the error's title, and
	## whether any call was hit.
	blame : Source.File, Dict(U64, U64), Candidates.CheckError, List(Candidates.Candidate) -> { cands : List(Candidates.Candidate), hit : Bool }
	blame = |file, line_of_node, error, cands| {
		on_lines = |cand| cand.from_line > 0 and error.line >= cand.from_line and error.line <= cand.to_line
		if cands.any(on_lines) {
			why = "roc check: ${error.title}"
			left = cands.map(
				|cand| if on_lines(cand) and cand.why_left == "" {
					{ ..cand, why_left: why }
				} else {
					cand
				},
			)
			{ cands: left, hit: Bool.True }
		} else {
			blame_definition(file, line_of_node, error, cands)
		}
	}
}

candidate : U64, Str, Str, U64, Str -> Candidates.Candidate
candidate = |place, module_name, fn, line, why_left| {
	node: place,
	module_name,
	fn,
	line,
	why_left,
	proven: Bool.False,
	kind: "",
	from_line: 0,
	to_line: 0,
	undo: Bool.False,
	type: "",
}

## A callee `Module.fn` split, when it is one: a capital and letters and
## digits, a `.`, then a small letter, letters, digits and `_`, and an
## optional last `!`.
module_call : Str -> Try({ module_name : Str, fn : Str }, [NoModuleCall])
module_call = |raw| {
	parts = raw.split_first(".") ?? { before: "", after: "" }
	module_name = parts.before.to_utf8()
	fn = parts.after.to_utf8()
	fn_body = if (fn.last() ?? 0) == '!' {
		fn.drop_last(1)
	} else {
		fn
	}
	is_module = match module_name.first() {
		Ok(first) => is_capital(first) and module_name.all(is_alphanumeric)
		Err(_) => Bool.False
	}
	is_fn = match fn_body.first() {
		Ok(first) => first >= 'a' and first <= 'z' and fn_body.all(|byte| is_alphanumeric(byte) or byte == '_')
		Err(_) => Bool.False
	}
	if is_module and is_fn {
		Ok({ module_name: parts.before, fn: parts.after })
	} else {
		Err(NoModuleCall)
	}
}

is_capital : U8 -> Bool
is_capital = |byte| byte >= 'A' and byte <= 'Z'

is_alphanumeric : U8 -> Bool
is_alphanumeric = |byte| is_capital(byte) or (byte >= 'a' and byte <= 'z') or (byte >= '0' and byte <= '9')

## Why the call `module.fn(...)` at node `place` must stay whatever the
## types say, or `""` when it may be tried.
reason_call_stays : Source.File, U64, Str, Str -> Str
reason_call_stays = |file, place, module_name, fn| {
	node = Source.node_at(file, place)
	parent = if node.parent > 0 {
		Ok(Source.node_at(file, node.parent))
	} else {
		Err(NoParent)
	}
	is_piped = match parent {
		Ok(up) => up.kind == "e-arrow-call" and (up.kids.get(1) ?? 0) == place
		Err(_) => Bool.False
	}
	unspanned = node.kids.drop_first(1).find_first(|arg| !(Source.node_at(file, arg).has_span))
	first_place = Source.kid(node, 2)
	first = Source.node_at(file, first_place)
	if is_piped {
		"the right of a `|>`, whose first argument the pipe gives"
	} else if unspanned.is_ok() {
		kind = Source.node_at(file, unspanned ?? 0).kind
		"an argument with no span in the tree (${kind}), whose text cannot be moved"
	} else if Literals.is_constant_numeric(file, first_place) {
		written = Source.text_of(file, first)
		text = if first.kind == "e-tuple" {
			written
		} else {
			"(${written})"
		}
		"a number literal's type would default as a receiver (`${text}.${fn}()` is no longer a ${module_name})"
	} else if Literals.takes_literal_type(file, first_place) {
		text = Source.text_of(file, first)
		taken = "`${text}` takes its type from a number literal, unannotated"
		"a number literal's type would default as a receiver (${taken}, and `${text}.${fn}()` is no longer a ${module_name})"
	} else {
		""
	}
}

## Whether node `place` is below node `top`.
is_below : Source.File, U64, U64 -> Bool
is_below = |file, place, top| {
	var $at = Source.node_at(file, place).parent
	while $at > 0 {
		if $at == top {
			return Bool.True
		}
		$at = Source.node_at(file, $at).parent
	}
	Bool.False
}

## A `missing method` error on the value of a definition `name = <value>`
## means that value's type never resolved because its only use was a turned
## `name.fn()`: the error is laid on each turned call whose first argument
## is the plain `name` within the definition's statements.
blame_definition : Source.File, Dict(U64, U64), Candidates.CheckError, List(Candidates.Candidate) -> { cands : List(Candidates.Candidate), hit : Bool }
blame_definition = |file, line_of_node, error, cands| {
	if error.title != "missing method" {
		return { cands, hit: Bool.False }
	}
	var $cands = cands
	var $hit = Bool.False
	for definition in file.nodes {
		name = Literals.defined_name(file, definition)
		if name != "" and (line_of_node.get(Source.kid(definition, 2)) ?? 0) == error.line {
			why = "roc check: ${error.title} (on `${name}`'s definition)"
			is_blamed = |cand| {
				first = Source.kid_node(file, Source.node_at(file, cand.node), 2)
				cand.from_line > 0 and first.kind == "e-ident" and first.raw == name and is_below(file, cand.node, definition.parent)
			}
			if $cands.any(is_blamed) {
				$hit = Bool.True
				$cands = $cands.map(
					|cand| if is_blamed(cand) and cand.why_left == "" {
						{ ..cand, why_left: why }
					} else {
						cand
					},
				)
			}
		}
	}
	{ cands: $cands, hit: $hit }
}
