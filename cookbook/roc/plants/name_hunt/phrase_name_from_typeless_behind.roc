# A plant for `phrase_name`: with no type of its own, `from_behind` still has no
# noun.
Shapes :: [].{
	from_behind : Str -> U32
	from_behind = |text| text.count_utf8_bytes()
}
