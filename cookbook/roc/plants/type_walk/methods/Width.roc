## A width, with members named as Span's are, and a record of both.
import Span

Width :: F64.{

	plus : Width, Width -> Width
	plus = |Width.(a), Width.(b)| Width.(a + b)

	minus : Width, Width -> Width
	minus = |Width.(a), Width.(b)| Width.(a - b)

	is_gt : Width, Width -> Bool
	is_gt = |Width.(a), Width.(b)| a > b

	## A span and a width together.
	Frame : { span : Span, width : Width }

	## Another name for a width.
	Across : Width

	## Another name for a span.
	Along : Span
}
