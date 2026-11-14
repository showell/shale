## Each import is used only in a type: a qualified type, the module's own
## type, a type argument, and a type in a type declaration.
import game.Distance
import game.Angle
import game.Spot
import game.Pose

QuietAnnotation :: [].{
	Placed : { at : Pose.Pose }

	far : Distance -> Angle.Angle
	far = |d| crash "never"

	spots : List(Spot) -> U64
	spots = |xs| xs.len()
}
