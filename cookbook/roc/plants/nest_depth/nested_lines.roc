# A plant for nest_depth: a nest over several lines, whose first line
# (`List.concat(`) says nothing alone; the finding names its outermost
# call and its deepest (`from A to B`: the finding must say `A to B`).
rows : Str -> List(U8)
rows = |s| List.concat( # depth 3 from List.concat to Str.with_ascii_uppercased
	[1],
	Str.to_utf8(
		Str.with_ascii_uppercased(s),
	),
)

# A method call is named by its method.
cut : Str -> Str
cut = |s| s.concat(Str.trim(Str.with_ascii_uppercased(s))) # depth 3 from .concat to Str.with_ascii_uppercased
