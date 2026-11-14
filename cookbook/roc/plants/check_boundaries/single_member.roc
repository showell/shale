# A plant for boundaries' `single`: a module member reached once, by its
# qualified name, from another member.
Words := [].{
	first_of : List(Str) -> Str
	first_of = |ws| ws.first() ?? ""

	opening : List(Str) -> Str
	opening = |ws| {
		w = Words.first_of(ws)
		Str.concat(w, ":")
	}
}
