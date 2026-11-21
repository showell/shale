## The `pure_mutator` check: an annotated pure function whose first word
## says it changes something though nothing is being added to (the
## launcher's header says the rule).
import lib.Declarations
import lib.Finding
import lib.Text
import lib.Tree

PureMutator := [].{

	## The finding for value `d` of the file at `path`, if it is one.
	## `aliases` are the file's structural type aliases, by name, and `terms`
	## the project's fixed terms that only look like a verb.
	judge : Str, Declarations.ValueDeclared, Dict(Str, Str), Set(Str) -> List(Finding)
	judge = |path, d, aliases, terms| {
		if !(d.is_function) or !(d.annotated) or Str.ends_with(d.name, "!") or opens_with_term(d.name, terms) {
			return []
		}
		first_word = Text.before_first(d.name, "_")
		is_mutation = is_mutation_word(first_word) and !(gives_first_back(d, aliases))
		if first_word == "get" or is_mutation {
			[{ path, line: d.line, check: "pure_mutator", what: "`${d.name}` is pure and gives a new `${d.result_type}`: name it for what it gives" }]
		} else {
			[]
		}
	}
}

## The value of a `Try(value, errors)`, or `result` when it is no `Try`.
value_of_try : Str -> Str
value_of_try = |result| {
	if !(Str.starts_with(result, "Try(")) {
		return result
	}
	bytes = Str.to_utf8(result)
	var $depth = 0
	var $k = 4
	while $k < List.len(bytes) {
		b = List.get(bytes, $k) ?? 0
		if b == 40 or b == 123 or b == 91 {
			$depth = $depth + 1
		} else if b == 41 or b == 125 or b == 93 {
			if $depth == 0 {
				return result
			}
			$depth = $depth - 1
		} else if b == 44 and $depth == 0 {
			return Str.trim(Text.bytes_of(result, { start: 4, count: $k - 4 }))
		}
		$k = $k + 1
	}
	result
}

record_holds : Str, Str -> Bool
record_holds = |record, type_name|
	Str.starts_with(record, "{") and (Str.contains(record, " : ${type_name},") or Str.contains(record, " : ${type_name} }"))

## Whether a function's result is what its first argument was added to.
gives_first_back : Declarations.ValueDeclared, Dict(Str, Str) -> Bool
gives_first_back = |d, aliases| {
	value = value_of_try(d.result_type)
	if value == d.first_type or record_holds(d.first_type, value) {
		Bool.True
	} else {
		record_holds(Dict.get(aliases, Tree.last_part(value)) ?? value, d.first_type)
	}
}

opens_with_term : Str, Set(Str) -> Bool
opens_with_term = |name, terms| Set.to_list(terms).any(|term| name == term or Str.starts_with(name, "${term}_"))

is_mutation_word : Str -> Bool
is_mutation_word = |w| w == "push" or w == "fill" or w == "set" or w == "append" or w == "add"
