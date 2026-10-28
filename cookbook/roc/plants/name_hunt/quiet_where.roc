# Quiet for every check: a question that gives a `Bool`, with a where clause.
is_subset : List(a), List(a) -> Bool where [a.is_eq : a, a -> Bool]
is_subset = |_, _| True
