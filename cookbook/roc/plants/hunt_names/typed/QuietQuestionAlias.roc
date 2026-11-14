## Quiet under the types for `question`: an annotation that names an alias
## of `Bool` gives a `Bool`.
QuietQuestionAlias := [].{
	Answer : Bool

	is_short : Str -> Answer
	is_short = |s| Str.count_utf8_bytes(s) < 4
}

expect QuietQuestionAlias.is_short("a")
