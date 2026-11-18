# Quiet for every check: the thing named before its preposition.
Plan : { routes_by_slot : List(List(Stop)), cost : U64 }

total_load : List(List(Stop)) -> U64
total_load = |routes_by_slot| 0
