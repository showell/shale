# A plant for `no_value`: four ordinary values kept to mean none, one an
# alias of a record, which is no function.
Arc := [].{
	Painter : { strokes : U64 }

	no_painter : Painter
	no_painter = { strokes: 0 }

	no_frames : Steps
	no_frames = Steps.of(1000000000)

	unset_id : U64
	unset_id = 0

	none_left : Str
	none_left = ""
}
