## What a test shares with anything else stays a finding.
Helpers :: [].{

	member : U8 -> U8
	member = |n| {
		_ = used_by_a_member
		_ = middle
		_ = { in_a_short_record, n }
		n
	}

	member_tests_use : F64
	member_tests_use = 1.5
}

used_by_a_member : F64
used_by_a_member = 1.5

used_by_a_definition : F64
used_by_a_definition = 1.5

not_a_test : U8
not_a_test = {
	_ = used_by_a_definition
	0
}

reached_from_production : F64
reached_from_production = 1.5

middle : U8
middle = {
	_ = reached_from_production
	0
}

in_a_short_record : F64
in_a_short_record = 1.5

used_by_nothing : F64
used_by_nothing = 1.5

only_itself : U8 -> F64
only_itself = |n| only_itself(n)

expect used_by_a_member == 1.5
expect Helpers.member_tests_use == 1.5
