## The right of a `|>`: the pipe gives the first argument, so it stays.
Pipe := [].{
	both : List(U64), List(U64) -> List(U64)
	both = |xs, ys| xs |> List.concat(ys)
}

expect Pipe.both([1], [2]) == [1, 2]
