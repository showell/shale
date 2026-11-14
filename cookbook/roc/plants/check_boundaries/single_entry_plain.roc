# A plant for boundaries' `single`: a wrapper into a function that does not
# call itself is no entry into a recursion, and still reads inline.
spaced : Str, Str -> Str
spaced = |a, b| Str.concat(Str.concat(a, " "), b)

greeting : Str -> Str
greeting = |who| spaced("hello", who)

farewell : Str -> Str
farewell = |who| spaced("bye", who)

loud : Str -> Str
loud = |who| {
	g = greeting(who)
	Str.concat(g, "!")
}
