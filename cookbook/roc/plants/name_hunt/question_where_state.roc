# A plant for name_hunt's `question`: a `Bool` named as a state, with a
# where clause.
marked : List(a) -> Bool where [a.is_eq : a, a -> Bool]
marked = |_| True
