## Quiet for `command_tense`: events where they are recorded or replayed, a
## plain function's argument and a record's field.
Log := [].{
	Change : [Added, Removed]

	replay : Change -> Str

	store! : { change : Change, line : Str } => {}
}
