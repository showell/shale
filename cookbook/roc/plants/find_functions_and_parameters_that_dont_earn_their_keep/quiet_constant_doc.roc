# Quiet for boundaries' `constant`: a `##` doc gives the reason a function
# of `{}` stays one.

## A function, not a constant: the platform calls it once a frame.
documented_board : {} -> List(U8)
documented_board = |{}| [1, 2, 3]
