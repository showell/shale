# A plant for errors' `set_fallback`: the list a record's field, stored
# into and fallen back to.
mark : { cells : List(U8), turn : U64 }, U64 -> List(U8)
mark = |game, s| List.set(game.cells, s, 1) ?? game.cells
