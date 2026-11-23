# Plants for `after_consume` in a helper: the length read after the
# helper's own write (copy-shapes' `helper-length-after-set`, a copy in the
# dev and boxy builds), beside the length read first and a helper that
# hands the list back, both in place.
peek : List(U64) -> (List(U64), U64)
peek = |xs| (xs, xs.len())

helper_returns_list : U64, U64, U64, List(U64), U64 -> U64
helper_returns_list = |i, steps, size, xs, sum| {
	if i >= steps {
		sum + xs.len()
	} else {
		j = i % size
		(xs2, length) = peek(xs)
		ys = xs2.set(j, length) ?? xs2
		helper_returns_list(i + 1, steps, size, ys, sum + length)
	}
}

bump_length_first : List(U64), U64, U64 -> (List(U64), U64)
bump_length_first = |xs, j, v| {
	length = xs.len()
	(xs.set(j, v) ?? xs, length)
}

bump_length_after : List(U64), U64, U64 -> (List(U64), U64)
bump_length_after = |xs, j, v| (xs.set(j, v) ?? xs, xs.len()) # after_consume
