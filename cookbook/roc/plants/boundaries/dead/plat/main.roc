# Quiet for boundaries' `dead`: a platform's `provides` are its host's to call.
# `Shown` is exposed, so a member nothing calls is `unused_api`.
platform ""
	requires {
		start : U64 -> U64
	}
	exposes [UnusedApi]
	packages {}
	provides {
		"roc_start": start_for_host,
	}
	targets: {
		inputs_dir: "targets/",
	}

import UnusedApi

start_for_host = |x| x
