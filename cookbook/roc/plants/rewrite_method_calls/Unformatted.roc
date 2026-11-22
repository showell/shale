## A file `roc fmt` would lay out otherwise (the blank lines below its two
## declarations): its calls turn, and it is not formatted after, so the
## rewrite changes nothing but the calls.
Unformatted := [].{
	size : List(U8) -> U64
	size = |bytes| List.len(bytes)



	shout : Str -> Str
	shout = |s| Str.with_ascii_uppercased(s)
}

expect Unformatted.size([1]) == 1
expect Unformatted.shout("a") == "A"
