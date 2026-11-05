# A plant for `pure_mutator`: four pure functions named like mutations.
Paint := [].{
	push_cat : U64 -> List(U64)
	push_cat = |cat| [cat]

	fill_row : U64 -> List(U64)
	fill_row = |row| [row]

	add_cat : List(U64), U64 -> List(U8)
	add_cat = |_, _| []

	get_speed : Rider -> F64
	get_speed = |rider| rider.speed
}
