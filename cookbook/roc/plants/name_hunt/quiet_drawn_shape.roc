# Quiet for every check: an effect whose first argument is the surface it
# draws on (a `Frame` or a `Canvas`) is named for what it draws there:
# `frame.circle!(...)` reads as the act.
Draw := [].{
	Frame := [].{}

	circle! : Frame, { x : F32, y : F32, r : F32 } => {}

	panel! : Draw.Frame, Str => {}

	Canvas := [].{}

	rectangle! : Canvas, { w : F32, h : F32 } => {}
}
