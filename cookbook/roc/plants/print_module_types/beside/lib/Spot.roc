## A place on a line.
Spot :: { at : I64 }.{

	## One step on; only the app `walker`, no widget, uses it.
	step : Spot -> Spot
	step = |Spot.({ at })| Spot.({ at: at + 1 })

	## The start; only the widget uses it.
	home : Spot
	home = Spot.({ at: 0 })
}
