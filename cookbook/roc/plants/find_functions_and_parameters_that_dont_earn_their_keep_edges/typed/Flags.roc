## Edges for find_functions_and_parameters_that_dont_earn_their_keep' `bool_param`: a flag passed as a literal at
## five calls, from the app, from an `expect` and from itself, so three are
## named and the rest elided; a flag in a record, and a flag first.
Flags := [].{
	shown : Str, Bool -> Str
	shown = |text, loud| if loud text else if text == "" shown("x", Bool.True) else text

	boxed : { text : Str, loud : Bool } -> Str
	boxed = |args| if args.loud args.text else ""

	after : Bool, Str -> Str
	after = |loud, text| if loud text else ""
}

expect Flags.shown("t", Bool.False) == "t"
