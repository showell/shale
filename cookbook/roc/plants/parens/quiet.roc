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
}
