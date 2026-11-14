# A plant for hunt_names's `noun_effect`: a shape's name, but the first
# argument is no surface drawn on (a writer, not a `Frame` or `Canvas`):
# `line!` reads as a value.
Out := [].{
	line! : Writer, Str => {}

	circle! : Str, F32 => {}
}
