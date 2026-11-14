## A grid's first dot, so the package uses Dot itself.
import Dot

Grid :: { first : Dot }.{

	start : Grid
	start = Grid.({ first: Dot.origin })
}
