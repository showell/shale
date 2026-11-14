## A stop, never a run without the types: see the table's `stop`.
Pin := [].{
	size : List(U8) -> U64
	size = |bytes| List.len(bytes)
}
