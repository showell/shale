# Quiet for boundaries' `dead`: an `expect` reaches it, and a member Roc calls
# by name needs no caller.
Scored := [].{
	is_eq = |a, b| a == b
}

only_tested = |x| x + 1

expect only_tested(1) == 2
