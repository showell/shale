## A call whose first argument is a number literal stays: `U64.to_str(5)`
## fixes the literal's type, and `(5).to_str()` leaves it to default (to
## `Dec`, "5.0"). Both check; only the expect tells them apart. The file's
## other call is still turned.
Literal := [].{
	five : Str
	five = U64.to_str(5)

	shout : Str -> Str
	shout = |name| Str.concat(name, "!")
}

expect Literal.five == "5"
expect Literal.shout("a") == "a!"
