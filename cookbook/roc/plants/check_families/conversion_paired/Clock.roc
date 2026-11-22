## A plant for unpaired_conversion: `at_step` builds a `Clock` and has no
## `to_at_step`, so it is a constructor, not half of a pair.
Clock :: U64.{
	at_step : U64 -> Clock
	at_step = |step| Clock.(step)

	step : Clock -> U64
	step = |Clock.(at)| at
}
