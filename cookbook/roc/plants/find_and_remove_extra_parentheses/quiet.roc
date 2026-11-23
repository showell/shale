Quiet :: [].{
	braces : I64 -> I64
	braces = |x|
		if x > 1 {
			2
		} else if x > 0 {
			1
		} else {
			0
		}

	grouped_operand : F64, F64 -> F64
	grouped_operand = |a, b| (a * b) + (b * a)

	grouped_condition : F64 -> Bool
	grouped_condition = |a|
		if (a * 2.0) < 1.0 {
			True
		} else {
			False
		}

	one_line_value : F64 -> F64
	one_line_value = |a| {
		doubled = (a * 2.0)
		doubled + 1.0
	}

	grouped_argument : F64 -> F64
	grouped_argument = |a| grouped_operand((a + 1.0), a)

	pair : I64 -> (I64, I64)
	pair = |x| (x, x + 1)

	statements_in_else : I64 -> I64
	statements_in_else = |x|
		if x > 1 {
			2
		} else {
			y = x + 1
			if y > 0 {
				1
			} else {
				0
			}
		}

	match_in_braces : I64, I64 -> I64
	match_in_braces = |x, y|
		if x > 1 {
			match y {
				0 => 1
				_ => 2
			}
		} else {
			3
		}

	if_on_its_own_line : I64 -> I64
	if_on_its_own_line = |x|
		if x > 1 {
			2
		} else {
			3
		}

	if_after_a_comment : I64 -> I64
	if_after_a_comment = |x| # the comment ends the lambda's line
		if x > 1 {
			2
		} else {
			3
		}

	if_on_its_own_line_inside_a_call : List(I64) -> List(I64)
	if_on_its_own_line_inside_a_call = |numbers|
		numbers.map(
			|x|
				if x > 1 {
					2
				} else {
					3
				},
		)

	if_in_a_lambdas_block : I64 -> I64
	if_in_a_lambdas_block = |x| {
		y = x + 1
		if y > 1 {
			2
		} else {
			3
		}
	}

	if_as_an_argument : I64 -> I64
	if_as_an_argument = |x| Quiet.braces(if x > 1 { 2 } else { 3 })

	if_as_an_operand : I64 -> I64
	if_as_an_operand = |x| 1 + (if x > 1 { 2 } else { 3 })

	if_beside_a_declaration : I64 -> I64
	if_beside_a_declaration = |x| {
		y = if x > 1 { 2 } else { 3 }
		y + 1
	}

	match_hanging_off_a_lambda : I64 -> I64
	match_hanging_off_a_lambda = |x| match x {
		0 => 1
		_ => 2
	}
}
