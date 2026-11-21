# Edges for errors' `optional_tag`: uses in expressions and patterns, each
# tag once a file however often it is used.
pick = |x|
	if x > 0 {
		Just(x)
	} else {
		None
	}

again = |x|
	match x {
		Just(y) => y
		None => 0
	}

maybe_one = Maybe
