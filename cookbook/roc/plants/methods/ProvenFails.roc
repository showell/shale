## Under the types (root TypedApp): `U8.to(0, 3)`'s first argument is a U8
## by the checker (the literal the call makes one), and `Iter.fold`'s an
## Iter, so all three calls are proven; but `(0).to(3).fold(...)` leaves
## the literal's type to the method, and the one confirm fails. The file
## goes back to rounds: the line's two calls stay, with the compiler's
## reason, and the other is still turned.
ProvenFails := [].{
	bytes : List(U8)
	bytes = Iter.fold(U8.to(0, 3), [], |acc, b| acc.append(b))

	size : List(U8) -> U64
	size = |b| List.len(b)
}

expect ProvenFails.size(ProvenFails.bytes) == 4
