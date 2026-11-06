# Quiet for every check: constructors, conversions and changed copies read as
# what they are.
Clock := U32.{
	at_step : U32 -> Clock
	at_step = |number| Clock.(number)

	from_ratio : U32 -> Try(Clock, [OutOfRange])
	from_ratio = |number| Ok(Clock.(number))

	of : U32 -> Clock
	of = |number| Clock.(number)

	from_hex_rgb : U32 -> Clock.Rgb
	from_hex_rgb = |number| { red: number }

	from_start : Clock -> U32
	from_start = |Clock.(number)| number

	to_seconds : Clock, U32 -> U32
	to_seconds = |Clock.(number), rate| number * rate

	into_list : Clock -> List(U32)
	into_list = |Clock.(number)| [number]

	with_lean : Clock.Motion(others), U32 -> Clock.Motion(others)
	with_lean = |motion, lean| { ..motion, lean }

	without_lean : List(U32), U32 -> List(U32)
	without_lean = |lean, gone| lean.drop_if(|l| l == gone)

	Spot := U32.{
		at_seam : U32 -> Clock.Spot
		at_seam = |number| Clock.Spot.(number)
	}
}
