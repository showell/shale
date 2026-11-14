## A call passed as a function value: the outer call turns, `Str.trim` stays.
FunctionValue := [].{
	trimmed : List(Str) -> List(Str)
	trimmed = |xs| List.map(xs, Str.trim)
}

expect FunctionValue.trimmed([" a "]) == ["a"]
