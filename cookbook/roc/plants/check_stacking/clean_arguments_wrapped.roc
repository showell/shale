module [Row, picked, two]

# Two bare arguments wrapped, a stacked record argument, and a stacked
# record type's fields are no three.
two : U8,
(U8 -> U8) -> Try(
	U8, # the number, through the function
	[Oops],
)
two = |a, f| Ok(f(a))

picked : {
	first : U8,
	second : U8,
	third : U8,
} -> U8
picked = |{ first, second, third }| first + second + third

Row : {
	left : U8,
	on_pick : U8, U8 -> U8,
}
