# A clean plant for `abbreviation`, including a comment that says dist and a
# string that says pts, neither of which is a name.
Sky := [].{
	max_visible_cats : U64
	max_visible_cats = 4

	SunPosition : { distance : U64 }

	dispose : U64 -> U64
	dispose = |x| {
		kept = x
		kept
	}

	said : Str
	said = "dist pts"
}
