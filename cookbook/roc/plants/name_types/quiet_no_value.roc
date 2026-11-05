# A clean plant for `no_value`.
Gaze := [].{
	Looks : [Found, Lost]

	no_cat : Bool
	no_cat = Bool.False

	no_answer : [Yes, No]
	no_answer = No

	no_look : Looks
	no_look = Lost

	none_got : List(F64)
	none_got = []

	none : Gaze
	none = Gaze.start

	no_match : Str -> U64
	no_match = |s| s.count()

	no_guess = 3

	know_id : U64
	know_id = 0
}
