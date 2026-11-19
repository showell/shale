## A platform's `import Host` is used by its header's hosted block alone.
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

start_for_host! : {} => [Failed, Ready]
start_for_host! = |{}| start!({})
