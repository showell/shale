# A plant for hunt_names's `bare_participle`: pure functions each named by
# one past participle, regular and irregular, which read as a `Bool`.
added : List(Str), Str -> List(Str)
added = |names, name| List.append(names, name)

joined = |names| Str.join_with(names, ", ")

chosen : List(Str) -> Str
chosen = |names| List.first(names) ?? ""

# A module type's member, the roc-chat lookup the check was made for.
Users := [].{
	named : List(Str), Str -> Str
	named = |names, name| List.first(names) ?? name

	written : Str -> Str
	written = |text| text
}
