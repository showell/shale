## Quiet for every check: a platform's docs name what it gives the host,
## `roc_plan`, and what the host defines for it, `roc_crashed`.
platform ""
	requires {
		plan_json : U32 -> List(U8)
	}
	exposes []
	packages {}
	provides { "roc_plan": plan_for_host }
	targets: {
		x64musl: {
			inputs: [app],
			output: Archive,
		},
	}

plan_for_host : U32 -> List(U8)
plan_for_host = |seed| plan_json(seed)
