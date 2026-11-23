## An edge for errors' run under the types: a module no root reaches is
## judged without them, named after the findings, and fails the run.
Unreached := [].{
	count_of : List(U64), U64 -> U64
	count_of = |counts, i| List.get(counts, i) ?? 0
}
