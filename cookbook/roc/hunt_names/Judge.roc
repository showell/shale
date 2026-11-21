## hunt_names' checks of one definition (the Shale original's
## `judge_definition`): every check but `container`, `preposition_split`,
## `where_name`, `stale_doc_name` and the bindings' `short_name`, which are
## judged over the files afterwards. The launcher's header says what each
## finds. Also the act an effect's name starts with, which the census
## counts and `noun_effect` and `state_effect` judge.
import lib.Finding
import Facts
import ListNames
import Makers
import Reading
import TypeText
import Words

Judge := [].{

	## The word an effect's name acts by: its first, or the one after a
	## namespace prefix; what that word says (`participle`, `verb`, `vague`,
	## `asks` or `refused`); and how a finding names its place.
	Act : { word : Str, verdict : Str, place : Str }

	## What a name's first word says: `participle`, `verb`, `vague`, `asks`
	## (`is`, `has`) or `refused`.
	first_word_verdict : Str, Words.Lists -> Str
	first_word_verdict = |word, lists|
		if Words.reads_as_participle(word, lists.participles, lists.ed_verbs) {
			"participle"
		} else if lists.verbs.contains(word) {
			"verb"
		} else if lists.vague.contains(word) {
			"vague"
		} else if word == "is" or word == "has" {
			"asks"
		} else {
			"refused"
		}

	## Whether the name is an effect, its `!` its own rather than a
	## callback's.
	is_effectful : Reading.Definition -> Bool
	is_effectful = |definition| definition.name.ends_with("!") and !(definition.has_callback)

	## The act of a name's words, `is_effect` saying whether it is an
	## effect's: a namespace prefix counts only before an effect's act.
	act_of : List(Str), Bool, Words.Lists -> Judge.Act
	act_of = |words, is_effect, lists| {
		first = word_at(words, 1)
		namespace = if is_effect and words.len() > 1 {
			lists.namespaces.get(first)
		} else {
			Err(KeyNotFound)
		}
		match namespace {
			Ok(instructions) => {
				word = word_at(words, 2)
				said = Judge.first_word_verdict(word, lists)
				verdict = if said == "refused" and instructions.contains(word) {
					"verb"
				} else {
					said
				}
				{ word, verdict, place: "the word after its namespace `${first}_`" }
			}
			Err(_) => { word: first, verdict: Judge.first_word_verdict(first, lists), place: "its first word" }
		}
	}

	## What each check judged here finds in one definition, in the Shale
	## original's order. `facts` are every file's.
	findings : Reading.Definition, Words.Lists, Facts.Gathered -> List(Finding)
	findings = |definition, lists, facts| {
		full_name = definition.name
		if lists.fixed_names.contains(full_name) {
			return []
		}
		has_bang = full_name.ends_with("!")
		name = full_name.drop_suffix("!")
		is_effect = Judge.is_effectful(definition)
		words = name.split_on("_")
		first = word_at(words, 1)
		judged_here = !(definition.is_paired)
		is_participle = |word| Words.reads_as_participle(word, lists.participles, lists.ed_verbs)
		act = Judge.act_of(words, is_effect, lists)
		named_as_state = is_participle(act.word) and !(name.contains("_is_") or name.contains("_has_"))
		var $found = []
		report = |check, what| { path: definition.path, line: definition.line, check, what }
		draws_on_surface = match definition.arguments.first() {
			Ok(argument) =>
				match argument.shape {
					Named(named) => named == "Frame" or named == "Canvas"
					_ => Bool.False
				}
			Err(_) => Bool.False
		}
		if is_effect and judged_here and act.verdict == "refused" and !(draws_on_surface) {
			what = if lists.prepositions.contains(act.word) {
				"`${full_name}` has an effect but ${act.place} is a preposition, `${act.word}`: an effect is named for its act"
			} else {
				"`${full_name}` has an effect but ${act.place}, `${act.word}`, is no verb: it reads as a value, not an act"
			}
			$found = $found.append(report("noun_effect", what))
		}
		names_drawn_type = words.len() > 1 and definition.mentioned_types.contains(TypeText.camel_case(words))
		if is_effect and judged_here and named_as_state and !(names_drawn_type) {
			$found = $found.append(report("state_effect", "`${full_name}` has an effect but is named as a state, `${act.word}`: a verb says what it does"))
		}
		named_as_question = words.len() > 1 and ["is", "has", "can", "should"].contains(first)
		signature = signature_of(definition, facts)
		returns_bool = signature.is_function and signature.result == "Bool"
		if named_as_question and judged_here and is_effect and !(returns_bool) {
			gives = if signature.is_function {
				" and gives `${signature.result}`${signature.source_note}"
			} else {
				""
			}
			$found = $found.append(report("question", "`${full_name}` asks a question but has an effect${gives}"))
		}
		if returns_bool and named_as_state and judged_here {
			$found = $found.append(report("question", "`${full_name}` gives a `Bool`${signature.source_note} but is named as a state, `${first}`: ask it (`is_*`, `has_*`)"))
		}
		if named_as_question and judged_here and signature.is_known and signature.is_function and !(is_effect) and signature.result != "Bool" and signature.result != "_" {
			$found = $found.append(report("question", "`${full_name}` asks a question but gives `${signature.result}`${signature.source_note}"))
		}
		if judged_here {
			here = { definition, words, signature, has_bang, returns_bool }
			$found = $found.concat(judged_where_defined(here, lists, facts))
		}
		$found.concat(judged_anywhere(definition, words, lists, facts))
	}
}

