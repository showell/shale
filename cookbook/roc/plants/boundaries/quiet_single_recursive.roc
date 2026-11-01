# Quiet for boundaries' `single`: a recursion, a body that calls itself,
# cannot be read inline in its one caller.
digits_acc : U64, U64 -> U64
digits_acc = |n, acc| if n < 10 acc + 1 else digits_acc(n // 10, acc + 1)

width : U64 -> Str
width = |n| {
	d = digits_acc(n, 0)
	d.to_str()
}
