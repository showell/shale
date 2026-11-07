# A plant for `phrase_name`: with no type of its own, a bare `from` still has no
# noun.
Shapes :: [].{
	from : Str -> U32
	from = |text| text.count_utf8_bytes()
}
