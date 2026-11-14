# A plant for `name_type`: each name below disagrees with the table.
Herd := [].{
	bull_dist : Geom.Along
	bull_dist = Geom.Along.of(1.0)

	Rail : { leg_steps : U32 }

	walk_dist : U64 -> F64
	walk_dist = |n| n.to_f64()

	read_dist! : Str => F64
	read_dist! = |s| s.to_f64()

	frames : Steps
	frames = Steps.of(1)

	in_2_frames : Steps
	in_2_frames = Steps.of(2)
}
