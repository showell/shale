# Quiet for boundaries' `single`: a `match` of three arms is the function's
# own rule, not a wrapper, however few its nodes.
word_of : [Yes, No, Maybe] -> Str
word_of = |b|
	match b {
		Yes => "yes"
		No => "no"
		Maybe => "maybe"
	}

said : [Yes, No, Maybe] -> Str
said = |b| {
	w = word_of(b)
	Str.concat(w, "!")
}
