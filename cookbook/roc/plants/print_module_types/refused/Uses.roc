## Flips a coin through its import, so `flip` has an outside use.
import Coin

Uses :: [].{

	flipped : Coin -> Coin
	flipped = |c| Coin.flip(c)
}