## The word at place `at` of `words`, counted from 1.
word_at : List(Str), U64 -> Str
word_at = |words, at|
	if at == 0 {
		""
	} else {
		words.get(at - 1) ?? ""
	}

## What a definition's type says. The type is the checker's when it gave
## one, else the annotation's; it is unknown for a definition with neither.
signature_of : Reading.Definition, Facts.Gathered -> Makers.Signature
signature_of = |definition, facts|
	match definition.typed {
		Typed(written) => {
			parts = TypeText.parts_of(written)
			result = if facts.bool_aliases.contains(parts.result) {
				"Bool"
			} else {
				parts.result
			}
			{ is_function: parts.is_function, first: TypeText.first_argument(parts.arguments), result, is_known: Bool.True, source_note: " (the checker's type)" }
		}
		Untyped => {
			is_function: definition.is_annotation and definition.is_function_type,
			first: definition.first,
			result: definition.result,
			is_known: definition.is_annotation,
			source_note: "",
		}
	}

## A definition judged where it is defined, not at an annotation paired
## with it: its words, what its type says, whether it ends in `!` and gives
## a `Bool`.
Here : {
	definition : Reading.Definition,
	words : List(Str),
	signature : Makers.Signature,
	has_bang : Bool,
	returns_bool : Bool,
}

