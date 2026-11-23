## Peers' and PeerMethods' helper: an app's own type, with a symmetric
## `max` of two Hues, and two functions off the symmetric list that take two
## Hues as well: `relative`, whose first argument is the subject (as Safari's
## `steps_since`), and `plus`, a unit's arithmetic, which reads as infix.
Hues := U32.{
	red : Hues
	red = Hues.(3)

	blue : Hues
	blue = Hues.(1)

	relative : Hues, Hues -> I64
	relative = |Hues.(a), Hues.(b)| a.to_i64() - b.to_i64()

	max : Hues, Hues -> Hues
	max = |Hues.(a), Hues.(b)| Hues.(U32.max(a, b))

	plus : Hues, Hues -> Hues
	plus = |Hues.(a), Hues.(b)| Hues.(a + b)

	is_eq : _
}
