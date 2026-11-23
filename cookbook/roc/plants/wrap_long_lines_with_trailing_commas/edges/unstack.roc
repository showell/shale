module [joined, kept, wide]

# Two items, so with tokens=6 the trailing comma goes and the call joins.
joined = f(
	a,
	b,
)

# A comment inside keeps it stacked.
kept = g(
	a, # the first
	b,
)

# Joined, this would pass the width, so it stays stacked.
wide = h(
	"a string long enough that joining the call would pass the width",
	"and another",
)
