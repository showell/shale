# Quiet for boundaries' `single`: the public name a twin holds to its Zig
# (quiet_single_twin.roc, a test file, reaches it).
Twinned := [].{
	upper : Str -> Str
	upper = |s| Str.trim(s)

	loud : Str -> Str
	loud = |s| {
		u = Twinned.upper(s)
		Str.concat(u, "!")
	}
}
