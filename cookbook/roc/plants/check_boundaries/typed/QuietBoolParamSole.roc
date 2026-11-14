## Quiet under the types for boundaries' `bool_param`: a `Bool` that is a
## function's one parameter is the value it converts (a JSON encoder's
## `bool`), no flag on other arguments, whatever literal a call passes.
QuietBoolParamSole := [].{
	## The word for a truth value.
	word : Bool -> Str
	word = |b| if b "yes" else "no"
}
