## Under the types, `singular_list` judges an unannotated parameter by what the
## checker infers: `corner` holds a list.
SingularListParameterInferred := [].{
	size_of = |corner| List.len(corner)
}

expect SingularListParameterInferred.size_of([1]) == 1
