## Every float here is in a helper only tests use.
QuietHelpers :: [].{

	member : U8 -> U8
	member = |n| n
}

in_a_test : F64
in_a_test = 1.5

through_a_helper : F64
through_a_helper = 1.5

maker : U8 -> F64
maker = |_n| through_a_helper

expect in_a_test == 1.5
expect maker(1) == 1.5
