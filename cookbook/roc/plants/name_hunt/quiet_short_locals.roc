# Quiet for `short_name`: one-letter locals and lambda parameters are read
# beside where they are defined.
area = |w, h| {
	p = w * h
	List.map([p], |x| x + 1)
}
