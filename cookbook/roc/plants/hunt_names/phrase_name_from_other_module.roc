# A plant for `phrase_name`: another module's type inside a union is no conversion
# to this one's.
Camera := {}.{
	from_path : Str -> [Found(Screen.Point), Missing]
	from_path = |path| Missing
}
