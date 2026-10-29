# Quiet for boundaries' `single`: one expression past the wrapper's size (24
# nodes) is work of its own, though it is one call with one caller.
summary : Str, Str, Str -> Str
summary = |a, b, c| Str.join_with(
	[
		Str.trim(a),
		Str.trim(b),
		Str.trim(c),
		Str.concat(Str.trim(a), Str.trim(b)),
		Str.concat(Str.trim(b), Str.trim(c)),
		Str.concat(Str.trim(c), Str.trim(a)),
	],
	", ",
)

shown : Str -> Str
shown = |s| {
	t = summary(s, s, s)
	Str.concat(t, "!")
}
