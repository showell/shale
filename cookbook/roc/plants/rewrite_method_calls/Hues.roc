## Peers' and PeerMethods' helper: an app's own type, whose `relative`
## takes two of it, as an app's `relative(a : Color, b : Color)`.
Hues := U32.{
	red : Hues
	red = Hues.(3)

	blue : Hues
	blue = Hues.(1)

	relative : Hues, Hues -> I64
	relative = |Hues.(a), Hues.(b)| a.to_i64() - b.to_i64()
}
