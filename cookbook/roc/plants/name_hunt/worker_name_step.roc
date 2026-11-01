## A plant for name_hunt's `worker_name`: a `_step` that calls itself is a
## recursion's worker, named only as the helper.
Tree := [].{
	depth_step : List(U8), U64 -> U64
	depth_step = |xs, at|
		match xs {
			[] => at
			[_, .. as rest] => Tree.depth_step(rest, at + 1)
		}
}
