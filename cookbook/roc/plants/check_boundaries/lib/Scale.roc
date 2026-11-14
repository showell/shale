# Quiet for boundaries' `fixed`: a member of a module under `lib/` is a
# library's API, so its parameter stays though this tree's callers all pass
# `2`; quiet_fixed_lib_caller.roc holds the calls.
Scale := [].{
	times : I64, I64 -> I64
	times = |k, x| k * x
}
