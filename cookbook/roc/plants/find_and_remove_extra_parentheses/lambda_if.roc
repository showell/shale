LambdaIf :: [].{
	hanging : I64 -> I64
	hanging = |x| if x > 1 { # lambda_if
		2
	} else {
		3
	}

	on_one_line : I64 -> I64
	on_one_line = |x| if x > 1 { 2 } else { 3 } # lambda_if

	else_if_chain : I64 -> I64
	else_if_chain = |x| if x > 1 { # lambda_if
		2
	} else if x > 0 {
		1
	} else {
		0
	}

	stacked_arguments : { first : I64, second : I64 } -> I64
	stacked_arguments = |
		{ first, second },
	| if first > second { # lambda_if
		first
	} else {
		second
	}

	inside_a_call : List(I64) -> List(I64)
	inside_a_call = |numbers|
		numbers.map(
			|x| if x > 1 { # lambda_if
				2
			} else {
				3
			},
		)

	lambda_in_a_lambda : I64 -> (I64 -> I64)
	lambda_in_a_lambda = |x|
		|y| if x > y { # lambda_if
			x
		} else {
			y
		}

	braceless_first : I64 -> I64
	braceless_first = |x| if x > 1 2 else 3 # paren_if
}
