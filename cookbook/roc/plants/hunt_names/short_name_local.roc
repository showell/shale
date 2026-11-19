# A plant for hunt_names's `short_name`: one-letter locals, parameters, match
# bindings, a destructured record's fields, and a lambda that is no loop's
# body, each read away from what it holds.
area = |w, h| {
	p = w * h
	{ k, y: q } = { k: 1, y: 2 }
	(t, u) = (p, q)
	ranked = [p].map(|e| {
		e + k
	})
	labels = Str.join_with(["one"], " ").walk_utf8(0, |e, l| e + l)
	shifted = add_two(|v| v + t)
	match ranked {
		[a, .. as rest] => a + u + labels + shifted + rest.len()
		_ => 0
	}
}

growth = |start| {
	var $n = start
	$n = $n + 1
	$n
}
