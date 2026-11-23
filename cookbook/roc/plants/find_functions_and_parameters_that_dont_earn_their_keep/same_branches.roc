# A plant for boundaries' `same_branches`: the shape of `end_game` in
# rocco-engine's pong, with and without braces.
step = |m| {
	if m.over { m } else m
}

other = |m| if (m.over) m.score else m.score

expect step({ over: 1 }) == other({ over: 1 })
