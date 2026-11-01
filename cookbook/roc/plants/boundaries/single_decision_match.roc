# A plant for boundaries' `single`: a `match` of two arms is a two-way
# choice, a wrapper with one caller however many lines its arms take.
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
