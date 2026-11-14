# A clean plant for `name_type`: each name below agrees with the table, or
# is one the table doesn't judge.
Herd := [].{
	road_dist : Geom.Distance
	road_dist = Geom.Distance.of(1.0)

	rest_steps : Steps
	rest_steps = Steps.of(1)

	from_dist : Distance -> Speed
	from_dist = |d| d.to_speed()

	after_two_steps : Herd
	after_two_steps = Herd.start

	is_dist : Herd -> Bool
	is_dist = |_| Bool.True

	inferred_dist : _
	inferred_dist = 1

	other_dist = 3

	plain_length : F64
	plain_length = 1.0

	local_names = |_| {
		dist : U64
		dist = 1
		dist
	}
}
