# A plant for boundaries' `single`: a file whose name ends `spec.roc` but
# names no module is no spec app, so its one-line wrapper with one caller
# is production's.
padded : Str -> Str
padded = |s| Str.concat(s, " ")

framed : Str -> Str
framed = |s| {
	p = padded(s)
	Str.concat(p, "|")
}
