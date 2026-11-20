## Words counted, in English.
Plural := [].{
	## `n` of `word`: `1 file`, `3 files`.
	of : U64, Str -> Str
	of = |n, word| if n == 1 { "1 ${word}" } else { "${U64.to_str(n)} ${word}s" }
}
