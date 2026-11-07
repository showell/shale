## Quiet under the types for `phrase_name`: the checker supplies the type an
## unannotated `from_*` needs in a module with no type of its own.
QuietPhraseNameTypelessInferred :: [].{
	from_digits = |n| n + 1
}

expect QuietPhraseNameTypelessInferred.from_digits(1) == 2
