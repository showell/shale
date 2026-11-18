## Under the types (root TypedApp): a call whose first two arguments are
## peers, values of one type, stays a module call, since neither is the
## subject: two U64s, two Strs, an app's own two Hues, and a Tint beside a
## U64, the alias read through to U64. `List.get(xs, i)`, a list and an
## index, is no peer call, and turns, as does `List.fold(xs, [], f)`: its
## two arguments are lists, but its declaration takes a `state`.
import Hues

Peers := [].{
	Tint : U64

	biggest : U64, U64 -> U64
	biggest = |a, b| U64.max(a, b)

	glued : Str, Str -> Str
	glued = |a, b| Str.concat(a, b)

	apart : Hues, Hues -> I64
	apart = |a, b| Hues.relative(a, b)

	tinted : Tint, U64 -> U64
	tinted = |tint, n| U64.max(tint, n)

	item : List(U64), U64 -> U64
	item = |xs, i| List.get(xs, i) ?? 0

	copied : List(U64) -> List(U64)
	copied = |xs| List.fold(xs, [], |acc, x| acc.append(x))
}

expect Peers.biggest(1, 2) == 2
expect Peers.glued("a", "b") == "ab"
expect Peers.apart(Hues.red, Hues.blue) == 2
expect Peers.tinted(3, 4) == 4
expect Peers.item([5], 0) == 5
expect Peers.copied([5]) == [5]
