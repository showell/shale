# A plant for hunt_names's `question`: a `Bool` named as a state, with a
# where clause.
marked : List(a) -> Bool where [a.is_eq : a, a -> Bool]
marked = |_| True
