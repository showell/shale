## A namespace module that builds `Parts.Lamp` and declares no second type.
Lamp := [].{
	bright : U64 -> U64
	bright = |n| n + 1
}
