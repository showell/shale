## The alias is never used, though the module's own name is: without the
## alias, `Distance` names nothing this import brings.
import game.Distance as D

UnusedAlias :: [].{
	far = Distance.metres(5)
}
