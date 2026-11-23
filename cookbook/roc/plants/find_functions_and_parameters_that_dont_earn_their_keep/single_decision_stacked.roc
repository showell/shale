# A plant for boundaries' `single`: single_decision_wide.roc's two-way
# decision laid out over lines, which the tree reads as the same wrapper.
gauge_of : I64 -> Str
gauge_of = |n|
	if n < 0 {
		"below zero: the gauge shows a red needle left of the middle mark"
	} else {
		"at or above zero: the gauge shows a green needle right of it"
	}

shown : I64 -> Str
shown = |n| {
	g = gauge_of(n)
	Str.concat(g, "!")
}
