## Quiet for `worker_name`: the entry function takes the plain name, and its
## worker is named for its extra parameter.
count : List(U8) -> U64
count = |xs| count_from(xs, 0)

count_from : List(U8), U64 -> U64
count_from = |xs, at|
	match xs {
		[] => at
		[_, .. as rest] => count_from(rest, at + 1)
	}
