# A clean plant for `pure_mutator`.
Paint := [].{
	push_cat : List(U64), U64 -> List(U64)
	push_cat = |cats, cat| cats.append(cat)

	set_speed : Rider, F64 -> Rider
	set_speed = |rider, speed| { ..rider, speed }

	Added : { rider : Rider, cat : U64 }

	add_rider : Rider, U64 -> Try(Rider, [Full])
	add_rider = |rider, _| Ok(rider)

	append_cat : Rider, U64 -> Try({ rider : Rider, cat : U64 }, [Full])
	append_cat = |rider, cat| Ok({ rider, cat })

	add_cat : Rider, U64 -> Try(Paint.Added, [Full])
	add_cat = |rider, cat| Ok({ rider, cat })

	append_hellos : { so_far : List(U8), count : U64 } -> List(U8)
	append_hellos = |{ so_far, count: _ }| so_far

	fill! : U64 => List(U64)
	fill! = |row| [row]

	fill_color : U32
	fill_color = 7

	fill_guess = |row| [row]

	gather_row : U64 -> List(U64)
	gather_row = |row| [row]
}
