## TestChanged.roc's helper: a `with_ascii_uppercased` of its own, which
## shouts by adding a `!`, where Str's uppercases.
Loud := [].{
	with_ascii_uppercased : Str -> Str
	with_ascii_uppercased = |s| "${s}!"
}

expect Loud.with_ascii_uppercased("a") == "a!"
