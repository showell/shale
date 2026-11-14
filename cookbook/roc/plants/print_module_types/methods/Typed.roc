## Calls Span's members on receivers typed as Span, each member through one
## rule: a record field of a parameter (`plus`), a local bound from a
## qualified call (`scaled`), a qualified constant (`halved`), what a method
## gives (`doubled`), a constant of this file (`flipped`), an operator on
## parameters whose type is an alias of Span (`is_lt`), and an operator whose
## right side alone is typed (`is_gte`).
import Span
import Width

Typed :: [].{

	longer : Width.Frame -> Span
	longer = |f| f.span.plus(f.span)

	twice : F64 -> Span
	twice = |n| {
		s = Span.of(n)
		s.scaled(2.0)
	}

	half : Span
	half = Span.zero.halved()

	whole : Span
	whole = half.flipped()

	again : Span -> Span
	again = |_s| Span.zero.halved().doubled()

	shorter : Width.Along, Width.Along -> Bool
	shorter = |a, b| a < b

	at_least : List(Str) -> List(Bool)
	at_least = |items| items.map(|item| item >= Span.zero)
}
