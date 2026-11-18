## Under the types (root TypedApp), with `peers=undo`: a method call whose
## receiver and first argument are peers turns back into a module call:
## `a.max(b)` to `U64.max(a, b)`, two Strs' `concat`, an app's own Hues'
## `relative`, and a Tint's `max` with a U64, the alias read through to U64.
## Without it, each is a peer_receiver finding. `xs.get(i)`, a list and an
## index, is no peer call, and stays, as does `xs.fold([], f)`: its two
## arguments are lists, but `List.fold`'s declaration takes a `state`.
import Hues

PeerMethods := [].{
	Tint : U64

	biggest : U64, U64 -> U64
	biggest = |a, b| a.max(b)

	glued : Str, Str -> Str
	glued = |a, b| a.concat(b)

	apart : Hues, Hues -> I64
	apart = |a, b| a.relative(b)

	tinted : Tint, U64 -> U64
	tinted = |tint, n| tint.max(n)

	item : List(U64), U64 -> U64
	item = |xs, i| xs.get(i) ?? 0

	copied : List(U64) -> List(U64)
	copied = |xs| xs.fold([], |acc, x| acc.append(x))
}

expect PeerMethods.biggest(1, 2) == 2
expect PeerMethods.glued("a", "b") == "ab"
expect PeerMethods.apart(Hues.red, Hues.blue) == 2
expect PeerMethods.tinted(3, 4) == 4
expect PeerMethods.item([5], 0) == 5
expect PeerMethods.copied([5]) == [5]
