# A plant for `pure_mutator`: pure functions named like mutations.
Paint := [].{
	push_cat : U64 -> List(U64)
	push_cat = |cat| [cat]

	fill_row : U64 -> List(U64)
	fill_row = |row| [row]

	add_cat : List(U64), U64 -> List(U8)
	add_cat = |_, _| []

	get_speed : Rider -> F64
	get_speed = |rider| rider.speed

	add_to_count : Rider, U64 -> Try(U64, [Full])
	add_to_count = |_, n| Ok(n)

	append_cat : Rider, U64 -> Try({ count : U64, cat : U64 }, [Full])
	append_cat = |_, n| Ok({ count: n, cat: n })

	set_ride : Rider, U64 -> List(Rider)
	set_ride = |rider, _| [rider]
}
