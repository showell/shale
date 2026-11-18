# Quiet for every check: the thing named before its preposition, and a
# plural after one, which `singular_list` would leave.
Plan : { routes_by_slot : List(List(Stop)), cost : U64 }

total_load : List(List(Stop)) -> U64
total_load = |routes_by_slot| 0

layer_count : List(Layer) -> U64
layer_count = |on_layers| 0
