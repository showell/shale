# A clean plant for `abbreviation`, including a comment that says dist and a
# local named pts, neither of which is a declared name.
Sky := [].{
	max_visible_cats : U64
	max_visible_cats = 4

	SunPosition : { distance : U64 }

	dispose : U64 -> U64
	dispose = |x| {
		pts = x
		pts
	}

	said : Str
	said = "dist"
}
