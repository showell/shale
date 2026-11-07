## Units of measure.
Unit :: [].{

	## A distance in metres; two steps from Ride, so printed by name alone
	## past `full_depth=1`.
	Metres :: F64.{

		zero : Metres
		zero = 0.0

		plus : Metres, Metres -> Metres
		plus = |a, b| a + b
	}
}
