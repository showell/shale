# Quiet for every check: a constructor that gives a type declared inside the
# module's own type, written bare, gives its own module's type.
MdText := [].{
	Budget : { left : U64 }

	at_most : U64 -> Budget
	at_most = |number| { left: number }
}
