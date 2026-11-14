## One import of three is never used.
import game.Angle
import game.Polygon
import pf.Stdout

Unused :: [].{
	turn : Angle -> Angle
	turn = |a| a

	say! = |text| Stdout.line!(text)
}
