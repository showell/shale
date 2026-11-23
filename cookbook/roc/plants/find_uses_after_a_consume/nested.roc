# Plants for `held`: an inner list got, written and set back while the
# outer list still holds it (copy-shapes' `nested-get-then-set`, a copy in
# every build), beside `update`, which hands it over unshared, in place.
nested_update : List(List(U64)), U64, U64 -> List(List(U64))
nested_update = |rows, j, i| rows.update(0, |inner| inner.set(j, i) ?? crash "nested_update: inner in range") ?? crash "nested_update: outer in range"

nested_get_then_set : List(List(U64)), U64, U64 -> List(List(U64))
nested_get_then_set = |rows, j, i| {
	inner = rows.get(0) ?? crash "nested_get_then_set: outer in range"
	rows.set(0, inner.set(j, i) ?? crash "nested_get_then_set: inner in range") ?? crash "nested_get_then_set: outer in range" # held
}

first_dropped : List(List(U64)) -> (List(U64), List(List(U64)))
first_dropped = |rows| {
	head = rows.first() ?? crash "first_dropped: not empty"
	(head.append(0), rows) # held
}
