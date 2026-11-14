# A plant for boundaries' `fixed`: both production calls skip `0`.
# quiet_WalkSpec.roc, a test file, skips `3`, and a test file's call is no
# production caller's.
Walk := [].{
	count_from : List(U8), U64 -> U64
	count_from = |s, at| List.len(List.drop_first(s, at))

	whole : List(U8) -> U64
	whole = |s| Walk.count_from(s, 0)

	tail : List(U8) -> U64
	tail = |s| Walk.count_from(List.drop_first(s, 1), 0)
}
