NoLastNewline :: [].{
	f : I64 -> I64
	f = |x| (x + 1)

	g : I64 -> I64
	g = |x| if x > 1 { 2 } else { 3 }
}