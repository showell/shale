module [apply_all, rest_of, sorted]

apply_all = |first_argument, second_argument, third_argument| first_argument(second_argument, third_argument)

rest_of = |list|
	match list {
		[first, second, .. as rest] => [first, second, rest.len(), 1, 2]
		_ => []
	}

sorted : List(k) -> List(k) where [k.is_eq : k, k -> Bool, k.is_lt : k, k -> Bool]
sorted = |list| list
