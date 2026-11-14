## Each import is used as a tag: one applied, and one a nominal record.
import game.Shape
import game.Point

QuietTag :: [].{
	dot = Shape.Dot(1)

	origin = Point.{ x: 0, y: 0 }
}
