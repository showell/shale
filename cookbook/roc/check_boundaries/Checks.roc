## The checks of check_boundaries, each function judged in the order of its
## key, then each `if`: `dead` or `unused_api`, `single`, `fixed` or
## `entry`, `constant`, `bool_param` and `same_branches`. The launcher's
## header says each rule.
import lib.Finding
import lib.Forest
import lib.Headers
import lib.Text
import lib.Tree
import Functions
import References
import Source
import Types

Checks := [].{

	## What the checks read: the forest and each file's lines; the
	## functions and their tallies; the checked files; each file's header
	## names; the modules each folder's main.roc exposes; the types; the
	## words that end a finding's count of the files read; and whether only a
	## production call keeps a member out of `dead`.
	Judging : {
		forest : Forest,
		lines_of : Dict(Str, List(Str)),
		defined : Functions.Defined,
		tallies : List(References.Tally),
		checked : Set(Str),
		headers : Dict(Str, { dictated : Set(Str), exposed : Set(Str) }),
		exposed_modules : Dict(Str, List(Str)),
		typing : Types.Typing,
		files_read : Str,
		is_dead_by_production : Bool,
	}

	## Every finding, in the order the Shale original reported them.
	judge : Checks.Judging -> List(Finding)
	judge = |judging| {
		defined = judging.defined
		order = |first, second| Text.byte_order(Functions.at(defined, first).key, Functions.at(defined, second).key)
		var $places = []
		$index : U64
		var $index = 0
		for _ in defined.functions {
			$places = $places.append($index)
			$index = $index + 1
		}
		var $findings = []
		for place in $places.sort_with(order) {
			function = Functions.at(defined, place)
			counted = judging.tallies.get(place) ?? crash "check_boundaries: a function with no tally"
			$findings = $findings.concat(judge_function(judging, function, counted))
		}
		$findings.concat(same_branches(judging))
	}
}

## A function and what reaches it.
Judged : { function : Functions.Function, counted : References.Tally }

judge_function : Checks.Judging, Functions.Function, References.Tally -> List(Finding)
judge_function = |judging, function, counted| {
	judged = { function, counted }
	is_test = References.is_test_file(function.path)
	is_judged = !(counted.is_ambiguous) and !(is_test)
	at = |check, what| { path: function.path, line: function.line, check, what }
	name = display_name(function)
	read = judging.files_read
	var $findings = []
	if !(is_test) and has_no_caller(judging, judged) and !(is_called_from_outside(judging, function)) {
		$findings = $findings.append(
			if is_library_api(judging, function) {
				at("unused_api", "`${name}` is exposed, and nothing${read} calls or tests it: API with no user here, or left behind?")
			} else if counted.expect_refs + counted.test_refs > 0 {
				at("dead", "`${name}` has no production caller${read}, only an expect or a test: delete it with its tests, or say who calls it")
			} else {
				at("dead", "`${name}` has no caller${read}, and no expect or test reaches it: delete it, or say who calls it")
			},
		)
	}
	if is_judged and reads_better_inline(judged) {
		said = "`${name}` (${function.size}) has one caller${read}, ${counted.first_prod_caller}, and no expect, test or doc"
		$findings = $findings.append(at("single", "${said}: read it inline there, or say why it stands alone"))
	}
	if is_judged and counted.prod_calls.len() >= 2 and counted.value_refs == 0 {
		$findings = $findings.concat(fixed_slots(judging, judged))
	}
	if function.takes_empty_record_alone and !(function.name.contains("!")) and !(is_test) and function.doc_lines == 0 {
		effect = "a constant (`${function.name} = ...`), or a `!` if it has an effect"
		$findings = $findings.append(at("constant", "`${name}` takes `{}` alone and names no effect, so it gives the same value every call: ${effect}"))
	}
	if is_judged and function.params.len() > 1 and judging.typing.real.contains(function.path) {
		$findings = $findings.concat(bool_params(judging, judged))
	}
	$findings
}

## The slots the production calls agree on with a literal: `fixed` if the
## function's own calls agree with them too, `entry` if they vary it.
fixed_slots : Checks.Judging, Judged -> List(Finding)
fixed_slots = |judging, judged| {
	function = judged.function
	counted = judged.counted
	outside = Source.agreed(judging.lines_of, judging.forest, counted.prod_calls)
	every = Source.agreed(judging.lines_of, judging.forest, counted.prod_calls.concat(counted.self_calls))
	own = if counted.self_calls.is_empty() {
		""
	} else {
		" and ${counted.self_calls.len().to_str()} of its own"
	}
	name = display_name(function)
	read = judging.files_read
	at = |check, what| { path: function.path, line: function.line, check, what }
	var $findings = []
	for slot in Text.sort_bytewise(outside.keys()) {
		match outside.get(slot) {
			Ok(Agreed(text)) if Source.is_literal(text) => {
				said = "`${name}`'s argument ${slot} is `${text}` in all ${counted.prod_calls.len().to_str()} production calls"
				match every.get(slot) {
					Ok(Agreed(_)) =>
						if !(is_library_api(judging, function)) {
							$findings = $findings.append(at("fixed", "${said}${own}${read}: no parameter, or an entry function that takes only what varies"))
						}
					_ => {
						varies = "its own calls vary it: an entry function that takes only what varies and passes `${text}` itself, its one outside call"
						$findings = $findings.append(at("entry", "${said}${read}, and ${varies}"))
					}
				}
			}
			_ => {}
		}
	}
	$findings
}

