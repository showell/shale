## A plant for name_hunt's `stacked_preposition`: a worker's `_onto`
## appended to an entry already named `rest_from`, two prepositions read
## at once.
rest_from_onto : List(U8), List(U8) -> List(U8)
rest_from_onto = |out, xs|
	match xs {
		[] => out
		[x, .. as rest] => rest_from_onto(List.append(out, x), rest)
	}
