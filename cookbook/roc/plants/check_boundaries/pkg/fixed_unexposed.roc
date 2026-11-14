# A plant for boundaries' `fixed`: a module in a package that its main.roc
# does not expose is the package's own, and is judged.
Hidden := [].{
	part : I64, I64 -> I64
	part = |d, x| x // d
}
