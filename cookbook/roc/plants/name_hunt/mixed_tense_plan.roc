## A plant for name_hunt's `mixed_tense`: a plan of steps with payloads, a
## declared type, handed in as a list.
Plan := [].{
	Step : [Remove(Str), Written(Str, List(U8))]

	apply_steps! : List(Step) => {}
}
