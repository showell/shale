module [sun_pos]

# The record of types stacks, and the arguments that only name its fields sit
# on one line.
sun_pos : { heading : Angle.Bearing, clock : Clock, cam_focal : F64 } -> Sky.SunPos
sun_pos = |
	{
		heading,
		clock,
		cam_focal,
	},
| {
	heading
}
