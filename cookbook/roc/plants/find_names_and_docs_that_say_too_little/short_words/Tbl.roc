## A plant for `abbreviation`'s short words: a module, a type, a parameter
## and two locals, each short for a word.
Tbl := [].{
	Trk : { stops : List(U64) }

	climb_of : Trk -> U64
	climb_of = |trk| {
		pts = trk.stops
		hgt = List.walk(pts, 0, |most, height| Num.max(most, height))
		hgt
	}
}
