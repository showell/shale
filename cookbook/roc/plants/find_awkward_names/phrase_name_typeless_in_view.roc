# A plant for `phrase_name`: having no type of its own passes only `from_*`.
Scene :: [].{
	in_view : Str -> U32
	in_view = |view| view.count_utf8_bytes()
}
