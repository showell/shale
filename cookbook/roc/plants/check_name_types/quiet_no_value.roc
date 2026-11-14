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

	no_match_words : Str
	no_match_words = "The name and the password don't match."

	no_channel_words : Str
	no_channel_words = "This chat has no ${channel} channel."

	know_id : U64
	know_id = 0
}
