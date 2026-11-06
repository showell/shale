platform ""
	requires { step : U8, U8, U8 -> U8 }
	exposes []
	packages {}
	provides { "s": host }
	targets: {}

# The platform's header dictates this function's arguments.
host : U8, U8, U8 -> U8
host = |a, b, c| step(a, b, c)
