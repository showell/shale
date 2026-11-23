# Edges for find_functions_and_parameters_that_dont_earn_their_keep' `same_branches`: branches alike over several
# lines, a long branch cut at 40 bytes, blocks of two statements kept whole,
# an `else if` chain, and branches that differ only in their spacing.
wide = |c, a, b|
	if c {
		Str.concat(
			a,
			b,
		)
	} else {
		Str.concat(
			a,
			b,
		)
	}

long = |c, record| if c record.first_field_of_many.second_field_of_many else record.first_field_of_many.second_field_of_many

blocks = |c, a|
	if c {
		x = a
		x
	} else {
		x = a
		x
	}

chain = |c, d, a| if c a else if d a else a

spaced = |c, a, b| if c Str.concat(a,b) else Str.concat(a, b)

expect wide(1, 2, 3) == long(1, 2) and blocks(1, 2) == chain(1, 2, 3) and spaced(1, 2, 3) == 1
