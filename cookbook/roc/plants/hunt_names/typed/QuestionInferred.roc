## Under the types, `question` judges an unannotated function by what the
## checker infers: `is_ready` gives a number, not a `Bool`.
QuestionInferred := [].{
	is_ready = |n| n + 1

	has_room = |xs| List.len(xs) > 3
}

expect QuestionInferred.is_ready(1) == 2
expect QuestionInferred.has_room([1, 2, 3, 4])
