## Under the types, `phrase_name` reports an unannotated `from_clock` that the
## checker shows neither takes nor gives the type its module has.
PhraseNameFromInferred := [Made(U64)].{
	from_clock = |n| n + 1
}

expect PhraseNameFromInferred.from_clock(1) == 2
