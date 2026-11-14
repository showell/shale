## Where a thing stands on the screen. Every sibling's closure reaches it,
## so a family walk prints it once.
Spot :: { x : F64, y : F64 }.{

	## The spot's height above the ground line.
	height : Spot -> F64
	height = |s| s.y
}
