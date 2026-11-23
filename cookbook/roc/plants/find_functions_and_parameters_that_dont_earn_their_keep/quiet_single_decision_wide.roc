# Quiet for boundaries' `single`: a three-way `else if` chain is a rule of
# its own, though one line holds it and its nodes are few
# (quiet_single_decision_stacked.roc is the same chain stacked, and the
# same verdict).
gauge_of : I64 -> Str
gauge_of = |n| if n < 0 { "below zero: a red needle left of the mark" } else if n > 0 { "above zero: a green needle right of the mark" } else { "zero: the needle on the mark" }

shown : I64 -> Str
shown = |n| {
	g = gauge_of(n)
	Str.concat(g, "!")
}
