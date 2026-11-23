## A call that must be rewritten: its first argument's type is its module's.
Rewrite := [].{
	count : List(U64) -> U64
	count = |xs| List.len(xs)

	joined : Str, Str -> Str
	joined = |a, b| Str.concat(a, b)
}

expect Rewrite.count([1, 2]) == 2
expect Rewrite.joined("a", "b") == "ab"
