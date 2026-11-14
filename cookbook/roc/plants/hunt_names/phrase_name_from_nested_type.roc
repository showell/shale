# A plant for `phrase_name`: `Hex` declares a type, `Digit`, so `from_text` has one
# to take or give.
Hex :: [].{
	Digit := U8

	from_text : Str -> U32
	from_text = |text| text.count_utf8_bytes()
}
