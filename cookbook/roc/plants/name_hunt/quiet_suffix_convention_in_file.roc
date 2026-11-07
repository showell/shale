## Quiet for every check: `_at` is on three stems of the shape `U32 -> F64`
## in this file, a convention, so `segment_at` beside `segment_behind` is no
## split. preposition_split_convention_elsewhere.roc holds the same pair with
## two such stems, and is flagged.
Tower := [].{
	segment_at : U32 -> F64

	segment_behind : U32 -> F64

	height_at : U32 -> F64

	brightness_at : U32 -> F64
}
