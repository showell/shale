# Quiet for every check: locals are no findings, only top-level and module
# member definitions are. Each local here would fire at the top level.
total = |xs| {
	item = xs
	data = item
	saved! = |_| {}
	links! = |_| {}
	line_count : Str
	line_count = "3"
	is_full : Str
	is_full = "x"
	data
}
