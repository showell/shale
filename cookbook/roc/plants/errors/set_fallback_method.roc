# A plant for errors' `set_fallback`: the same store as a method call.
clear : List(U8), U64 -> List(U8)
clear = |board, s| board.set(s, 0) ?? board
