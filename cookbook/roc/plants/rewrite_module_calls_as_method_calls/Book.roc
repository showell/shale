## LastSegment's helper: a type module named for the last segment of
## `Shelf.Book`, whose function takes that type, as fasttrack's Game.roc
## takes `Type.Game`.
import Shelf

Book :: [].{
	heading : Shelf.Book -> Str
	heading = |b| b.title().concat(":")
}
