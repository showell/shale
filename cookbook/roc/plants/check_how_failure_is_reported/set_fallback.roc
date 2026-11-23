# A plant for errors' `set_fallback`: a store that falls back to the list
# it stores into (FastTrack's Piece.place), an answer no store means.
place : List(U8), U64 -> List(U8)
place = |board, s| List.set(board, s, 1) ?? board
