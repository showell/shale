## Quiet under the types for boundaries' `bool_param`: parameters passed
## literals, none of them a `Bool` parameter: a tag of the function's own
## union, and a generic parameter that a call fills with `Bool.True`.
QuietBoolParamOther := [].{
	## The label, shouted for `Loud`.
	label : Str, [Loud, Quiet] -> Str
	label = |text, volume|
		match volume {
			Loud => "${text}!"
			Quiet => text
		}

	## A list of the one item, whatever its type.
	one : item -> List(item)
	one = |x| [x]
}
