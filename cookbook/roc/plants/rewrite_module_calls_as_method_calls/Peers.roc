## Under the types (root TypedApp): a call of a symmetric function
## (symmetric_functions.txt) whose first two arguments are of one type stays
## a module call, since neither is the subject: `max` of two U64s, two Strs'
## `concat`, the app's own Hues' `max`, a Tint's `max` with a U64 (the
## alias read through to U64), `map2` of two lists, and `bitwise_and`.
## Off the list, each call turns, whatever its arguments: `List.get`,
## `List.fold`, `Str.starts_with`, Hues' `relative` (its first argument the
## subject) and Hues' `plus` (a unit's arithmetic, read as infix).
import Hues

Peers := [].{
	Tint : U64

	biggest : U64, U64 -> U64
	biggest = |a, b| U64.max(a, b)

	glued : Str, Str -> Str
	glued = |a, b| Str.concat(a, b)

	brighter : Hues, Hues -> Hues
	brighter = |a, b| Hues.max(a, b)

	tinted : Tint, U64 -> U64
	tinted = |tint, n| U64.max(tint, n)

	labels : List(U64), List(Str) -> List(Str)
	labels = |xs, ys| List.map2(xs, ys, |x, y| "${x.to_str()}${y}")

	masked : U64, U64 -> U64
	masked = |a, b| U64.bitwise_and(a, b)

	item : List(U64), U64 -> U64
	item = |xs, i| List.get(xs, i) ?? 0

	copied : List(U64) -> List(U64)
	copied = |xs| List.fold(xs, [], |acc, x| acc.append(x))

	prefixed : Str, Str -> Bool
	prefixed = |a, b| Str.starts_with(a, b)

	apart : Hues, Hues -> I64
	apart = |a, b| Hues.relative(a, b)

	mixed : Hues, Hues -> Hues
	mixed = |a, b| Hues.plus(a, b)
}

expect Peers.biggest(1, 2) == 2
expect Peers.glued("a", "b") == "ab"
expect Peers.brighter(Hues.red, Hues.blue) == Hues.red
expect Peers.tinted(3, 4) == 4
expect Peers.labels([1], ["a"]) == ["1a"]
expect Peers.masked(6, 3) == 2
expect Peers.item([5], 0) == 5
expect Peers.copied([5]) == [5]
expect Peers.prefixed("ab", "a")
expect Peers.apart(Hues.red, Hues.blue) == 2
expect Peers.mixed(Hues.red, Hues.blue).relative(Hues.blue) == 3
