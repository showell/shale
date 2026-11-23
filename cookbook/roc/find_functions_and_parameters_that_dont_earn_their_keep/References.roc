## The second pass of find_functions_and_parameters_that_dont_earn_their_keep: every reference to a function the
## checked files declare, with the call it heads, tallied by function. A
## reference is a function's bare name in the file that defines it,
## `Type.name` anywhere (the type read through the file's `import ... as`),
## a method call `.name(...)`, which counts for every member of that name, or
## a record's shorthand (`{ helper }`). A reference within the function's own
## lines makes it recursive; any other is a test file's, an `expect`'s or
## production's. A name that may reach a member by a way this cannot follow
## (an alias of a type, a name an import exposes) is a mention, which keeps
## every member of that name out of `dead`.
import lib.Forest
import lib.Text
import lib.Tree
import Functions

References := [].{

	## A call: the arguments' places, and an arrow or method call's receiver,
	## its first argument, or 0 for none.
	Call : { path : Str, line : U64, args : List(U64), receiver : U64 }

	## What reaches a function, by kind, in the order the forest holds them.
	Tally : {
		prod_refs : U64,
		value_refs : U64,
		expect_refs : U64,
		test_refs : U64,
		first_prod_caller : Str,
		prod_calls : List(References.Call),
		self_calls : List(References.Call),
		test_calls : List(References.Call),
		is_recursive : Bool,
		is_entry : Bool,
		is_ambiguous : Bool,
		is_mentioned : Bool,
	}

	## Each function's tally, by its place among the functions. `test_paths`
	## are the files `tests=` names: a reference from one of them to a
	## function outside them is a test's.
	tally : Forest, Functions.Ancestry, Functions.Defined, Set(Str) -> List(References.Tally)
	tally = |forest, ancestry, defined, test_paths| {
		found = references_of({ forest, ancestry, defined, aliases: import_aliases(forest) })
		sorted = found.events.sort_with(event_order)
		var $tallies = []
		$cursor : U64
		var $cursor = 0
		for function in defined.functions {
			index = $tallies.len()
			$end : U64
			var $end = $cursor
			while $end < sorted.len() and ((sorted.get($end) ?? crash "find_functions_and_parameters_that_dont_earn_their_keep: no event").function == index) {
				$end = $end + 1
			}
			own = sorted.sublist({ start: $cursor, len: $end - $cursor })
			$cursor = $end
			counted = tally_of(function, own, test_paths)
			$tallies = $tallies.append({
				..counted,
				is_ambiguous: found.ambiguous.contains(index),
				is_mentioned: !(function.owner.starts_with("@")) and found.mentioned.contains(function.name),
			})
		}
		entries = entries_of(defined, $tallies)
		$tallies.map_with_index(|counted, index| { ..counted, is_entry: entries.contains(index) })
	}

	## Whether the file is a test of the rest: a name ending in `twin.roc`, a
	## file under a `twin/` or `test/` folder, or a spec app named for its
	## module followed by `Spec.roc` (`BlitSpec.roc`).
	is_test_file : Str -> Bool
	is_test_file = |path| {
		in_folder = path.contains("/twin/") or path.contains("/test/") or path.starts_with("test/") or path.starts_with("twin/")
		is_spec(Text.basename(path)) or path.ends_with("twin.roc") or in_folder
	}
}

## Where a reference is: its file and line, whether an `expect` holds it,
## and the declaration around it.
Reference : { path : Str, line : U64, in_expect : Bool, enclosing : Str }

## A reference to the function at `function` among them, the `seq`th found:
## the call it heads, or none when the function is passed as a value.
Event : { function : U64, seq : U64, reference : Reference, use : [Called(References.Call), AsValue] }

## What the pass reads: the forest, its ancestry, the functions, and the
## module each file's import aliases stand for, by `path|alias`.
Reading : { forest : Forest, ancestry : Functions.Ancestry, defined : Functions.Defined, aliases : Dict(Str, Str) }

## Each file's import aliases, `path|alias`, and the module each names.
import_aliases : Forest -> Dict(Str, Str)
import_aliases = |forest| {
	var $aliases = Dict.empty()
	$at : U64
	var $at = 1
	while $at <= Forest.count(forest) {
		if Forest.kind(forest, $at) == "s-import" {
			match Tree.find_attribute(Forest.node(forest, $at), "alias") {
				Ok(alias) => {
					$aliases = $aliases.insert("${Forest.path(forest, $at)}|${alias}", Tree.last_part(Forest.raw(forest, $at)))
				}
				Err(_) => {}
			}
		}
		$at = $at + 1
	}
	$aliases
}

