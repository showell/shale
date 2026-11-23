# A plant for boundaries' `entry`: both callers start the recursion at
# `at: 0` and its own call passes `at + 1`, so `at` is a parameter the
# recursion needs, and the callers want an entry function.
count_from : List(U8), { at : U64 } -> U64
count_from = |s, { at }| if at >= List.len(s) at else count_from(s, { at: at + 1 })

first : List(U8) -> U64
first = |s| count_from(s, { at: 0 })

second : List(U8) -> U64
second = |s| count_from(List.drop_first(s, 1), { at: 0 })
