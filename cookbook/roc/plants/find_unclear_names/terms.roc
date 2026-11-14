# A plant for `terms`: a header's name, and a name that only starts like it.
Cookie := [].{
	set_cookie_header : U64 -> Str
	set_cookie_header = |value| value.to_str()

	set_cookies : U64 -> Str
	set_cookies = |value| value.to_str()
}
