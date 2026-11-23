# Quiet for every check: a value named as a question is no function that
# asks (a test table's column, `has_got`), and an effect that asks the
# host about live state, giving a `Bool`, has to be effectful.
Table := [].{
	has_got : List(Bool)
	has_got = [Bool.true, Bool.false]

	is_playing! : Str => Bool
}
