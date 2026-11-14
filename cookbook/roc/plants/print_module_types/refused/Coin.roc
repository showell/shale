## A coin. Uses.roc flips it, and Broken.roc spins it, but roc-ast refuses
## Broken.roc, so no use of `spin` can be counted.
Coin :: [Heads, Tails].{

	flip : Coin -> Coin
	flip = |c| c

	spin : Coin -> Coin
	spin = |c| c
}
