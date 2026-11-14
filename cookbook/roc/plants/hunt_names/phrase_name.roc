# A plant for hunt_names's `phrase_name`: each reads as a yes or no and hides
# what it gives.
in_view : U64 -> List(U64)
in_view = |view| [view]

over_gap : U64, U64 -> Str
over_gap = |gap, end| (gap + end).to_str()

# One word is a phrase too.
behind : List(Str), Str -> List(Str)
behind = |shapes, backdrop| [backdrop].concat(shapes)
