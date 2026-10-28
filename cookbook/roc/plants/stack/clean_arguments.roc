module [two, Row]

# Two bare arguments, a record field typed so, and a type alias are no
# three.
two : U8, U8 -> U8
two = |a, b| a + b

Row : {
	on_pick : U8, U8 -> U8,
}
