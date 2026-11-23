# Plants for a dict: the old dict read after an insert (copy-shapes'
# `dict-length-after-insert`) and a value got and inserted back while the
# dict still holds it (`dict-get-then-insert`), both copies in every build,
# beside a plain insert and a value taken out first (got, then removed),
# in place.
dict_insert : Dict(U64, U64), U64, U64 -> U64
dict_insert = |d, j, i| {
	d2 = d.insert(j, i)
	d2.len()
}

dict_length_after_insert : Dict(U64, U64), U64, U64 -> U64
dict_length_after_insert = |d, j, i| {
	d2 = Dict.insert(d, j, i)
	d2.len() + d.len() # after_consume
}

dict_take_then_insert : Dict(U64, List(U64)), U64, U64 -> Dict(U64, List(U64))
dict_take_then_insert = |d, j, i| {
	l = d.get(0) ?? crash "dict_take_then_insert: key 0 is there"
	d0 = d.remove(0)
	d0.insert(0, l.set(j, i) ?? l)
}

dict_get_then_insert : Dict(U64, List(U64)), U64, U64 -> Dict(U64, List(U64))
dict_get_then_insert = |d, j, i| {
	l = Dict.get(d, 0) ?? crash "dict_get_then_insert: key 0 is there"
	d.insert(0, l.set(j, i) ?? l) # held
}

dict_matched : Dict(U64, List(U64)), U64, U64 -> Dict(U64, List(U64))
dict_matched = |d, j, i|
	match d.get(0) {
		Ok(l) => d.insert(0, l.set(j, i) ?? l) # held
		Err(_) => d
	}
