# Plants for roc-apps' Unique marker: its reads before a write are free, a
# read of the old value after `Unique.set` is found as on a bare list
# (copy-shapes' `unique-get-after-set`, a copy in every build), in the
# module-call form and the method form.
unique_set : Unique(List(U64)), U64, U64 -> Unique(List(U64))
unique_set = |u, j, i| {
	v = Unique.get(u, j)
	Unique.set(u, j, v + i)
}

unique_get_after_set : Unique(List(U64)), U64, U64 -> (Unique(List(U64)), U64)
unique_get_after_set = |u, j, i| {
	u2 = Unique.set(u, j, i)
	(u2, Unique.get(u, j)) # after_consume
}

unique_len_after_append : Unique(List(U64)), U64 -> (Unique(List(U64)), U64)
unique_len_after_append = |u, i| {
	u2 = u.append(i)
	(u2, u.len()) # after_consume
}
