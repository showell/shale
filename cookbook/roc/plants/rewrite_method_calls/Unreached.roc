## Reached by no root (`TypedApp` and `SecondApp` neither import it): the
## types cannot judge it, and the run stops, naming both roots.
Unreached := [].{
	size : List(U8) -> U64
	size = |bytes| List.len(bytes)
}
