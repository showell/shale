# A plant for boundaries' `single`: a one-line wrapper with one caller, no
# expect and no doc.
trimmed : Str -> Str
trimmed = |s| Str.trim(s)

shout : Str -> Str
shout = |s| {
	t = trimmed(s)
	Str.concat(t, "!")
}
