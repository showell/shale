## Edges for find_awkward_names under the types: a member alias of `Bool` that the
## checker writes with its owner, an unannotated question, a list given by an
## unannotated function, and a state named function that gives a `Bool`.
Inferred := [].{
	Answer : Bool

	is_wide : U64 -> Inferred.Answer
	is_wide = |width| width > 2

	has_any = |items| List.len(items)

	with_more = |items| List.append(items, 1)

	marked = |number| number > 0

	corner = |number| [number]
}

expect Inferred.is_wide(3)
expect Inferred.corner(1) == [1]
