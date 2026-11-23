# Plants for the walk's flow: a write in one branch and a read in the
# other, which is clean; a read after the branches, found; a loop that
# takes the same list on every pass, found; a `var` written each pass,
# clean; and a name bound afresh, clean.
branches_apart : List(U64), Bool -> U64
branches_apart = |xs, which| {
	if which {
		ys = xs.set(0, 1) ?? xs
		ys.len()
	} else {
		xs.get(0) ?? crash "branches_apart: not empty"
	}
}

read_after_branches : List(U64), Bool -> U64
read_after_branches = |xs, which| {
	ys = if which {
		xs.set(0, 1) ?? xs
	} else {
		[]
	}
	ys.len() + (xs.get(0) ?? crash "read_after_branches: not empty") # after_consume
}

taken_each_pass : List(U64), List(U64) -> List(List(U64))
taken_each_pass = |xs, places| {
	var $out = []
	for k in places {
		$out = $out.append(xs.set(k, 0) ?? crash "taken_each_pass: in range") # after_consume
	}
	$out
}

written_each_pass : List(U64), List(U64) -> List(U64)
written_each_pass = |xs, places| {
	var $acc = xs
	for k in places {
		$acc = $acc.set(k, 0) ?? crash "written_each_pass: in range"
	}
	$acc
}

two_strings : Str -> (Str, Str)
two_strings = |name| {
	trimmed = name.trim()
	(trimmed, name.concat("!")) # after_consume
}
