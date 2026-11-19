## A clean plant for `abbreviation`'s longer forms: plurals of plain words
## and of accepted terms (`lines`, `boxes`, `rects`, `ids`), and words that end
## as no plural does (`class`, `bonus`, `alias`).
Shelf := [].{
	Row : { lines : List(Str), boxes : U64, class : Str, bonus : U64, alias : Str, rects : U64, ids : List(U64) }
}
