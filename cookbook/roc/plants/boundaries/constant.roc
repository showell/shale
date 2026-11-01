# A plant for boundaries' `constant`: a pure function of `{}` alone gives
# the same value every call, a constant.
board : {} -> List(U8)
board = |{}| [1, 2, 3]
