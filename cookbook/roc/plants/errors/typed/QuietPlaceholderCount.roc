## Quiet under the types for errors' `placeholder`: a count that is zero
## for a key never seen, or a total of nothing, is an answer, not a
## sentinel.
QuietPlaceholderCount := [].{
	votes : Dict(Str, U64), Str -> U64
	votes = |tally, name| Dict.get(tally, name) ?? 0

	top : List(U64) -> U64
	top = |scores| List.max(scores) ?? 0
}

expect QuietPlaceholderCount.top([]) == 0
