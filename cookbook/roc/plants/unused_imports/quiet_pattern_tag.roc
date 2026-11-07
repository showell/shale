## The import is used only as a qualified tag in a pattern, whose qualifier
## the tree drops.
import game.Color

QuietPatternTag :: [].{
	name = |c| match c {
		Color.Red => "red"
		_ => "other"
	}
}
