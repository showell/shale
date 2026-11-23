# Edges for errors' `error_alias`: a loop of aliases, an empty union, a
# nominal union, nested modules, a result holding two errors, and the
# annotations that are not judged.
Loop : Round

Round : Loop

spin : U64 -> Try(U64, Loop)

Empty : []

nothing : U64 -> Try(U64, Empty)

Wrapped := [Torn, Lost]

unwrapped : U64 -> Try(U64, Wrapped)

Outer := [].{
	Inner := [].{
		Problem : [Bent, Broken]
	}

	inner_read : U64 -> Try(U64, Outer.Inner.Problem)
}

Problem : [Late, Early]

both : U64 -> (Try(U64, Problem), List(Try(Str, Problem)))

listed : U64 -> List(U8)

kept : Try(U64, Problem)

short : U64 -> Try(U64)

builtin : U64 -> Try(U64, U8)

applied : U64 -> Try(U64, List(Problem))

outer = |x| {
	local_read : U64 -> Try(U64, Problem)
	local_read = |y| Ok(y)
	local_read(x)
}
