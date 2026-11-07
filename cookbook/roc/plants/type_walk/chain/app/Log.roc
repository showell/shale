## Calls a method named as one of Ride's members on a value whose type the
## walk can't know, so the use counts by the method's name alone.
Log :: [].{
	line_of : Str -> Str
	line_of = |thing| thing.noted()
}
