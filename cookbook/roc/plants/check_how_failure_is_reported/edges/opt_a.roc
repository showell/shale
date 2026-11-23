# Edges for errors' `optional_tag`: `None` again in another file is found
# again, and `Maybe` here is named by a type in opt_b.roc.
blank = None

said = |x|
	match x {
		Maybe => 1
		Absent => 2
		_ => 3
	}
