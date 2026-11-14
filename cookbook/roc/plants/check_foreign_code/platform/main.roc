# A platform's own names are the ones its header dictates.
platform ""
	requires {
		step : U8, U8, U8 -> U8
	}
	exposes []
	packages {}
	provides {
		"roc_step": step_for_host,
	}
	targets: {
		inputs_dir: "targets/",
	}

step_for_host : U8, U8, U8 -> U8
step_for_host = |a, b, c| step(a, b, c)
loose_end : U8, U8, U8 -> U8
loose_end = |a, b, c| a + b + c

expect loose_end(1, 2, 3) == 6