## Every reference in the forest, in its order; the functions a method call
## of a shared name reaches; and the names mentioned.
references_of : Reading -> { events : List(Event), ambiguous : Set(U64), mentioned : Set(Str) }
references_of = |reading| {
	forest = reading.forest
	var $events = []
	var $ambiguous = Set.empty()
	var $mentioned = Set.empty()
	$at : U64
	var $at = 1
	while $at <= Forest.count(forest) {
		kind = Forest.kind(forest, $at)
		parent_kind = Forest.kind_or_none(forest, Forest.parent(forest, $at))
		if kind == "e-method-call" {
			name = Str.from_utf8_lossy(Forest.attribute(forest, $at, "method").to_utf8().drop_first(1))
			if name != "" {
				call = method_call(forest, $at)
				keys = reading.defined.member_keys.get(name) ?? []
				for key in keys {
					index = reading.defined.by_key.get(key) ?? crash "find_functions_and_parameters_that_dont_earn_their_keep: a member key with no function"
					$events = $events.append({ function: index, seq: $events.len(), reference: reference_at(reading, $at), use: Called(call) })
					if keys.len() > 1 {
						$ambiguous = $ambiguous.insert(index)
					}
				}
			}
		} else if kind == "e-ident" {
			name = Forest.raw(forest, $at)
			found = if name == "" {
				Err(NoFunction)
			} else {
				referenced(reading, $at, name)
			}
			match found {
				Ok(index) => {
					$events = $events.append({ function: index, seq: $events.len(), reference: reference_at(reading, $at), use: call_headed_by(forest, $at) })
				}
				Err(_) =>
					if name.contains(".") {
						$mentioned = $mentioned.insert(Tree.last_part(name))
					}
			}
		} else if kind == "field" and Forest.kids(forest, $at).is_empty() and parent_kind == "e-record" {
			# `{ helper }` names `helper` with no identifier node. The field has
			# no span, so its record says where it is.
			match referenced(reading, $at, Forest.attribute(forest, $at, "field")) {
				Ok(index) => {
					record = Forest.parent(forest, $at)
					$events = $events.append({ function: index, seq: $events.len(), reference: reference_at(reading, record), use: AsValue })
				}
				Err(_) => {}
			}
		} else if kind == "text" and parent_kind == "exposed-lower-ident" {
			match Tree.find_attribute(Forest.node(forest, $at), "value") {
				Ok(value) => {
					$mentioned = $mentioned.insert(value)
				}
				Err(_) => {}
			}
		}
		$at = $at + 1
	}
	{ events: $events, ambiguous: $ambiguous, mentioned: $mentioned }
}

reference_at : Reading, U64 -> Reference
reference_at = |reading, at| {
	path = Forest.path(reading.forest, at)
	line = Forest.node(reading.forest, at).line
	in_expect = Functions.flag_at(reading.ancestry.in_expect, at)
	{ path, line, in_expect, enclosing: Functions.text_at(reading.ancestry.enclosing, at) }
}

## The function the name at `at` refers to, by its place among them.
referenced : Reading, U64, Str -> Try(U64, [NoFunction])
referenced = |reading, at, name| {
	path = Forest.path(reading.forest, at)
	by_key = reading.defined.by_key
	known = |key|
		match by_key.get(key) {
			Ok(index) => Ok(index)
			Err(_) => Err(NoFunction)
		}
	if name.contains(".") {
		parts = name.split_on(".")
		owner = parts.get(parts.len() - 2) ?? ""
		type_name = reading.aliases.get("${path}|${owner}") ?? owner
		known("${type_name}|${Tree.last_part(name)}")
	} else {
		owner = Functions.text_at(reading.ancestry.owner, at)
		if owner != "" and by_key.contains("${owner}|${name}") {
			known("${owner}|${name}")
		} else if by_key.contains("@${path}|${name}") {
			known("@${path}|${name}")
		} else {
			var $found = Err(NoFunction)
			for key in reading.defined.member_keys.get(name) ?? [] {
				index = by_key.get(key) ?? crash "find_functions_and_parameters_that_dont_earn_their_keep: a member key with no function"
				if Functions.at(reading.defined, index).path == path {
					$found = Ok(index)
				}
			}
			$found
		}
	}
}

## The call whose head is the identifier at `at`, or none. In `x->f(a)`, the
## apply `f(a)` is the arrow call's second kid, and `x` its receiver.
call_headed_by : Forest, U64 -> [Called(References.Call), AsValue]
call_headed_by = |forest, at| {
	apply = Forest.parent(forest, at)
	if Forest.kind_or_none(forest, apply) != "e-apply" or Forest.kid(forest, apply, 1) != at {
		AsValue
	} else {
		arrow = Forest.parent(forest, apply)
		is_arrow = Forest.kind_or_none(forest, arrow) == "e-arrow-call" and Forest.kid(forest, arrow, 2) == apply
		receiver = if is_arrow {
			Forest.kid(forest, arrow, 1)
		} else {
			0
		}
		args = Forest.kids(forest, apply).drop_first(1)
		Called({ path: Forest.path(forest, at), line: Forest.node(forest, at).line, args, receiver })
	}
}

