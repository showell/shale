## Under the types, boundaries' `bool_param`: a `Bool` parameter that a
## caller passes as a literal, `Bool.True`, where a tag would say what the
## flag means (`Loud`).
BoolParam := [].{
	## The label, shouted when `loud`.
	label : Str, Bool -> Str
	label = |text, loud| if loud "${text}!" else text
}
