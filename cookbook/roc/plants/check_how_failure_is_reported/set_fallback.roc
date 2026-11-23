# A plant for errors' `set_fallback`: a store that falls back to the list
# it stores into (FastTrack's Piece.place), so every store copies.
place : List(U8), U64 -> List(U8)
place = |board, s| List.set(board, s, 1) ?? board
