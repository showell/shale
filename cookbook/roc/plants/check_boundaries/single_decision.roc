# A plant for boundaries' `single`: a decision of one line, with one
# caller, reads as a wrapper.
sign_of : I64 -> Str
sign_of = |n| if n < 0 { "-" } else { "+" }

shown : I64 -> Str
shown = |n| {
	s = sign_of(n)
	Str.concat(s, n.to_str())
}
