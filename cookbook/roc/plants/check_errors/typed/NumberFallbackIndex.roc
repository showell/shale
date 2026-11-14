## Under the types, errors' `number_fallback`: a number falling back for a
## lookup that fails only past the end of a list, a sentinel for a bug.
NumberFallbackIndex := [].{
	seat_count : List(U64), U64 -> U64
	seat_count = |counts, seat| List.get(counts, seat) ?? 0
}

expect NumberFallbackIndex.seat_count([3], 0) == 3
