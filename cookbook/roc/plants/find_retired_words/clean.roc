# A clean plant for vocabulary: words that only look like a retired one pass,
# such as a detour, a stilted line or a tourist, and so do strings.
Ride := { lean : F64 }.{
	detour_length : F64
	detour_length = 0.5

	said : Str
	said = "# the tilt of the tour, in a string"

	long_said : Str
	long_said =
		\\ # a tilt in a multiline string
		\\ tour
}
