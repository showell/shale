## The module's only use is a method on a value of its type, which never
## names the module, so the import is needed no more than it is seen.
import game.Distance
import game.Rider

UnusedMethodOnly :: [].{
	gap = |rider| Rider.ahead(rider).metres()
}
