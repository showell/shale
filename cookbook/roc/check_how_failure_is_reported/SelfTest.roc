## The recipe's self-test, run after the checks over the plants in
## cookbook/roc/plants/check_how_failure_is_reported/ and its typed/ folder: each plant is
## named for the check it holds, and that check must fire on it and on no
## other check's plant; a `quiet_*.roc` plant must draw no finding, and a
## plant named for no check is a failure; every error a plant names must be
## declared in a plant; and every `optional_tag` finding must point at a line
## that names its tag.
import pf.Shale
import lib.Finding
import lib.Text

SelfTest := [].{

	## What the checks found over the plants.
	Judged : {
		paths : List(Str),
		findings : List(Finding),
		optionals : List({ finding : Finding, tag : Str }),
		unjudged : List(Str),
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
				is_mine = is_plant_for(plant_name(path), check)
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
		}
		# No check would ever fail on a plant named for none of them.
		for path in judged.paths {
			name = plant_name(path)
			is_named = name.starts_with("quiet_") or checks.any(|check| is_plant_for(name, check))
			if !(is_named) {
				$failures = $failures.append("${path}: a plant the table does not judge")
			}
		}
		# Every error a plant names is declared in a plant; otherwise a quiet
		# plant could pass without `error_alias` judging it.
		for error_name in judged.unjudged {
			$failures = $failures.append("`error_alias` cannot judge `${error_name}`, declared in no plant")
		}
		for optional in judged.optionals {
			finding = optional.finding
			source_lines = read_text!(finding.path).split_on("\n")
			is_named_there = finding.line != 0 and Text.line_at(source_lines, finding.line).contains(optional.tag)
			if !(is_named_there) {
				line = if finding.line == 0 {
					"?"
				} else {
					finding.line.to_str()
				}
				$failures = $failures.append("`optional_tag` says ${finding.path}:${line}, a line that does not name `${optional.tag}`")
			}
		}
		for failure in $failures {
			Shale.print!("FAIL ${failure}")
		}
		if !($failures.is_empty()) {
			Shale.fail!("check_how_failure_is_reported self_test: ${$failures.len().to_str()} plants wrong")
		}
		proof = "each check fires on its plants alone, the quiet_ plants on none, each optional at its tag's line"
		Shale.print!("check_how_failure_is_reported (Roc) self_test: PASS (${proof})")
	}

	## The plants' folder.
	plants : Str
	plants = "cookbook:roc/plants/check_how_failure_is_reported/"
}

read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("check_how_failure_is_reported self_test: ${path} went missing during the check")
			""
		}
	}

## A plant's name below the plants' folder. A typed plant's module name is
## read in snake case: `typed/PlaceholderIndex.roc` is
## `placeholder_index.roc`.
plant_name : Str -> Str
plant_name = |path| {
	name = path.drop_prefix(SelfTest.plants)
	if name.starts_with("typed/") {
		snake_case(name.drop_prefix("typed/"))
	} else {
		name
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

is_plant_for : Str, Str -> Bool
is_plant_for = |name, check| name == "${check}.roc" or name.starts_with("${check}_")
