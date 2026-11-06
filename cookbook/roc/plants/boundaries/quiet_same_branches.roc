# Quiet for boundaries' `same_branches`: branches that differ, by a little.
pick = |m| {
	if m.over { m.left } else m.right
}

expect pick({ over: 1, left: 1, right: 2 }) == 1
