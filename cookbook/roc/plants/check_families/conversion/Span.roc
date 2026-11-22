## A plant for unpaired_conversion: `metres` gives a `Span` beside
## `to_metres`, so it reads as the way out, not the way in.
Span :: F64.{
	metres : F64 -> Span
	metres = |metres| Span.(metres)

	to_metres : Span -> F64
	to_metres = |Span.(span)| span
}
