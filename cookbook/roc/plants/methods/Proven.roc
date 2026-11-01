## Under the types (root TypedApp): each call's first argument is the
## module's own type, so the calls are proven, written together with every
## other file's proven calls, and confirmed by one `roc check`: no round.
Proven := [].{
	size : List(U8) -> U64
	size = |bytes| List.len(bytes)

	shout : Str -> Str
	shout = |s| Str.concat(s, "!")
}

expect Proven.size([1]) == 1
expect Proven.shout("a") == "a!"
