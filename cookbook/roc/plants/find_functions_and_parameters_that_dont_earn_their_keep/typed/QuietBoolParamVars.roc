## Quiet under the types for boundaries' `bool_param`: a `Bool` parameter
## every caller passes a value it computed, never a literal.
QuietBoolParamVars := [].{
	## The label, shouted when `loud`.
	label : Str, Bool -> Str
	label = |text, loud| if loud "${text}!" else text
}