## The method call at `at`, its receiver the receiver node's value.
method_call : Forest, U64 -> References.Call
method_call = |forest, at| {
	var $args = []
	var $receiver = 0
	for kid in Forest.kids(forest, at) {
		kind = Forest.kind(forest, kid)
		if kind == "receiver" {
			$receiver = Forest.kid(forest, kid, 1)
		}
		if kind.starts_with("args") {
			$args = Forest.kids(forest, kid)
		}
	}
	{ path: Forest.path(forest, at), line: Forest.node(forest, at).line, args: $args, receiver: $receiver }
}

## By function, then in the order found.
event_order : Event, Event -> [Before, Same, After]
event_order = |first, second|
	if first.function != second.function {
		if first.function < second.function {
			Before
		} else {
			After
		}
	} else if first.seq < second.seq {
		Before
	} else if first.seq > second.seq {
		After
	} else {
		Same
	}

## A function's tally from its own references, in the order found.
tally_of : Functions.Function, List(Event), Set(Str) -> References.Tally
tally_of = |function, events, test_paths| {
	is_own = |event| event.reference.path == function.path and event.reference.line >= function.line and event.reference.line <= function.end_line
	own = events.keep_if(is_own)
	outside = events.drop_if(is_own)
	is_from_test_file = |event| References.is_test_file(event.reference.path) or (test_paths.contains(event.reference.path) and !(test_paths.contains(function.path)))
	from_tests = outside.keep_if(is_from_test_file)
	from_expects = outside.drop_if(is_from_test_file).keep_if(|event| event.reference.in_expect)
	production = outside.drop_if(is_from_test_file).drop_if(|event| event.reference.in_expect)
	prod_calls = calls_of(production)
	first_prod_caller = match production.first() {
		Ok(event) => "${event.reference.path}:${event.reference.line.to_str()} in ${event.reference.enclosing}"
		Err(_) => ""
	}
	{
		prod_refs: production.len(),
		value_refs: production.len() - prod_calls.len(),
		expect_refs: from_expects.len(),
		test_refs: from_tests.len(),
		first_prod_caller,
		prod_calls,
		self_calls: calls_of(own),
		test_calls: calls_of(outside.keep_if(|event| is_from_test_file(event) or event.reference.in_expect)),
		is_recursive: !(own.is_empty()),
		is_entry: Bool.False,
		is_ambiguous: Bool.False,
		is_mentioned: Bool.False,
	}
}

## The functions holding a production call of a recursive function: the
## entries into a recursion, by their places among the functions.
entries_of : Functions.Defined, List(References.Tally) -> Set(U64)
entries_of = |defined, tallies| {
	var $by_path = Dict.empty()
	$index : U64
	var $index = 0
	for function in defined.functions {
		$by_path = $by_path.insert(function.path, ($by_path.get(function.path) ?? []).append($index))
		$index = $index + 1
	}
	var $entries = Set.empty()
	for counted in tallies {
		if counted.is_recursive {
			for call in counted.prod_calls {
				for index in $by_path.get(call.path) ?? [] {
					holder = Functions.at(defined, index)
					if call.line >= holder.line and call.line <= holder.end_line {
						$entries = $entries.insert(index)
					}
				}
			}
		}
	}
	$entries
}

## Whether a file's name is a spec app's: `Spec.roc` after a run of letters
## and digits that holds a capital.
is_spec : Str -> Bool
is_spec = |base|
	if !(base.ends_with("Spec.roc")) {
		Bool.False
	} else {
		before = base.drop_suffix("Spec.roc").to_utf8()
		is_alphanumeric = |byte| (byte >= 'a' and byte <= 'z') or (byte >= 'A' and byte <= 'Z') or (byte >= '0' and byte <= '9')
		run_start = match before.find_last_index(|byte| !(is_alphanumeric(byte))) {
			Ok(index) => index + 1
			Err(_) => 0
		}
		before.drop_first(run_start).any(|byte| byte >= 'A' and byte <= 'Z')
	}

## The calls among the references, in their order.
calls_of : List(Event) -> List(References.Call)
calls_of = |events| {
	var $calls = []
	for event in events {
		match event.use {
			Called(call) => {
				$calls = $calls.append(call)
			}
			AsValue => {}
		}
	}
	$calls
}
