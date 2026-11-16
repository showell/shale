## The package that the app reaches only through `outer`, its own package.
Inner := [].{
	shout : Str -> Str
	shout = |text| text.concat("!")
}
