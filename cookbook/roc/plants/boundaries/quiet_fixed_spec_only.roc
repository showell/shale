# Quiet for boundaries' `fixed`: a function only quiet_WalkSpec.roc, a test
# file, calls, the same colour each time, has no production call.
Shade := [].{
	stops : U64, U64 -> List(U64)
	stops = |colour, n| List.repeat(colour, n)
}
