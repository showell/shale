## A chained call, turned from the inside out.
Chained := [].{
	big : List(U64) -> U64
	big = |xs| List.len(List.keep_if(xs, |x| x > 1))
}

expect Chained.big([1, 2, 3]) == 2
