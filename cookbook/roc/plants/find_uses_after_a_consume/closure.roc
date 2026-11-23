# Plants for `closure`: a closure over the list called after the write
# (copy-shapes' `closure-called-after`, a copy in every build), beside the
# same closure called inside the write's own arguments, in place. And a
# closure that writes a list its maker holds.
closure_called_before : U64, U64, U64, List(U64), U64 -> U64
closure_called_before = |i, steps, size, xs, sum| {
	if i >= steps {
		sum + xs.len()
	} else {
		j = i % size
		at = |k| xs.get(k) ?? crash "closure_called_before: in range"
		ys = xs.set(j, at(j) + 1) ?? xs
		closure_called_before(i + 1, steps, size, ys, sum + j)
	}
}

closure_called_after : U64, U64, U64, List(U64), U64 -> U64
closure_called_after = |i, steps, size, xs, sum| {
	if i >= steps {
		sum + xs.len()
	} else {
		j = i % size
		at = |k| xs.get(k) ?? crash "closure_called_after: in range"
		ys = xs.set(j, i) ?? xs
		closure_called_after(i + 1, steps, size, ys, sum + at(j)) # closure
	}
}

zeroed_at : List(U64), List(U64) -> List(List(U64))
zeroed_at = |xs, places| places.map(|k| xs.set(k, 0) ?? crash "zeroed_at: in range") # closure
