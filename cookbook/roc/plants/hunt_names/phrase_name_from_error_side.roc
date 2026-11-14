# A plant for `phrase_name`: a `Try` gives its success, and an error that carries
# the module's type is no conversion to it.
Clock := U32.{
	from_text : Str -> Try(Str, [NotAClock(Clock)])
	from_text = |text| Ok(text)
}
