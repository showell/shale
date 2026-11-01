# A plant for boundaries' `single`: a two-way decision on one wide line,
# with one caller, reads as a wrapper (single_decision_stacked.roc is the
# same decision stacked, and the same verdict).
gauge_of : I64 -> Str
gauge_of = |n| if n < 0 { "below zero: the gauge shows a red needle left of the middle mark" } else { "at or above zero: the gauge shows a green needle right of it" }

shown : I64 -> Str
shown = |n| {
	g = gauge_of(n)
	Str.concat(g, "!")
}
