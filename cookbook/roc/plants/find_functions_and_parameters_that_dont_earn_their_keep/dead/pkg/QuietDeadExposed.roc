# Quiet for boundaries' `dead`: an exposed member an `expect` reaches is neither
# dead nor unused, and `fixed` leaves its parameter alone.
QuietDeadExposed := [].{
	tested_only = |x| x
}

expect QuietDeadExposed.tested_only(1) == 1
