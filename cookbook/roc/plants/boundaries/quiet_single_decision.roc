# Quiet for boundaries' `single`: a `match` laid out an arm a line is the
# function's own rule, not a wrapper, however few its nodes.
word_of : [Yes, No] -> Str
word_of = |b|
	match b {
		Yes => "yes"
		No => "no"
	}

said : [Yes, No] -> Str
said = |b| {
	w = word_of(b)
	Str.concat(w, "!")
}
