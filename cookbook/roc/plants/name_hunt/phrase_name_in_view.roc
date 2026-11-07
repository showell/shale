# A plant for `phrase_name`: a bare phrase in a module with a type of its own.
Scene := U32.{
	in_view : Scene -> U32
	in_view = |Scene.(number)| number
}
