## Under the types, `singular_list` judges an unannotated function by what the
## checker infers: `triangle` gives a list.
SingularListInferred := [].{
	triangle = |n| [n + 1]
}

expect SingularListInferred.triangle(1) == [2]
