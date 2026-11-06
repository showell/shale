## Quiet under the types for `phrase_name`: the checker shows an unannotated
## constructor gives its own type, and a `from_*` takes it.
QuietPhraseNameInferred := [Made(U64)].{
	at_step = |n| QuietPhraseNameInferred.Made(n + 1)

	from_made = |QuietPhraseNameInferred.Made(n)| n

	is_eq : _
}

expect QuietPhraseNameInferred.at_step(1) == QuietPhraseNameInferred.Made(2)
expect QuietPhraseNameInferred.from_made(QuietPhraseNameInferred.Made(2)) == 2
