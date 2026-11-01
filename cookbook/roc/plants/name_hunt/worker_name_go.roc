## A plant for name_hunt's `worker_name`: a `_go` that calls itself is a
## recursion's worker, named only as the helper.
count_go : List(U8), U64 -> U64
count_go = |xs, at|
	match xs {
		[] => at
		[_, .. as rest] => count_go(rest, at + 1)
	}
