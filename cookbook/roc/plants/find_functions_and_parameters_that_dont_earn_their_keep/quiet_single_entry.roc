# Quiet for boundaries' `single`: an entry into a recursion, a wrapper
# that calls the worker with its start, the shape `fixed` asks for, though
# it has one caller.
sum_from : List(U64), U64 -> U64
sum_from = |ls, at| if at >= List.len(ls) 0 else (List.get(ls, at) ?? 0) + sum_from(ls, at + 1)

total : List(U64) -> U64
total = |ls| sum_from(ls, 0)

rest : List(U64) -> U64
rest = |ls| sum_from(ls, 1)

report : List(U64) -> Str
report = |ls| {
	t = total(ls)
	t.to_str()
}
