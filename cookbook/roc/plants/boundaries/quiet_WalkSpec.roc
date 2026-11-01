# Quiet for boundaries: a spec app (a name ending `<Module>Spec.roc`), a
# test file, whose functions are never judged though `shown` has one
# caller and `graded` is passed `0` both times. Its calls are no
# production caller's: `Walk.count_from` started at `3` hides nothing in
# fixed_spec.roc, and `Shade.stops` (quiet_fixed_spec_only.roc) has none.
shown : U64 -> Str
shown = |n| n.to_str()

graded : Str, U64 -> Str
graded = |name, want| Str.concat(name, want.to_str())

main! = |_args| {
	a = Walk.count_from([1, 2, 3], 3)
	b = graded("whole", 0)
	c = graded("tail", 0)
	d = Shade.stops(8405024, 2)
	e = Shade.stops(8405024, 3)
	s = shown(a)
	Ok({})
}
