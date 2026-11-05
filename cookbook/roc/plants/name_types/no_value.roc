# A plant for `no_value`: three ordinary values kept to mean none.
Arc := [].{
	no_frames : Steps
	no_frames = Steps.of(1000000000)

	unset_id : U64
	unset_id = 0

	none_left : Str
	none_left = ""
}
