## Spins a coin, which is `spin`'s only outside use, but the annotation
## with no type below makes roc-ast refuse the file.
import Coin

Broken :: [].{

	spun : Coin -> Coin
	spun = |c| Coin.spin(c)

	bad : = |
}
