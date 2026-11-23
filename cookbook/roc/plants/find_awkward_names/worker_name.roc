## A plant for find_awkward_names's `worker_name`: a recursion's worker named only
## as the helper, `_acc`, where a `from` would say what it carries.
count_acc : List(U8), U64 -> U64
count_acc = |xs, number|
	match xs {
		[] => number
		[_, .. as rest] => count_acc(rest, number + 1)
	}
