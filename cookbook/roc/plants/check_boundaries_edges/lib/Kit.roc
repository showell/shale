# Edges for check_boundaries: a member under `lib/` is a library's API, so
# nothing calling it is `unused_api`, and a top-level function there is
# private to its file.
Kit := [].{
	unused_tool = |x| x

	spaced = |n, x| n + x
}

kit_helper = |x| x
