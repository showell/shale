## Quiet under the types for `singular_list`: the checker shows a list, and the
## name is plural.
QuietSingularListInferred := [].{
	triangles = |n| [n + 1]
}

expect QuietSingularListInferred.triangles(1) == [2]
