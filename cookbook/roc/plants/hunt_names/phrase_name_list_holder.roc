# A plant for `phrase_name`: a list that is no function, named from a
# preposition, hides what it holds.
Plan : { by_slot : List(List(Stop)), cost : U64 }

total_load : List(List(Stop)) -> U64
total_load = |by_slot| 0