## The checks judged only where a definition is judged, not at the
## definition an annotation is paired with.
judged_where_defined : Here, Words.Lists, Facts.Gathered -> List(Finding)
judged_where_defined = |here, lists, facts| {
	definition = here.definition
	signature = here.signature
	words = here.words
	full_name = definition.name
	name = full_name.drop_suffix("!")
	first = word_at(words, 1)
	last_word = word_at(words, words.len())
	report = |check, what| { path: definition.path, line: definition.line, check, what }
	var $found = []
	is_entry_point = words.len() == 1 and ["handle", "run", "main"].contains(first)
	if words.all(|word| lists.vague.contains(word)) and !(is_entry_point) {
		$found = $found.append(report("vague", "`${full_name}` is only vague words: what does it handle, and how?"))
	}
	if name.count_utf8_bytes() == 1 {
		$found = $found.append(report("short_name", "`${full_name}` is one letter, read far from where it is defined: name what it holds"))
	}
	leading = if words.len() > 2 {
		words.take_first(words.len() - 2)
	} else {
		[]
	}
	has_leading_noun = leading.any(|word| !(lists.prepositions.contains(word)))
	before_last = word_at(words, words.len() - 1)
	if has_leading_noun and lists.prepositions.contains(last_word) and lists.prepositions.contains(before_last) {
		$found = $found.append(report("stacked_preposition", "`${full_name}` stacks two prepositions, `_${before_last}_${last_word}`: let each read once, the entry named for what it gives (`rest`, `tail_after`) and the worker for what it adds (`rest_onto`)"))
	}
	# A function is known by its type, else by a lambda definition; a value
	# by a type that is no function, else by a definition that is no lambda.
	# An inferred annotation (`two : _`) with no checker's type is neither.
	is_typed = match definition.typed {
		Typed(_) => Bool.True
		Untyped => Bool.False
	}
	has_type = signature.is_known and !(definition.is_annotation and definition.whole == "_" and !(is_typed))
	is_function = signature.is_function or (!(signature.is_known) and definition.is_lambda)
	is_value = (has_type and !(signature.is_function)) or (!(signature.is_known) and !(definition.is_lambda))
	taken_kind = lists.taken.get(name) ?? ""
	capitalized = TypeText.camel_case([name])
	if taken_kind == "roc" and is_function {
		gives_the_word = signature.is_function and (signature.result.split_on(".").last() ?? "") == capitalized
		if !(gives_the_word) {
			$found = $found.append(report("taken_word", "`${full_name}` is Roc's own word, `${name}`, for a function that gives no `${capitalized}`: name it for the domain's thing (`element`, `append`), and keep Roc's words for Roc"))
		}
	}
	if taken_kind == "cardinal" and is_value {
		$found = $found.append(report("taken_word", "`${full_name}` is a number's name for a value: it says how many, not what it holds"))
	}
	is_participle = Words.reads_as_participle(name, lists.participles, lists.ed_verbs)
	if !(here.has_bang) and words.len() == 1 and is_participle and is_function and !(here.returns_bool) {
		$found = $found.append(report("bare_participle", "`${full_name}` is a function named by a bare past participle, which reads as a `Bool`: name it by a noun for what it gives (`user_by_name`) or a verb for what it does (`add_user`)"))
	}
	is_phrase = !(here.has_bang) and lists.prepositions.contains(first) and is_function and !(here.returns_bool) and signature.result != "_"
	if is_phrase and !(Makers.reads_as_maker(definition, words, signature, facts, lists.prepositions)) {
		gives = if signature.is_known {
			"gives `${signature.result}`${signature.source_note}"
		} else {
			"has no annotation to show it makes its own type"
		}
		$found = $found.append(report("phrase_name", "`${full_name}` starts with a preposition, `${first}`, and ${gives}: bare, it reads as a yes or no and hides what it gives; name the thing (`trees_in_view`, not `in_view`)"))
	} else if signature.is_known {
		kind = if signature.is_function {
			"function"
		} else {
			"value"
		}
		subject = { path: definition.path, line: definition.line, name: full_name, kind, is_function: signature.is_function, first: signature.first, gives: signature.result }
		$found = $found.concat(ListNames.findings(subject, lists))
	}
	if !(here.has_bang) and is_function and lists.world_verbs.contains(first) and !(definition.has_doc) {
		$found = $found.append(report("world_verb", "`${full_name}` is pure, and `${first}` claims to act on something outside: name it for the value it gives (`tree_shapes`, not `draw_tree`)"))
	}
	$place : U64
	var $place = 0
	for parameter in definition.parameters {
		$place = $place + 1
		parameter_type = definition.argument_types.get($place - 1) ?? parameter.typed
		if parameter.name != "" {
			subject = { path: definition.path, line: parameter.line, name: parameter.name, kind: "parameter", is_function: Bool.False, first: "", gives: parameter_type }
			$found = $found.concat(ListNames.findings(subject, lists))
		}
	}
	$found
}

