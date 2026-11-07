## A point on a grid.
Dot :: { x : I64, y : I64 }.{

	## One step right; only the app `one` uses it.
	shift : Dot -> Dot
	shift = |Dot.({ x, y })| Dot.({ x: x + 1, y })

	## Twice as far out; both apps use it.
	scale : Dot -> Dot
	scale = |Dot.({ x, y })| Dot.({ x: x * 2, y: y * 2 })

	## The origin; only the package's own Grid uses it.
	origin : Dot
	origin = Dot.({ x: 0, y: 0 })

	## Nobody outside uses it.
	hidden : Dot -> I64
	hidden = |Dot.({ x, y })| x + y
}
