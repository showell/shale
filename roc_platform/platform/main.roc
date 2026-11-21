## main.roc: written by c/gen_builtins.shale from c/builtins.txt; never edit
## it. Shale's Roc platform (roc_platform/README.md). The host is C Shale's
## runtime used as a library: it gives the app Shale's effects (Shale.roc),
## and calls the app's `main!` once.
##
## Each hosted function's symbol is `shale_` and its name in Shale.roc,
## without the `!`.
platform ""
	requires {
		main! : {} => {}
	}
	exposes [
		Shale,
	]
	packages {}
	provides {
		"roc_main": main_for_host!,
	}
	hosted {
		"shale_arg": Shale.arg!,
		"shale_exists": Shale.exists!,
		"shale_fail": Shale.fail!,
		"shale_glob": Shale.glob!,
		"shale_grep_all": Shale.grep_all!,
		"shale_list": Shale.list!,
		"shale_mkdir": Shale.mkdir!,
		"shale_print": Shale.print!,
		"shale_re_find": Shale.re_find!,
		"shale_re_find_all": Shale.re_find_all!,
		"shale_re_lines": Shale.re_lines!,
		"shale_re_match": Shale.re_match!,
		"shale_re_replace": Shale.re_replace!,
		"shale_read": Shale.read!,
		"shale_real": Shale.real!,
		"shale_remove_tree": Shale.remove_tree!,
		"shale_roc_tree": Shale.roc_tree!,
		"shale_run": Shale.run!,
		"shale_write": Shale.write!,
	}
	targets: {
		x64musl: {
			inputs: [app],
			output: Archive,
		},
	}

import Shale

main_for_host! : {} => {}
main_for_host! = |{}| main!({})
