## Calls a method named as one of Ride's members on an inner function's
## parameter, whose type the walk can't know, so the use counts by the
## method's name alone.
Log :: [].{
	lines_of : List(Str) -> List(Str)
	lines_of = |things| things.map(|thing| thing.noted())
}
