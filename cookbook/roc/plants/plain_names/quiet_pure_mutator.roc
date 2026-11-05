# A clean plant for `pure_mutator`.
Paint := [].{
	push_cat : List(U64), U64 -> List(U64)
	push_cat = |cats, cat| cats.append(cat)

	set_speed : Rider, F64 -> Rider
	set_speed = |rider, speed| { ..rider, speed }

	fill! : U64 => List(U64)
	fill! = |row| [row]

	fill_color : U32
	fill_color = 7

	fill_guess = |row| [row]

	gather_row : U64 -> List(U64)
	gather_row = |row| [row]
}
