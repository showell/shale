# A plant for `phrase_name`: a constructor's first word that gives another
# module's type is no constructor.
Camera := {}.{
	at : Point -> Screen.Point
	at = |point| Screen.project(point)
}
