## The module is never named, but a name each line exposes is used.
import pf.Stdout exposing [line!]
import game.Shapes exposing [Shape]

QuietExposing :: [].{
	say! = |text| line!(text)

	none : List(Shape)
	none = []
}
