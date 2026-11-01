# A plant for boundaries' `fixed`: a recursion whose every call, its own
# included, passes `step` as `1`; `at` varies.
count_by : List(U8), U64, U64 -> U64
count_by = |s, step, at| if at >= List.len(s) at else count_by(s, 1, at + step)

first : List(U8) -> U64
first = |s| count_by(s, 1, 0)

later : List(U8) -> U64
later = |s| count_by(s, 1, 3)
