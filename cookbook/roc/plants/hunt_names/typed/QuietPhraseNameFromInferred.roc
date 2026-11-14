## Quiet under the types for `phrase_name`: the checker shows an unannotated
## `from_*` gives its own type inside a record.
QuietPhraseNameFromInferred := [Made(U64)].{
	from_held = |n| { made: QuietPhraseNameFromInferred.Made(n), count: n + 1 }

	is_eq : _
}

expect QuietPhraseNameFromInferred.from_held(1).count == 2
