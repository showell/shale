# Quiet for boundaries' `fixed`: a module a package's main.roc exposes, so
# its members are the package's API.
Geo := [].{
	half : I64, I64 -> I64
	half = |d, x| x // d
}
