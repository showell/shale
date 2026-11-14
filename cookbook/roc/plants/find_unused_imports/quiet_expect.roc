## The import is used only in an expect, which still counts.
import game.Distance

QuietExpect :: [].{
	two = 2
}

expect Distance.metres(QuietExpect.two) == Distance.metres(2)
