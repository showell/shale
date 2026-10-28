## Quiet for `command_tense`: a state carried into an effect, one of whose
## tags holds the state's data (`Known` with what is known).
Who := [].{
	Asked : [Known(List(Str)), Unasked]

	load_names! : Asked => List(Str)
}
