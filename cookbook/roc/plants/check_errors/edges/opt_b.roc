# Edges for errors' `optional_tag`: a mode with no payload names `Maybe`, so
# its uses are spared; an error union names `Absent` the same way.
Answer : [Maybe, Sure]

asked : U64 -> Try(U64, [Absent])

Present : [Present(U64), Nothing(U64)]

Many : [
	Some(U64),
	Option,
	Optional(Str),
]
