## A plant for name_hunt's `worker_name`: a recursion's worker named only
## as the helper, `_acc`, where `count_from` says what it carries.
count_acc : List(U8), U64 -> U64
count_acc = |xs, n|
	match xs {
		[] => n
		[_, .. as rest] => count_acc(rest, n + 1)
	}
