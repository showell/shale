## Quiet under the types for errors' `number_fallback`: a count that is zero
## for a key never seen, or a total of nothing, is an answer, not a
## sentinel.
QuietNumberFallbackCount := [].{
	votes : Dict(Str, U64), Str -> U64
	votes = |tally, name| Dict.get(tally, name) ?? 0

	top : List(U64) -> U64
	top = |scores| List.max(scores) ?? 0
}

expect QuietNumberFallbackCount.top([]) == 0
