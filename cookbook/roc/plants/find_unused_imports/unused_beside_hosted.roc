## The hosted block uses `Host` alone, so `import Http` is still unused.
platform ""
	requires {
		start! : {} => [Failed, Ready]
	}
	exposes [
		Host,
	]
	packages {}
	provides {
		"roc_start": start_for_host!,
	}
	hosted {
		"read_file": Host.read_file!,
	}
	targets: {
		x64musl: {
			inputs: [app],
			output: Archive,
		},
	}

import Host
import Http

start_for_host! : {} => [Failed, Ready]
start_for_host! = |{}| start!({})
