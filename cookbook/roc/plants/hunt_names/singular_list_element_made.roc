# A plant for `singular_list`: a function that makes a list from something
# else is no changed copy, so a name of one element is reported.
Strokes := [].{
	Stroke : { at : U64 }

	start_stroke : U64 -> List(Stroke)
	start_stroke = |at| [{ at }]
}
