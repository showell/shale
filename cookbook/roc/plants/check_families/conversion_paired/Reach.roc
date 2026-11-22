## A plant for unpaired_conversion: `from_metres` beside `to_metres`, the
## pair named both ways.
Reach :: F64.{
	from_metres : F64 -> Reach
	from_metres = |metres| Reach.(metres)

	to_metres : Reach -> F64
	to_metres = |Reach.(reach)| reach
}
