# Edges for find_functions_and_parameters_that_dont_earn_their_keep' owners: a type inside a type, a name declared
# twice, a record's shorthand, a method Roc calls by name, and two types
# sharing a member's name, which a method call cannot tell apart.
Outer := [].{
	Inner := [].{
		deep = |x| x
	}

	shallow = |x| Inner.deep(x)

	is_eq = |a, b| a == b

	label = |{}| "outer"
}

Other := [].{
	label = |{}| "other"

	use_label = |o| o.label()
}

twice = |x| x

twice = |x| x

bundle = |x| { twice, x }

lonely = |x| Outer.shallow(x)
