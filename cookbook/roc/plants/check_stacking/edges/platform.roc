platform ""
	requires { main! : U8, U8, U8 => {} }
	exposes []
	packages {}
	provides { "host_main": host_main, "step": step }
	targets: {
		x64musl: {
			inputs: ["crt1-with-a-long-name-to-pass-the-width.o", "host-with-a-long-name.o", app, "libc.a"],
			output: Archive,
		},
	}

step : U8, U8, U8 -> U8
step = |a, b, c| a

host_main : U8, U8, U8 => {}
host_main = |a, b, c| main!(a, b, c)
