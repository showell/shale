# Quiet for boundaries' `entry`: the recursion's start `at: 0` is passed by
# its entry function alone, its one outside call, and both callers go
# through the entry.
count_on : List(U8), { at : U64 } -> U64
count_on = |s, { at }| if at >= List.len(s) at else count_on(s, { at: at + 1 })

count : List(U8) -> U64
count = |s| count_on(s, { at: 0 })

first : List(U8) -> U64
first = |s| count(s)

second : List(U8) -> U64
second = |s| count(List.drop_first(s, 1))
