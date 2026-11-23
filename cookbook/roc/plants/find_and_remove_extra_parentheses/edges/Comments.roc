## A plant for find_and_remove_extra_parentheses' edges: comments beside the brackets, two
## findings on a line, a unary and a `match` with no span of their own, and
## a body that is a block. Not in the self-test's glob, whose plants mark one
## finding a line: check_goldens.shale holds these to the Shale original.
Comments :: [].{
	wrapped_with_comments : I64 -> I64
	wrapped_with_comments = |x| ( # the opening's comment
		x + 1 # the closing line's comment
	)

	condition_comment : I64 -> I64
	condition_comment = |x|
		if (x > 1) # after the condition
			2
		else # after else
			3

	both_findings : I64 -> I64
	both_findings = |x| (if (x > 1) 2 else 3)

	block_statement : I64 -> I64
	block_statement = |x| {
		(
			x + 1
		)
	}

	else_block_with_comment : I64 -> I64
	else_block_with_comment = |x|
		if x > 1 {
			2
		} else { # the block's comment
			if x > 0 {
				1
			} else {
				0
			}
		}

	unicode_before_the_if : Str -> I64
	unicode_before_the_if = |s| if s == "é" { 1 } else { 2 }

	unary_condition : Bool -> I64
	unary_condition = |b| if (!b) 1 else 2

	unary_branches : Bool -> Bool
	unary_branches = |b|
		if b
			!b
		else
			!(!b)

	match_in_parens : I64 -> I64
	match_in_parens = |x| (
		match x {
			0 => 1
			_ => 2
		}
	)

	nested_wrapped : I64 -> I64
	nested_wrapped = |x| ((x + 1))

	branch_body_block : [A, B] -> I64
	branch_body_block = |t|
		match t {
			A => ({
				y = 1
				y + 1
			})
			B => 3
		}

	declaration_over_lines : I64 -> I64
	declaration_over_lines = |x| {
		y = (
			x
				+ 1
		)
		y
	}

	else_if_braceless : I64 -> I64
	else_if_braceless = |x|
		if x > 1 2 else if x > 0 1 else 0

	else_block_in_else_block : I64 -> I64
	else_block_in_else_block = |x|
		if x > 2 {
			3
		} else {
			if x > 1 {
				2
			} else {
				if x > 0 {
					1
				} else {
					0
				}
			}
		}
}
