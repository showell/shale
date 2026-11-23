# Plants for `after_consume`: a list read after its write (copy-shapes'
# `get-after-set`, which copies in every build), beside the reads before the
# write and the three fallbacks, which write in place.
set_restored : U64, U64, U64, List(U64), U64 -> U64
set_restored = |i, steps, size, xs, sum| {
	if i >= steps {
		sum + xs.len()
	} else {
		j = i % size
		v = xs.get(j) ?? crash "set_restored: in range"
		ys = xs.set(j, v + 1) ?? xs
		set_restored(i + 1, steps, size, ys, sum + v)
	}
}

set_or_empty : U64, U64, U64, List(U64), U64 -> U64
set_or_empty = |i, steps, size, xs, sum| {
	if i >= steps {
		sum + xs.len()
	} else {
		j = i % size
		v = xs.get(j) ?? crash "set_or_empty: in range"
		ys = xs.set(j, v + 1) ?? []
		set_or_empty(i + 1, steps, size, ys, sum + v)
	}
}

set_or_crash : U64, U64, U64, List(U64), U64 -> U64
set_or_crash = |i, steps, size, xs, sum| {
	if i >= steps {
		sum + xs.len()
	} else {
		j = i % size
		ys = List.set(xs, j, i) ?? crash "set_or_crash: in range"
		set_or_crash(i + 1, steps, size, ys, sum + j)
	}
}

get_after_set : U64, U64, U64, List(U64), U64 -> U64
get_after_set = |i, steps, size, xs, sum| {
	if i >= steps {
		sum + xs.len()
	} else {
		j = i % size
		ys = xs.set(j, i) ?? xs
		v = xs.get(j) ?? crash "get_after_set: in range" # after_consume
		get_after_set(i + 1, steps, size, ys, sum + v)
	}
}
