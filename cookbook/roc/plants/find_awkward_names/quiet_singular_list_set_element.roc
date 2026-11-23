# Quiet for every check: a changed copy whose object is one element of the
# list it gives names that element, not the list.
Strokes := [].{
	Stroke : { at : U64, width : U64 }

	set_stroke : List(Stroke), Stroke -> List(Stroke)
	set_stroke = |strokes, stroke| strokes.append(stroke)
}
