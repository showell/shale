# Quiet for boundaries' `single`: a pure decision an `expect` reaches.
doubled : U64 -> U64
doubled = |n| n * 2

quadrupled : U64 -> U64
quadrupled = |n| {
	d = doubled(n)
	d + d
}

expect doubled(2) == 4
