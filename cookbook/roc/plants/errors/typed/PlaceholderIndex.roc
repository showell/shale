## Under the types, errors' `placeholder`: a number falling back for a
## lookup that fails only past the end of a list, a sentinel for a bug.
PlaceholderIndex := [].{
	seat_count : List(U64), U64 -> U64
	seat_count = |counts, seat| List.get(counts, seat) ?? 0
}

expect PlaceholderIndex.seat_count([3], 0) == 3