## Each `Bool` parameter, by the checker's type, that some call passes a
## literal: every call counts, a test's and the function's own too.
bool_params : Checks.Judging, Judged -> List(Finding)
bool_params = |judging, judged| {
	function = judged.function
	counted = judged.counted
	every = counted.prod_calls.concat(counted.self_calls).concat(counted.test_calls)
	var $findings = []
	$number : U64
	var $number = 0
	for param in function.params {
		$number = $number + 1
		node = Forest.node(judging.forest, param)
		is_bool = node.kind == "p-ident" and Types.pattern_type(judging.typing, function.path, node) == Ok("Bool")
		if is_bool {
			var $literal_calls = []
			for call in every {
				text = Source.argument_texts(judging.lines_of, judging.forest, call).get($number.to_str()) ?? ""
				if bool_literals.contains(text) {
					$literal_calls = $literal_calls.append("${call.path}:${call.line.to_str()} `${text}`")
				}
			}
			if !($literal_calls.is_empty()) {
				more = if $literal_calls.len() > 3 {
					", ..."
				} else {
					""
				}
				shown = "${Str.join_with($literal_calls.take_first(3), ", ")}${more}"
				counts = "at ${$literal_calls.len().to_str()} of its ${every.len().to_str()} calls (${shown})"
				param_said = "`${display_name(function)}`'s parameter ${$number.to_str()}, `${Tree.attribute(node, "raw")}`"
				tag = "a flag a caller passes is a tag, named for what it means (roc-apps docs/roc-style.md)"
				what = "${param_said}, is a `Bool` (the checker's type) passed as a literal ${counts}: ${tag}"
				$findings = $findings.append({ path: function.path, line: function.line, check: "bool_param", what })
			}
		}
	}
	$findings
}

## Each `if` in a checked file but a test whose two branches are the same
## text, braces aside.
same_branches : Checks.Judging -> List(Finding)
same_branches = |judging| {
	forest = judging.forest
	var $findings = []
	$at : U64
	var $at = 1
	while $at <= Forest.count(forest) {
		path = Forest.path(forest, $at)
		is_judged = !(References.is_test_file(path)) and judging.checked.contains(path)
		if Forest.kind(forest, $at) == "e-if-then-else" and is_judged and Forest.kids(forest, $at).len() == 3 {
			shown = Source.branch_text(judging.lines_of, forest, Forest.kid(forest, $at, 2))
			if shown != "?" and shown == Source.branch_text(judging.lines_of, forest, Forest.kid(forest, $at, 3)) {
				cut = Str.from_utf8_lossy(shown.to_utf8().take_first(40))
				what = "both branches of this `if` are `${cut}`: the condition decides nothing"
				$findings = $findings.append({ path, line: Forest.node(forest, $at).line, check: "same_branches", what })
			}
		}
		$at = $at + 1
	}
	$findings
}

## The pinned Roc defines `Bool := [False, True]`.
bool_literals : Set(Str)
bool_literals = Set.from_list(["Bool.True", "Bool.False", "True", "False"])

display_name : Functions.Function -> Str
display_name = |function|
	if function.owner.starts_with("@") {
		function.name
	} else {
		"${function.owner}.${function.name}"
	}

## A function that would read better inline at its one production call: a
## small wrapper with no other reference, no doc, no recursion and not a
## question.
reads_better_inline : Judged -> Bool
reads_better_inline = |judged| {
	counted = judged.counted
	function = judged.function
	is_question = function.name.starts_with("is_") or function.name.starts_with("has_")
	is_alone = counted.prod_refs == 1 and counted.value_refs == 0 and counted.expect_refs == 0 and counted.test_refs == 0
	is_alone and function.doc_lines == 0 and function.is_wrapper and !(counted.is_recursive) and !(counted.is_entry) and !(is_question)
}

## Whether a function is part of a library's API: a name its own `module`
## header exposes, or a module type's member in a file under a `lib/`
## folder or in a module its package or platform exposes.
is_library_api : Checks.Judging, Functions.Function -> Bool
is_library_api = |judging, function| {
	path = function.path
	header = judging.headers.get(path) ?? { dictated: Set.empty(), exposed: Set.empty() }
	if header.exposed.contains(function.name) {
		Bool.True
	} else if function.owner.starts_with("@") {
		Bool.False
	} else if path.starts_with("lib/") or path.contains("/lib/") or path.contains(":lib/") {
		Bool.True
	} else {
		(judging.exposed_modules.get(Text.dirname(path)) ?? []).contains(Tree.module_name(path))
	}
}

## A function something outside the files calls: what its header dictates,
## or a member Roc calls by name.
is_called_from_outside : Checks.Judging, Functions.Function -> Bool
is_called_from_outside = |judging, function|
	if function.owner.starts_with("@") {
		header = judging.headers.get(function.path) ?? { dictated: Set.empty(), exposed: Set.empty() }
		header.dictated.contains(function.name)
	} else {
		Headers.language_fixed_names.contains(function.name)
	}

## Whether nothing reaches a function. A test of a library's API is a user
## of it, so `unused_api` keeps it.
has_no_caller : Checks.Judging, Judged -> Bool
has_no_caller = |judging, judged| {
	counted = judged.counted
	is_member = !(judged.function.owner.starts_with("@"))
	unreached = counted.prod_refs == 0 and !(counted.is_mentioned)
	if judging.is_dead_by_production and is_member and !(is_library_api(judging, judged.function)) {
		unreached
	} else {
		unreached and counted.expect_refs == 0 and counted.test_refs == 0
	}
}
