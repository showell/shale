platform ""
	requires {
		step : U8 -> U8
	}
	exposes []
	packages {}
	provides { "s": host }
	targets: {
		x64musl: {
			inputs: ["crt1.o", "host.o", app, "libc.a", "libm.a", "libz.a"],
			output: Archive,
		},
		wasm32: {
			inputs: [app],
			output: Archive,
		},
	}

# `roc fmt` lays out the `targets:` section by itself, whatever its commas say.
host : U8 -> U8
host = |a| step(a)
