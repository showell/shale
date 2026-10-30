## A module's own call stays as it is declared.
Own := [].{
	double : U64 -> U64
	double = |n| n * 2

	quadruple : U64 -> U64
	quadruple = |n| Own.double(Own.double(n))
}

expect Own.quadruple(1) == 4
