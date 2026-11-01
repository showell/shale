# Quiet for errors' `set_fallback`: a store that crashes on the impossible,
# as the guide writes it, and one whose fallback is another list.
place : List(U8), U64 -> List(U8)
place = |board, s| List.set(board, s, 1) ?? crash "place: a square past the board"

mark : { cells : List(U8), blank : List(U8) }, U64 -> List(U8)
mark = |game, s| List.set(game.cells, s, 1) ?? game.blank
