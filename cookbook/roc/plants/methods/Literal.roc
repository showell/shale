## A call whose first argument is a constant numeric expression stays:
## `U64.to_str(5)` fixes the literal's type, and `(5).to_str()` leaves it
## to default (to `Dec`, "5.0"); so does `(2 + 3).to_str()`. Both check;
## only the expects tell them apart. A sum with a name in it (`n + 1`, n a
## U64) has its type, and the file's other calls are still turned.
Literal := [].{
	five : Str
	five = U64.to_str(5)

	sum : Str
	sum = U64.to_str(2 + 3)

	next : U64 -> Str
	next = |n| U64.to_str(n + 1)

	shout : Str -> Str
	shout = |name| Str.concat(name, "!")
}

expect Literal.five == "5"
expect Literal.sum == "5"
expect Literal.next(4) == "5"
expect Literal.shout("a") == "a!"
