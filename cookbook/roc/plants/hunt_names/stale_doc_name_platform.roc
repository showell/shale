## A plant for `stale_doc_name`: a platform's doc that names a function
## gone from it, `plan_for_guest`.
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
