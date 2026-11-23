module [joined, blank, block, held, annotated_fn, last_arg, with_rest]

joined = f(
	a,
	b,
)

blank = f(
	a,

	b,
)

block = f(
	|x| {
		y = x
		y
	},
)

held = (
	g(
		a,
		b,
	)
)

annotated_fn : U8,
	{
		a : U8,
		b : U8,
	},
	U8 -> U8
annotated_fn = |x, r, y| x

last_arg = g(
	a,
	b,
	|x| x,
)

with_rest : {
	a : U8,
	b : U8,
}, U8 -> U8
with_rest = |r, y| y
