## Reached only through the second root (`SecondApp`): its call is proven
## by that root's types and confirmed on it.
Second := [].{
	size : List(U8) -> U64
	size = |bytes| List.len(bytes)
}

expect Second.size([1]) == 1
