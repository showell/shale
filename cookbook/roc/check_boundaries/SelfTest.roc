## The recipe's self-test, run after the checks over the plants in
## cookbook/roc/plants/check_boundaries/: each plant is named for the check it
## holds (`fixed_string.roc` holds `fixed`), and each check must fire on its
## own plants and on no others, and on no client; a `quiet_*.roc` plant gives
## no finding. A plant under `typed/` or `dead/` is a module, read in snake
## case (`BoolParam.roc` as `bool_param.roc`).
import pf.Shale
import lib.Finding
import lib.Text

SelfTest := [].{

	## What the checks found over the plants: the checked files, the clients,
	## how many files `tests=` named, and the findings printed.
	Judged : {
		paths : List(Str),
		clients : List(Str),
		test_count : U64,
		findings : List(Finding),
	}

	## The plants' verdict printed, then a stop when any plant is wrong.
	run! : SelfTest.Judged, List(Str) => {}
	run! = |judged, checks| {
		fired = Set.from_list(judged.findings.map(|finding| "${finding.path}|${finding.check}"))
		var $failures = []
		for check in checks {
			$planted : U64
			var $planted = 0
			for path in judged.paths {
				name = plant_name(path)
				is_mine = name == "${check}.roc" or name.starts_with("${check}_")
				if is_mine {
					$planted = $planted + 1
				}
				did_fire = fired.contains("${path}|${check}")
				if is_mine and !(did_fire) {
					$failures = $failures.append("`${check}` does not fire on its plant ${path}")
				}
				if did_fire and !(is_mine) {
					$failures = $failures.append("`${check}` fires on ${path}, another check's plant")
				}
			}
			if $planted == 0 {
				$failures = $failures.append("`${check}` has no plant")
			}
			for path in judged.clients {
				if fired.contains("${path}|${check}") {
					$failures = $failures.append("`${check}` fires on the client ${path}, which is read only for its calls")
				}
			}
		}
		if judged.clients.is_empty() or judged.test_count == 0 {
			$failures = $failures.append("no client or no gate plant read")
		}
		for failure in $failures {
			Shale.print!("FAIL ${failure}")
		}
		if !($failures.is_empty()) {
			Shale.fail!("check_boundaries self_test: ${$failures.len().to_str()} plants wrong")
		}
		Shale.print!("check_boundaries (Roc) self_test: PASS (each check fires on its plants alone, the quiet_ plants on none)")
	}

	## The plants' folder.
	plants : Str
	plants = "cookbook:roc/plants/check_boundaries/"

	## The typed plants, judged with the types under `QuietApp.roc`.
	typed_plants : Str
	typed_plants = "${SelfTest.plants}typed/"

	## The plants `dead` and `unused_api` are judged on, since nothing calls
	## the others.
	dead_plants : Str
	dead_plants = "${SelfTest.plants}dead/"
}

## A plant's file name as the self-test reads it: a typed or `dead/` plant's
## in snake case.
plant_name : Str -> Str
plant_name = |path| {
	base = Text.basename(path)
	if path.starts_with(SelfTest.typed_plants) or path.starts_with(SelfTest.dead_plants) {
		snake_case(base)
	} else {
		base
	}
}

## A CamelCase file name in snake case: `_` before each capital but a first,
## and the capital made small.
snake_case : Str -> Str
snake_case = |name| {
	var $out = []
	for byte in name.to_utf8() {
		if Text.is_capital(byte) {
			if !($out.is_empty()) {
				$out = $out.append('_')
			}
			$out = $out.append(byte + 32)
		} else {
			$out = $out.append(byte)
		}
	}
	Str.from_utf8_lossy($out)
}
