## Under the types, `phrase_name` judges an unannotated function by what the
## checker infers: `in_view` gives a number.
PhraseNameInferred := [].{
	in_view = |n| n + 1
}

expect PhraseNameInferred.in_view(1) == 2
