## A clean plant for `abbreviation`'s longer forms: plurals of plain words
## and of accepted terms (`lines`, `boxes`, `rects`, `ids`), plurals made
## with `es` or `ies` whose stem would read short without its `e` (`shapes`,
## `metres`, `pieces`, `tries`, `Leaves`, `SafariFences`), and words that end as
## no plural does (`class`, `bonus`, `alias`).
Shelf := [].{
	Row : { lines : List(Str), boxes : U64, class : Str, bonus : U64, alias : Str, rects : U64, ids : List(U64) }

	Leaves : { shapes : U64, metres : U64, frames : U64, places : U64, pieces : U64, angles : U64 }

	SafariFences : { tries : U64 }

	count_tries = |{ tries }| {
		var $tries = tries
		$tries = $tries + 1
		$tries
	}
}
