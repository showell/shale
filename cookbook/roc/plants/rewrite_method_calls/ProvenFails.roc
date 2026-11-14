## Under the types (root TypedApp): `U8.to(start, 3)`'s first argument is a
## U8 by the checker (`start` an `if` of literals, which the call makes
## one), and `Iter.fold`'s an Iter, so all three calls are proven; but
## `start.to(3).fold(...)` leaves `start`'s type to default, and the one
## confirm fails. (A constant numeric expression, or a name bound to one, is
## no candidate; a name bound to an `if` of them is, and only the compiler
## or an expect sees it.) The file goes back to rounds: the line's two calls
## stay, with the compiler's reason, and the other is still turned.
ProvenFails := [].{
	bytes : List(U8)
	bytes = {
		start = if 1 > 2 1 else 0
		Iter.fold(U8.to(start, 3), [], |acc, b| acc.append(b))
	}

	size : List(U8) -> U64
	size = |b| List.len(b)
}

expect ProvenFails.size(ProvenFails.bytes) == 4
