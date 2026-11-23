# A plant for `phrase_name`: `Clockwork` starts with `Clock` and is another type.
Clock := U32.{
	from_gears : Str -> [Built(Clockwork), Broken]
	from_gears = |gears| Broken
}
