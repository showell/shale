## Quiet for `stacked_preposition`: a worker's one preposition reads once,
## and a preposition mid-name (`count_from_start`) is followed by a noun.
rest_onto : List(U8), List(U8) -> List(U8)
rest_onto = |out, xs|
	match xs {
		[] => out
		[x, .. as rest] => rest_onto(List.append(out, x), rest)
	}

walk_with : List(U8), U64 -> U64
walk_with = |xs, seen|
	match xs {
		[] => seen
		[_, .. as rest] => walk_with(rest, seen + 1)
	}

count_from_start : List(U8) -> U64
count_from_start = |xs| walk_with(xs, 0)
