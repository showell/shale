# Quiet for boundaries' `single`: quiet_single_decision_wide.roc's
# three-way chain laid out over lines, the same rule of its own.
gauge_of : I64 -> Str
gauge_of = |n|
	if n < 0 {
		"below zero: a red needle left of the mark"
	} else if n > 0 {
		"above zero: a green needle right of the mark"
	} else {
		"zero: the needle on the mark"
	}

shown : I64 -> Str
shown = |n| {
	g = gauge_of(n)
	Str.concat(g, "!")
}
