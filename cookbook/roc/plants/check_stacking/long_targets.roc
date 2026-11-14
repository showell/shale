platform ""
	requires {
		step : U8 -> U8
	}
	exposes []
	packages {}
	provides { "s": host }
	targets: {
		x64musl: {
			inputs: ["crt1-with-a-long-name-to-pass-the-width.o", "host-with-a-long-name-to-pass-the-width.o", app],
			output: Archive,
		},
	}

# The rewrite can't reach this line, so it stays a finding.
host : U8 -> U8
host = |a| step(a)
