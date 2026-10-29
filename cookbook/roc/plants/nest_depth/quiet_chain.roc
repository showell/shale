# Quiet for nest_depth: a method chain and a pipe read left to right.
chained : Str -> U64
chained = |s| s.with_ascii_uppercased().trim().to_utf8().len()

piped : Str -> U64
piped = |s|
	s
	|> Str.with_ascii_uppercased
	|> Str.trim
	|> Str.to_utf8
	|> List.len

# A named middle step.
stepped : Str -> Str
stepped = |s| {
	upper = Str.trim(Str.with_ascii_uppercased(s))
	Str.concat(upper, "!")
}