## The checks judged at an annotation and at its definition alike.
judged_anywhere : Reading.Definition, List(Str), Words.Lists, Facts.Gathered -> List(Finding)
judged_anywhere = |definition, words, lists, facts| {
	full_name = definition.name
	first = word_at(words, 1)
	last_word = word_at(words, words.len())
	report = |check, what| { path: definition.path, line: definition.line, check, what }
	var $found = []
	# `worker_name` is judged at the definition, which knows whether it calls
	# itself, and at an annotation only when no definition follows it, as
	# with a hosted effect.
	is_annotation_alone = definition.is_annotation and !(definition.has_definition)
	is_helper_suffix = helper_suffixes.contains(last_word) and (is_annotation_alone or !(definition.is_annotation))
	is_recursion_suffix = Reading.recursion_suffixes.contains(last_word) and definition.recurses
	if words.len() > 1 and (is_helper_suffix or is_recursion_suffix) {
		$found = $found.append(report("worker_name", "`${full_name}` is named only as a helper, `_${last_word}`: name the worker for the parameter it adds (`_from` a start point, `_onto` an output built, `_with` other carried state) and give the entry function the plain name"))
	}
	if definition.is_annotation and !(definition.is_function_type) and definition.whole != "_" {
		annotated = definition.whole
		if last_word == "count" and !(integer_types.contains(annotated)) {
			$found = $found.append(report("name_type", "`${full_name}` is a count of type `${annotated}`"))
		}
		if last_word == "path" and annotated != "Str" and !(annotated.contains("Path")) {
			$found = $found.append(report("name_type", "`${full_name}` is a path of type `${annotated}`"))
		}
		if annotated == "Bool" and (first == "no" or first == "not") and words.len() > 1 {
			$found = $found.append(report("negative", "`${full_name}` is a negative flag: `${full_name} = Bool.false` reads twice"))
		}
	}
	if definition.is_annotation and Judge.is_effectful(definition) and definition.is_function_type {
		for argument in definition.arguments {
			$found = $found.concat(tense_findings(definition, argument, lists, facts))
		}
	}
	if definition.is_annotation and words.len() > 2 and first == "is" and word_at(words, 2) == "not" {
		$found = $found.append(report("negative", "`${full_name}` asks a negative: `!${full_name}(...)` reads twice"))
	}
	$found
}

## What `command_tense` and `mixed_tense` find in one argument an effect's
## annotation takes: a tag union written in place, or one a type declared
## in any file checked names.
tense_findings : Reading.Definition, TypeText.ArgumentShape, Words.Lists, Facts.Gathered -> List(Finding)
tense_findings = |definition, argument, lists, facts| {
	union = match argument.shape {
		Written(written) => written
		Named(named) =>
			match facts.unions.get(named) {
				Ok(declared) => declared
				Err(_) => return []
			}
		Unread => return []
	}
	report = |check, what| { path: definition.path, line: definition.line, check, what }
	listed = if argument.is_list {
		"a list of "
	} else {
		""
	}
	shown = "${listed}`[${Str.join_with(union.names, ", ")}]`"
	is_participle = |word| Words.reads_as_participle(word, lists.participles, lists.ed_verbs)
	var $found = []
	all_past = union.names.all(|tag| is_participle(TypeText.tag_first_word(tag)))
	if !(union.has_payload) and union.names.len() >= 2 and all_past {
		$found = $found.append(report("command_tense", "`${definition.name}` takes ${shown}, an instruction named like an event: name its tags as commands (`Pinned` is `Pin`)"))
	}
	event_tags = union.names.keep_if(|tag| is_participle(TypeText.tag_first_word(tag)))
	command_tags = union.names.keep_if(|tag| !(is_participle(TypeText.tag_first_word(tag))) and lists.verbs.contains(TypeText.tag_first_word(tag)))
	if !(command_tags.is_empty()) and !(event_tags.is_empty()) {
		commands = Str.join_with(command_tags, ", ")
		events = Str.join_with(event_tags, ", ")
		$found = $found.append(report("mixed_tense", "`${definition.name}` takes ${shown}: commands (${commands}) beside events (${events}): name what it is told to do in one mood"))
	}
	$found
}

## The suffixes `worker_name` reports always: they say only that a
## function is a helper.
helper_suffixes : List(Str)
helper_suffixes = ["acc", "loop", "inner", "helper", "rec"]

integer_types : List(Str)
integer_types = ["U8", "U16", "U32", "U64", "U128", "I8", "I16", "I32", "I64", "I128"]
