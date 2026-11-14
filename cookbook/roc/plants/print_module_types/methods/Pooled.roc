## Calls `plus` and applies `<=` on values the walk can't type, an inner
## function's parameters, so both count by name alone.
Pooled :: [].{

	sums : List(Str) -> List(Str)
	sums = |things| things.map(|t| t.plus(t))

	ordered : List(Str) -> List(Bool)
	ordered = |xs| xs.map(|x| x <= x)
}
