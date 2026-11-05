module [
	annotated,
	block,
	commented,
	holding,
	literals,
	parens,
	six,
	spaced,
	wide,
]

# Six tokens and three items.
six = f(
	a,
	b,
	c,
	d,
	e,
	g,
)

# A comment can't join a line.
commented = f(
	a, # the first
	b,
)

# Nor can a block's statements.
block = f(
	a,
	|x| {
		y = x
		y
	},
)

# The line would be past the hundredth column.
wide = f(
	"a very long string that runs on and on and on, well past the end of a line",
	"and a second string to take it over",
)

# A pair around a group that stays stacked can't join.
holding = f(
	a,
	{
		b: 1,
		c: 2,
		d: 3,
	},
)

# Joining this call would bring the parentheses around it onto one wide line.
parens = (
	aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa / (
		bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb * g(
			c,
			d,
		)
	)
) * eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee

# A blank line is the writer's.
spaced = [
	a,

	b,
]

# Six literals may stand either way.
literals = [
	1,
	2,
	3,
	4,
	5,
	6,
]

# With the record on one line the annotation is past the hundredth column.
annotated : List(Aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa),
{
	from : Bbbbbbbbbbbbbbbbbbbbbbbbbbb,
	to : Cccccccccccccccccccccc,
} -> List(Ddddddddd)
annotated = |a, _| a
