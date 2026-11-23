Emitted :: [].{
	one_line : I64 -> I64
	one_line = |x| if (x > 1) 2 else 3 # paren_if

	whole_condition : I64 -> I64
	whole_condition = |x|
		if (x > 1) { # paren_if
			2
		} else {
			3
		}

	wrapped : I64 -> I64
	wrapped = |x| ( # wrapped_body
		x + 1
	)

	wrapped_on_one_line : I64 -> Bool
	wrapped_on_one_line = |x| (x > 1) # wrapped_body

	wrapped_value : I64 -> I64
	wrapped_value = |x| {
		y = ( # wrapped_body
			if x > 1 {
				2
			} else {
				3
			}
		)
		(y + 1) # wrapped_body
	}

	wrapped_branch : [A, B] -> I64
	wrapped_branch = |t|
		match t {
			A => ( # wrapped_body
				1 + 2
			)
			B => 3
		}

	nested_else : I64 -> I64
	nested_else = |x|
		if x > 1 { # else_block
			2
		} else {
			if x > 0 {
				1
			} else {
				0
			}
		}
}
