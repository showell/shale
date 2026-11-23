# A quiet plant: a slice of a list read again after it (copy-shapes'
# `append-length-after`, in place in every build). `drop_last` and its kin
# make a slice of the same memory, so the old list's later read costs no
# copy.
append_length_after : List(U64), U64 -> (List(U64), U64)
append_length_after = |xs, i| {
	ys = xs.drop_last(1).append(i)
	(ys, xs.len())
}
