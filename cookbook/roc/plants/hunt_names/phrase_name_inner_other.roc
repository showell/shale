# A plant for `phrase_name`: a constructor's first word that gives a bare
# type the file doesn't declare is no constructor.
MdText := [].{
	Width : { left : U64 }

	at_most : U64 -> Budget
	at_most = |n| { left: n }
}
