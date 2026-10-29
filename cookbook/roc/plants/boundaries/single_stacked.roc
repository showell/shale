# A plant for boundaries' `single`: a one-expression wrapper with one
# caller, its record laid out a field a line (as `stack.shale write=1`
# leaves it). Its lines are many; its body is still one call.
labelled : Str -> Str
labelled = |s| Str.join_with(
	[
		"note",
		s,
		"end",
	],
	": ",
)

shout : Str -> Str
shout = |s| {
	t = labelled(s)
	Str.concat(t, "!")
}
