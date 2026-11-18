## Under the types (root TypedApp): `Try.is_ok(Ok(n))`'s first argument is
## a Try by the checker, but only because the call makes the bare tag one:
## `Ok(n).is_ok()` finds no method. So all three calls open are proven, the
## one confirm fails, and the file goes back to rounds, where that call
## stays with the compiler's reason and the other two are turned.
## `U8.to(start, 3)` takes two U8s, peers, so it stays a module call: as a
## method call, `start`, typed only by literals, would default to a Dec.
ProvenFails := [].{
	bytes : List(U8)
	bytes = {
		start = if 1 > 2 1 else 0
		Iter.fold(U8.to(start, 3), [], |acc, b| acc.append(b))
	}

	size : List(U8) -> U64
	size = |b| List.len(b)

	is_whole : U64 -> Bool
	is_whole = |n| Try.is_ok(Ok(n))
}

expect ProvenFails.size(ProvenFails.bytes) == 4
expect ProvenFails.is_whole(1)
