## A length along a line. Width has members of the same names, so a method
## call counts for Span only when its receiver is a Span, or can't be typed.
Span :: F64.{

	of : F64 -> Span
	of = |n| Span.(n)

	zero : Span
	zero = Span.(0.0)

	plus : Span, Span -> Span
	plus = |Span.(a), Span.(b)| Span.(a + b)

	minus : Span, Span -> Span
	minus = |Span.(a), Span.(b)| Span.(a - b)

	scaled : Span, F64 -> Span
	scaled = |Span.(a), factor| Span.(a * factor)

	halved : Span -> Span
	halved = |Span.(a)| Span.(a / 2.0)

	doubled : Span -> Span
	doubled = |Span.(a)| Span.(a * 2.0)

	flipped : Span -> Span
	flipped = |Span.(a)| Span.(0.0 - a)

	is_lt : Span, Span -> Bool
	is_lt = |Span.(a), Span.(b)| a < b

	is_lte : Span, Span -> Bool
	is_lte = |Span.(a), Span.(b)| a <= b

	is_gt : Span, Span -> Bool
	is_gt = |Span.(a), Span.(b)| a > b

	is_gte : Span, Span -> Bool
	is_gte = |Span.(a), Span.(b)| a >= b

	is_eq : _
}
